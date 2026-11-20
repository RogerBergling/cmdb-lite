package main

// demo-seed creates the encrypted demo database used by the public demo image.
// It drives the same cmdb-rpc JSON-lines protocol as cmdb-server, so the
// fixtures go through the real import, agent, diagram, and auth-state paths.

import (
	"bufio"
	"crypto/rand"
	"encoding/base64"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"log"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"time"

	"golang.org/x/crypto/argon2"
)

const (
	demoUsername      = "guest"
	demoPassword      = "guest"
	demoTOTPSecret    = "JBSWY3DPEHPK3PXPJBSWY3DPEHPK3PXP"
	demoAgentVersion  = "0.7.0"
	demoOwner         = "Nordwind Logistics"
	passwordMemoryKiB = 64 * 1024
	passwordTime      = 3
	passwordThreads   = 2
	passwordKeyLen    = 32
	passwordSaltLen   = 16
)

var demoRecoveryCodes = []string{
	"DEMO2AAAAAA2",
	"DEMO2BBBBBB3",
	"DEMO2CCCCCC4",
	"DEMO2DDDDDD5",
	"DEMO2EEEEEE6",
	"DEMO2FFFFFF7",
	"DEMO2GGGGGG2",
	"DEMO2HHHHHH3",
	"DEMO2IIIIII4",
	"DEMO2JJJJJJ5",
}

type rpcClient struct {
	cmd    *exec.Cmd
	stdin  io.WriteCloser
	stdout *bufio.Reader
	next   int
}

func startRPC(path string) (*rpcClient, error) {
	cmd := exec.Command(path)
	cmd.Stderr = os.Stderr
	stdin, err := cmd.StdinPipe()
	if err != nil {
		return nil, err
	}
	stdout, err := cmd.StdoutPipe()
	if err != nil {
		return nil, err
	}
	if err := cmd.Start(); err != nil {
		return nil, err
	}
	return &rpcClient{cmd: cmd, stdin: stdin, stdout: bufio.NewReaderSize(stdout, 1<<20)}, nil
}

func (r *rpcClient) call(method string, params map[string]any) (json.RawMessage, error) {
	r.next++
	request, err := json.Marshal(map[string]any{
		"id":     r.next,
		"method": method,
		"params": params,
		"actor":  "demo-seed",
	})
	if err != nil {
		return nil, err
	}
	if _, err := r.stdin.Write(append(request, '\n')); err != nil {
		return nil, err
	}
	line, err := r.stdout.ReadBytes('\n')
	if err != nil {
		return nil, err
	}
	var response struct {
		OK     bool            `json:"ok"`
		Result json.RawMessage `json:"result"`
		Error  struct {
			Code    string `json:"code"`
			Message string `json:"message"`
			Detail  string `json:"detail"`
		} `json:"error"`
	}
	if err := json.Unmarshal(line, &response); err != nil {
		return nil, fmt.Errorf("%s: invalid RPC response: %w", method, err)
	}
	if !response.OK {
		message := response.Error.Message
		if message == "" {
			message = "RPC request failed"
		}
		if response.Error.Detail != "" {
			message += ": " + response.Error.Detail
		}
		return nil, fmt.Errorf("%s: %s", method, message)
	}
	return response.Result, nil
}

func (r *rpcClient) callInto(method string, params map[string]any, out any) error {
	raw, err := r.call(method, params)
	if err != nil {
		return err
	}
	return json.Unmarshal(raw, out)
}

func (r *rpcClient) close() {
	if r == nil {
		return
	}
	_ = r.stdin.Close()
	_ = r.cmd.Wait()
}

type importReport struct {
	Rows       int `json:"rows"`
	Created    int `json:"created"`
	Updated    int `json:"updated"`
	Skipped    int `json:"skipped"`
	ErrorCount int `json:"error_count"`
	Errors     []struct {
		Line    int    `json:"line"`
		Column  string `json:"column"`
		Message string `json:"message"`
	} `json:"errors"`
}

type ciItem struct {
	ID   string `json:"id"`
	Name string `json:"name"`
}

type relType struct {
	ID   string `json:"id"`
	Name string `json:"name"`
}

type agentEnrollment struct {
	Agent struct {
		ID string `json:"id"`
	} `json:"agent"`
}

type hostFixture struct {
	DisplayName  string
	Hostname     string
	MachineID    string
	OSFamily     string
	OSName       string
	OSVersion    string
	Architecture string
	LastSeenIP   string
	CI           string
}

type appFixture struct {
	Key             string
	Name            string
	Version         string
	Publisher       string
	InstallDate     string
	InstallLocation string
	SizeKB          int64
}

type demoCredentialsFile struct {
	Username      string   `json:"username"`
	Password      string   `json:"password"`
	TOTPSecret    string   `json:"totp_secret"`
	RecoveryCodes []string `json:"recovery_codes"`
}

type recoveryRecord struct {
	Hash   string `json:"hash"`
	UsedAt string `json:"used_at,omitempty"`
}

type userRecord struct {
	ID                 string           `json:"id"`
	Username           string           `json:"username"`
	PasswordHash       string           `json:"password_hash"`
	IsAdmin            bool             `json:"is_admin"`
	Disabled           bool             `json:"disabled"`
	MustChangePassword bool             `json:"must_change_password,omitempty"`
	TOTPSecret         string           `json:"totp_secret,omitempty"`
	TOTPConfirmed      bool             `json:"totp_confirmed"`
	TOTPLastStep       int64            `json:"totp_last_step"`
	RecoveryCodes      []recoveryRecord `json:"recovery_codes,omitempty"`
	CreatedAt          string           `json:"created_at"`
	UpdatedAt          string           `json:"updated_at"`
}

type authState struct {
	Version int          `json:"version"`
	Users   []userRecord `json:"users"`
}

func main() {
	rpcPath := flag.String("rpc", "", "path to cmdb-rpc")
	dbPath := flag.String("db", "", "path to the demo database")
	passphrase := flag.String("passphrase", "", "database passphrase")
	fixturesDir := flag.String("fixtures", "", "directory containing cis.csv and relationships.csv")
	credentialsPath := flag.String("credentials", "", "path for the generated demo credentials file")
	flag.Parse()

	for name, value := range map[string]string{
		"-rpc":         *rpcPath,
		"-db":          *dbPath,
		"-passphrase":  *passphrase,
		"-fixtures":    *fixturesDir,
		"-credentials": *credentialsPath,
	} {
		if strings.TrimSpace(value) == "" {
			log.Fatalf("%s is required", name)
		}
	}
	if err := os.MkdirAll(filepath.Dir(*dbPath), 0700); err != nil {
		log.Fatal(err)
	}

	cisCSV, err := os.ReadFile(filepath.Join(*fixturesDir, "cis.csv"))
	if err != nil {
		log.Fatal(err)
	}
	relationshipsCSV, err := os.ReadFile(filepath.Join(*fixturesDir, "relationships.csv"))
	if err != nil {
		log.Fatal(err)
	}

	rpc, err := startRPC(*rpcPath)
	if err != nil {
		log.Fatalf("start %s: %v", *rpcPath, err)
	}
	defer rpc.close()

	var created map[string]any
	if err := rpc.callInto("db.create", map[string]any{
		"path":       *dbPath,
		"passphrase": *passphrase,
	}, &created); err != nil {
		log.Fatalf("create database: %v", err)
	}
	log.Printf("created demo database")

	if err := importCSV(rpc, "ci_csv", string(cisCSV), "configuration items"); err != nil {
		log.Fatal(err)
	}
	if err := importCSV(rpc, "rel_csv", string(relationshipsCSV), "relationships"); err != nil {
		log.Fatal(err)
	}

	cis := map[string]string{}
	var ciList struct {
		Items []ciItem `json:"items"`
	}
	if err := rpc.callInto("cis.list", map[string]any{"limit": 200}, &ciList); err != nil {
		log.Fatalf("list CIs: %v", err)
	}
	for _, item := range ciList.Items {
		cis[strings.ToLower(item.Name)] = item.ID
	}
	log.Printf("loaded %d configuration items", len(cis))

	var relTypes struct {
		Types []relType `json:"items"`
	}
	if err := rpc.callInto("relationships.types", map[string]any{}, &relTypes); err != nil {
		log.Fatalf("list relationship types: %v", err)
	}
	relTypeIDs := map[string]string{}
	for _, item := range relTypes.Types {
		relTypeIDs[strings.ToLower(item.Name)] = item.ID
	}

	if err := seedDiagram(rpc, cis, relTypeIDs); err != nil {
		log.Fatal(err)
	}
	if err := seedAgents(rpc, cis); err != nil {
		log.Fatal(err)
	}
	if err := seedAuth(rpc, *credentialsPath); err != nil {
		log.Fatal(err)
	}

	var locked map[string]any
	if err := rpc.callInto("db.lock", map[string]any{}, &locked); err != nil {
		log.Fatalf("lock database: %v", err)
	}
	log.Printf("demo seed complete; login %s / %s", demoUsername, demoPassword)
}

func importCSV(rpc *rpcClient, format, content, label string) error {
	var report importReport
	err := rpc.callInto("import.commit", map[string]any{
		"format":               format,
		"content":              content,
		"create_missing_types": false,
		"duplicate_policy":     "error",
	}, &report)
	if err != nil {
		return fmt.Errorf("import %s: %w", label, err)
	}
	if report.ErrorCount > 0 {
		if len(report.Errors) > 0 {
			return fmt.Errorf("import %s: %d rows failed (line %d: %s)", label, report.ErrorCount, report.Errors[0].Line, report.Errors[0].Message)
		}
		return fmt.Errorf("import %s: %d rows failed", label, report.ErrorCount)
	}
	log.Printf("imported %s: %d created, %d updated, %d skipped", label, report.Created, report.Updated, report.Skipped)
	return nil
}

func seedDiagram(rpc *rpcClient, cis map[string]string, relTypes map[string]string) error {
	names := []string{"erp-web", "erp-api-01", "db-erp-primary", "srv-db-01", "fw-edge-01"}
	nodes := make([]struct {
		Name string
		ID   string
		X    int
		Y    int
	}, 0, len(names))
	positions := [][2]int{{40, 40}, {280, 40}, {520, 40}, {520, 200}, {40, 200}}
	for i, name := range names {
		id := cis[strings.ToLower(name)]
		if id == "" {
			continue
		}
		nodes = append(nodes, struct {
			Name string
			ID   string
			X    int
			Y    int
		}{name, id, positions[i][0], positions[i][1]})
	}
	if len(nodes) < 3 {
		return nil
	}
	usesID := relTypes["uses"]
	runsOnID := relTypes["runs_on"]
	cells := `<mxCell id="0"/><mxCell id="1" parent="0"/>`
	for _, node := range nodes {
		cells += fmt.Sprintf(
			`<mxCell id="ci-%s" value="%s" style="rounded=1;whiteSpace=wrap;html=1;" vertex="1" parent="1" cmdbTypeName="Demo"><mxGeometry x="%d" y="%d" width="180" height="64" as="geometry"/></mxCell>`,
			node.ID, htmlEscape(node.Name), node.X, node.Y)
	}
	edges := []struct {
		ID     string
		Value  string
		TypeID string
		Source string
		Target string
	}{
		{"e1", "uses", usesID, cis["erp-web"], cis["erp-api-01"]},
		{"e2", "uses", usesID, cis["erp-api-01"], cis["db-erp-primary"]},
		{"e3", "runs_on", runsOnID, cis["db-erp-primary"], cis["srv-db-01"]},
	}
	for _, edge := range edges {
		if edge.Source == "" || edge.Target == "" || edge.TypeID == "" {
			continue
		}
		cells += fmt.Sprintf(
			`<mxCell id="edge-%s" value="%s" style="edgeStyle=orthogonalEdgeStyle;rounded=0;html=1;" edge="1" parent="1" source="ci-%s" target="ci-%s" cmdbTypeId="%s"><mxGeometry relative="1" as="geometry"/></mxCell>`,
			edge.ID, edge.Value, edge.Source, edge.Target, edge.TypeID)
	}
	xml := `<mxfile host="cmdb-lite" type="device"><diagram id="demo-erp" name="ERP order-to-cash overview"><mxGraphModel dx="1000" dy="700" grid="1" gridSize="10" guides="1" tooltips="1" connect="1" arrows="1" fold="1" page="1" pageScale="1" pageWidth="1000" pageHeight="700" math="0" shadow="0"><root>` +
		cells + `</root></mxGraphModel></diagram></mxfile>`
	var result map[string]any
	if err := rpc.callInto("diagrams.save", map[string]any{
		"name": "ERP order-to-cash overview",
		"xml":  xml,
	}, &result); err != nil {
		return fmt.Errorf("seed diagram: %w", err)
	}
	log.Printf("seeded diagram")
	return nil
}

func seedAgents(rpc *rpcClient, cis map[string]string) error {
	hosts := []hostFixture{
		{"App server 01", "srv-app-01", "machine-srv-app-01", "linux", "Ubuntu", "24.04", "x86_64", "10.20.10.31", "srv-app-01"},
		{"App server 02", "srv-app-02", "machine-srv-app-02", "linux", "Ubuntu", "24.04", "x86_64", "10.20.10.32", "srv-app-02"},
		{"Database server 01", "srv-db-01", "machine-srv-db-01", "linux", "Ubuntu", "24.04", "x86_64", "10.20.10.41", "srv-db-01"},
		{"Web server 01", "srv-web-01", "machine-srv-web-01", "linux", "Ubuntu", "24.04", "x86_64", "10.20.30.21", "srv-web-01"},
		{"Monitoring server", "srv-mon-01", "machine-srv-mon-01", "linux", "Ubuntu", "24.04", "x86_64", "10.20.10.51", "srv-mon-01"},
		{"Domain controller 01", "srv-ad-01", "machine-srv-ad-01", "windows", "Windows Server", "2022", "x86_64", "10.20.10.61", "srv-ad-01"},
		{"File server 01", "srv-file-01", "machine-srv-file-01", "windows", "Windows Server", "2022", "x86_64", "10.20.10.63", "srv-file-01"},
		{"Warehouse VM", "vm-warehouse-01", "machine-vm-warehouse-01", "linux", "Ubuntu", "22.04", "x86_64", "10.20.10.73", "vm-warehouse-01"},
	}

	for index, host := range hosts {
		var tokenResult struct {
			Token string `json:"token"`
		}
		if err := rpc.callInto("agents.token_create", map[string]any{
			"label":       "Demo seed: " + host.Hostname,
			"ttl_seconds": 3600,
		}, &tokenResult); err != nil {
			return fmt.Errorf("create enrollment token for %s: %w", host.Hostname, err)
		}
		if tokenResult.Token == "" {
			return fmt.Errorf("create enrollment token for %s: empty token", host.Hostname)
		}
		facts := map[string]any{
			"display_name":  host.DisplayName,
			"hostname":      host.Hostname,
			"machine_id":    host.MachineID,
			"os_family":     host.OSFamily,
			"os_name":       host.OSName,
			"os_version":    host.OSVersion,
			"architecture":  host.Architecture,
			"agent_version": demoAgentVersion,
			"last_seen_ip":  host.LastSeenIP,
		}
		var enrollment agentEnrollment
		enrollParams := map[string]any{"token": tokenResult.Token}
		for key, value := range facts {
			enrollParams[key] = value
		}
		if err := rpc.callInto("agents.enroll", enrollParams, &enrollment); err != nil {
			return fmt.Errorf("enroll %s: %w", host.Hostname, err)
		}
		if enrollment.Agent.ID == "" {
			return fmt.Errorf("enroll %s: empty agent id", host.Hostname)
		}

		for revision, collectedAt := range []time.Time{
			time.Now().UTC().Add(-26 * time.Hour),
			time.Now().UTC().Add(-2 * time.Hour),
		} {
			reportParams := map[string]any{
				"agent_id":         enrollment.Agent.ID,
				"collected_at":     collectedAt.Format(time.RFC3339),
				"apps":             hostApps(index, revision),
				"collector_errors": "",
				"complete":         true,
				"retention_days":   365,
				"max_reports":      90,
			}
			for key, value := range facts {
				reportParams[key] = value
			}
			var reported map[string]any
			if err := rpc.callInto("agents.report", reportParams, &reported); err != nil {
				return fmt.Errorf("report %s (revision %d): %w", host.Hostname, revision, err)
			}
		}

		ciID := cis[strings.ToLower(host.CI)]
		if ciID != "" {
			var linked map[string]any
			if err := rpc.callInto("agents.link_ci", map[string]any{
				"id":    enrollment.Agent.ID,
				"ci_id": ciID,
			}, &linked); err != nil {
				return fmt.Errorf("link %s to %s: %w", host.Hostname, host.CI, err)
			}
		}
	}
	log.Printf("seeded %d application inventory hosts", len(hosts))
	return nil
}

func seedAuth(rpc *rpcClient, credentialsPath string) error {
	now := time.Now().UTC().Format("2006-01-02T15:04:05Z")
	guestHash, err := hashPassword(demoPassword)
	if err != nil {
		return err
	}
	aliceHash, err := hashPassword("demo-alice-12")
	if err != nil {
		return err
	}
	bobHash, err := hashPassword("demo-bob-1234")
	if err != nil {
		return err
	}
	recovery := make([]recoveryRecord, 0, len(demoRecoveryCodes))
	for _, code := range demoRecoveryCodes {
		hash, err := hashPassword("recovery:" + code)
		if err != nil {
			return err
		}
		recovery = append(recovery, recoveryRecord{Hash: hash})
	}

	state := authState{
		Version: 1,
		Users: []userRecord{
			{
				ID:            "11111111-1111-4111-8111-111111111111",
				Username:      demoUsername,
				PasswordHash:  guestHash,
				IsAdmin:       true,
				TOTPSecret:    demoTOTPSecret,
				TOTPConfirmed: true,
				RecoveryCodes: recovery,
				CreatedAt:     now,
				UpdatedAt:     now,
			},
			{
				ID:                 "22222222-2222-4222-8222-222222222222",
				Username:           "alice",
				PasswordHash:       aliceHash,
				MustChangePassword: true,
				CreatedAt:          now,
				UpdatedAt:          now,
			},
			{
				ID:           "33333333-3333-4333-8333-333333333333",
				Username:     "bob",
				PasswordHash: bobHash,
				Disabled:     true,
				CreatedAt:    now,
				UpdatedAt:    now,
			},
		},
	}
	payload, err := json.Marshal(state)
	if err != nil {
		return err
	}
	var saved map[string]any
	if err := rpc.callInto("auth.state.set", map[string]any{
		"state": string(payload),
	}, &saved); err != nil {
		return fmt.Errorf("seed auth state: %w", err)
	}

	credentials := demoCredentialsFile{
		Username:      demoUsername,
		Password:      demoPassword,
		TOTPSecret:    demoTOTPSecret,
		RecoveryCodes: demoRecoveryCodes,
	}
	encoded, err := json.MarshalIndent(credentials, "", "  ")
	if err != nil {
		return err
	}
	if err := os.MkdirAll(filepath.Dir(credentialsPath), 0700); err != nil {
		return err
	}
	if err := os.WriteFile(credentialsPath, encoded, 0600); err != nil {
		return err
	}
	if err := os.Chmod(credentialsPath, 0600); err != nil {
		return err
	}
	log.Printf("seeded auth state and %s", credentialsPath)
	return nil
}

func hashPassword(password string) (string, error) {
	salt := make([]byte, passwordSaltLen)
	if _, err := rand.Read(salt); err != nil {
		return "", err
	}
	key := argon2.IDKey([]byte(password), salt, passwordTime,
		passwordMemoryKiB, passwordThreads, passwordKeyLen)
	return fmt.Sprintf("$argon2id$v=19$m=%d,t=%d,p=%d$%s$%s",
		passwordMemoryKiB, passwordTime, passwordThreads,
		base64.RawStdEncoding.EncodeToString(salt),
		base64.RawStdEncoding.EncodeToString(key)), nil
}

func hostApps(hostIndex, revision int) []map[string]any {
	pool := linuxAppPool
	source := "deb"
	if hostIndex == 5 || hostIndex == 6 {
		pool = windowsAppPool
		source = "windows"
	}
	start := (hostIndex * 3) % len(pool)
	count := 12 + hostIndex%4
	apps := make([]map[string]any, 0, count+1)
	used := map[string]bool{}
	for i := 0; i < count; i++ {
		item := pool[(start+i)%len(pool)]
		if used[item.Key] {
			continue
		}
		used[item.Key] = true
		version := item.Version
		if revision == 1 && i == 1 {
			version = bumpedVersion(version)
		}
		apps = append(apps, map[string]any{
			"key":              item.Key,
			"name":             item.Name,
			"version":          version,
			"publisher":        item.Publisher,
			"source":           source,
			"architecture":     "amd64",
			"install_date":     item.InstallDate,
			"install_location": item.InstallLocation,
			"size_kb":          item.SizeKB,
		})
	}
	if revision == 1 {
		if len(apps) > 2 {
			apps = apps[:len(apps)-1]
		}
		extra := extraApp(hostIndex, source)
		apps = append(apps, map[string]any{
			"key":              extra.Key,
			"name":             extra.Name,
			"version":          extra.Version,
			"publisher":        extra.Publisher,
			"source":           source,
			"architecture":     "amd64",
			"install_date":     extra.InstallDate,
			"install_location": extra.InstallLocation,
			"size_kb":          extra.SizeKB,
		})
	}
	return apps
}

func bumpedVersion(version string) string {
	parts := strings.Split(version, ".")
	if len(parts) == 0 {
		return "2.0.0"
	}
	parts[0] = "2"
	return strings.Join(parts, ".")
}

func extraApp(hostIndex int, source string) appFixture {
	if source == "windows" {
		return appFixture{
			Key: "win:microsoft-teams", Name: "Microsoft Teams",
			Version: "24193.1805.3040.8974", Publisher: "Microsoft Corporation",
			InstallDate: "2026-10-02", InstallLocation: `C:\Program Files\Microsoft\Teams`, SizeKB: 412000,
		}
	}
	return appFixture{
		Key: fmt.Sprintf("deb:demo-agent-%d", hostIndex), Name: "cmdb-lite agent",
		Version: demoAgentVersion, Publisher: demoOwner,
		InstallDate: "2026-11-01", InstallLocation: "/usr/bin/cmdb-agent", SizeKB: 12500,
	}
}

func htmlEscape(value string) string {
	replacer := strings.NewReplacer("&", "&amp;", "<", "&lt;", ">", "&gt;", `"`, "&quot;", "'", "&apos;")
	return replacer.Replace(value)
}

var linuxAppPool = []appFixture{
	{"deb:openssh-server", "OpenSSH server", "9.6p1-3ubuntu13", "OpenBSD", "2026-01-15", "/usr/sbin/sshd", 1250},
	{"deb:nginx-core", "nginx", "1.24.0-2ubuntu7", "nginx.org", "2026-01-15", "/usr/sbin/nginx", 1420},
	{"deb:postgresql-16", "PostgreSQL 16", "16.4-1.pgdg24.04", "PostgreSQL Global Development Group", "2026-02-01", "/usr/lib/postgresql/16", 18200},
	{"deb:redis-server", "Redis server", "7:7.0.15-1build2", "Redis Ltd.", "2026-01-20", "/usr/bin/redis-server", 3100},
	{"deb:python3", "Python 3", "3.12.3-0ubuntu2", "Python Software Foundation", "2026-01-15", "/usr/bin/python3", 6800},
	{"deb:openjdk-21-jre-headless", "OpenJDK 21 JRE", "21.0.4+7-1ubuntu2", "Oracle", "2026-02-10", "/usr/lib/jvm/java-21-openjdk-amd64", 182000},
	{"deb:docker-ce", "Docker Engine", "5:27.1.1-1~ubuntu.24.04", "Docker Inc.", "2026-03-02", "/usr/bin/docker", 124000},
	{"deb:prometheus-node-exporter", "Prometheus node exporter", "1.7.0-1", "Prometheus Authors", "2026-02-15", "/usr/bin/prometheus-node-exporter", 9800},
	{"deb:filebeat", "Filebeat", "8.15.0", "Elastic", "2026-03-10", "/usr/share/filebeat", 54000},
	{"deb:chrony", "chrony", "4.5-1ubuntu4", "chrony project", "2026-01-15", "/usr/sbin/chronyd", 410},
	{"deb:rsyslog", "rsyslog", "8.2312.0-3ubuntu9", "rsyslog project", "2026-01-15", "/usr/sbin/rsyslogd", 1800},
	{"deb:fail2ban", "fail2ban", "1.0.2-3", "fail2ban project", "2026-02-20", "/usr/bin/fail2ban-server", 920},
	{"deb:unattended-upgrades", "unattended-upgrades", "2.9.1+nmu4ubuntu1", "Debian", "2026-01-15", "/usr/share/unattended-upgrades", 260},
	{"deb:curl", "curl", "8.5.0-2ubuntu10.6", "Daniel Stenberg", "2026-04-12", "/usr/bin/curl", 540},
	{"deb:ca-certificates", "CA certificates", "20240203", "Mozilla", "2026-01-15", "/etc/ssl/certs", 420},
	{"deb:jq", "jq", "1.7.1-3build1", "Stephen Dolan", "2026-01-15", "/usr/bin/jq", 190},
	{"deb:htop", "htop", "3.3.0-4build1", "Hisham Muhammad", "2026-01-15", "/usr/bin/htop", 380},
	{"deb:tmux", "tmux", "3.4-1build1", "tmux project", "2026-01-15", "/usr/bin/tmux", 720},
}

var windowsAppPool = []appFixture{
	{"win:microsoft-edge", "Microsoft Edge", "130.0.2849.68", "Microsoft Corporation", "2026-10-30", `C:\Program Files (x86)\Microsoft\Edge`, 512000},
	{"win:microsoft-365", "Microsoft 365 Apps for enterprise", "16.0.17928.20216", "Microsoft Corporation", "2026-09-18", `C:\Program Files\Microsoft Office`, 2240000},
	{"win:7zip", "7-Zip 24.08", "24.08", "Igor Pavlov", "2026-08-12", `C:\Program Files\7-Zip`, 6200},
	{"win:python312", "Python 3.12.7", "3.12.7150.0", "Python Software Foundation", "2026-10-05", `C:\Program Files\Python312`, 128000},
	{"win:java21", "Java 21 Runtime", "21.0.5", "Oracle", "2026-10-16", `C:\Program Files\Java\jre-21`, 192000},
	{"win:ssms", "SQL Server Management Studio", "20.2.30.0", "Microsoft Corporation", "2026-07-22", `C:\Program Files\Microsoft SQL Server Management Studio 20`, 840000},
	{"win:notepadpp", "Notepad++", "8.6.9", "Notepad++ Team", "2026-07-11", `C:\Program Files\Notepad++`, 12500},
	{"win:git", "Git for Windows", "2.46.0", "The Git Development Community", "2026-08-01", `C:\Program Files\Git`, 318000},
	{"win:vscode", "Visual Studio Code", "1.94.2", "Microsoft Corporation", "2026-10-10", `C:\Program Files\Microsoft VS Code`, 356000},
	{"win:putty", "PuTTY", "0.81", "Simon Tatham", "2026-06-18", `C:\Program Files\PuTTY`, 8200},
	{"win:adobreader", "Adobe Acrobat Reader", "24.003.20112", "Adobe Inc.", "2026-09-02", `C:\Program Files\Adobe\Acrobat DC`, 412000},
	{"win:veeam-agent", "Veeam Agent for Windows", "6.2.0.1144", "Veeam Software", "2026-05-20", `C:\Program Files\Veeam\Endpoint Backup`, 186000},
}
