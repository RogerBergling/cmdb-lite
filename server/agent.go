package main

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"log"
	"net"
	"net/http"
	"strings"
	"unicode"
)

const (
	agentMaxEnrollBytes = 64 << 10
	agentMaxReportBytes = 32 << 20
	agentMaxApps        = 20000
)

type agentHost struct {
	DisplayName   string `json:"display_name"`
	Hostname      string `json:"hostname"`
	MachineID     string `json:"machine_id"`
	OSFamily      string `json:"os_family"`
	OSName        string `json:"os_name"`
	OSVersion     string `json:"os_version"`
	Architecture  string `json:"architecture"`
	AgentVersion  string `json:"agent_version"`
	IPAddress     string `json:"ip_address"`
	SerialNumber  string `json:"serial_number"`
	CPUCores      int    `json:"cpu_cores"`
	MemoryMB      int64  `json:"memory_mb"`
	KernelVersion string `json:"kernel_version"`
}

type agentApp struct {
	Key             string `json:"key"`
	Name            string `json:"name"`
	Version         string `json:"version"`
	Publisher       string `json:"publisher"`
	Source          string `json:"source"`
	Architecture    string `json:"architecture"`
	InstallDate     string `json:"install_date"`
	InstallLocation string `json:"install_location"`
	SizeKB          int64  `json:"size_kb"`
}

type agentEnrollRequest struct {
	Token string    `json:"token"`
	Host  agentHost `json:"host"`
}

type agentReportRequest struct {
	CollectedAt     string     `json:"collected_at"`
	Host            agentHost  `json:"host"`
	Apps            []agentApp `json:"apps"`
	CollectorErrors string     `json:"collector_errors"`
	Complete        *bool      `json:"complete"`
}

type agentIdentity struct {
	ID          string `json:"id"`
	DisplayName string `json:"display_name"`
}

func agentTextOK(value string, max int) bool {
	if len(value) > max {
		return false
	}
	for _, r := range value {
		if unicode.IsControl(r) || unicode.Is(unicode.Cf, r) ||
			r == '\u2028' || r == '\u2029' {
			return false
		}
	}
	return true
}

func validAgentToken(value string) bool {
	if len(value) != 64 {
		return false
	}
	_, err := hex.DecodeString(value)
	return err == nil
}

func (h *agentHost) validate() error {
	if !agentTextOK(h.DisplayName, 255) ||
		!agentTextOK(h.Hostname, 255) ||
		!agentTextOK(h.MachineID, 127) ||
		!agentTextOK(h.OSFamily, 31) ||
		!agentTextOK(h.OSName, 127) ||
		!agentTextOK(h.OSVersion, 127) ||
		!agentTextOK(h.Architecture, 31) ||
		!agentTextOK(h.AgentVersion, 63) ||
		!agentTextOK(h.IPAddress, 63) ||
		!agentTextOK(h.SerialNumber, 127) ||
		!agentTextOK(h.KernelVersion, 127) {
		return errors.New("host facts contain invalid characters or are too long")
	}
	if h.IPAddress != "" && net.ParseIP(h.IPAddress) == nil {
		return errors.New("host IP address is invalid")
	}
	if h.CPUCores < 0 || h.CPUCores > 65536 ||
		h.MemoryMB < 0 || h.MemoryMB > 16777216 {
		return errors.New("host CPU or memory facts are out of range")
	}
	if h.DisplayName == "" && h.Hostname == "" && h.MachineID == "" {
		return errors.New("host facts need a hostname, display name, or machine ID")
	}
	return nil
}

func (r *agentReportRequest) validate() error {
	if err := r.Host.validate(); err != nil {
		return err
	}
	if !agentTextOK(r.CollectorErrors, 2047) {
		return errors.New("collector_errors is invalid")
	}
	if len(r.Apps) > agentMaxApps {
		return fmt.Errorf("application count exceeds %d", agentMaxApps)
	}
	for i := range r.Apps {
		app := &r.Apps[i]
		if app.Key == "" || app.Name == "" {
			return errors.New("every application needs a key and name")
		}
		if !agentTextOK(app.Key, 255) ||
			!agentTextOK(app.Name, 511) ||
			!agentTextOK(app.Version, 255) ||
			!agentTextOK(app.Publisher, 255) ||
			!agentTextOK(app.Source, 31) ||
			!agentTextOK(app.Architecture, 31) ||
			!agentTextOK(app.InstallDate, 31) ||
			!agentTextOK(app.InstallLocation, 1023) {
			return errors.New("application fields contain invalid characters or are too long")
		}
		if app.SizeKB < 0 || app.SizeKB > 1_000_000_000_000 {
			return errors.New("application size is out of range")
		}
	}
	return nil
}

func agentHostParams(host *agentHost) map[string]any {
	return map[string]any{
		"display_name":   host.DisplayName,
		"hostname":       host.Hostname,
		"machine_id":     host.MachineID,
		"os_family":      host.OSFamily,
		"os_name":        host.OSName,
		"os_version":     host.OSVersion,
		"architecture":   host.Architecture,
		"agent_version":  host.AgentVersion,
		"ip_address":     host.IPAddress,
		"serial_number":  host.SerialNumber,
		"cpu_cores":      host.CPUCores,
		"memory_mb":      host.MemoryMB,
		"kernel_version": host.KernelVersion,
	}
}

func bearerToken(r *http.Request) string {
	header := strings.TrimSpace(r.Header.Get("Authorization"))
	const prefix = "Bearer "
	if len(header) < len(prefix) || !strings.EqualFold(header[:len(prefix)], prefix) {
		return ""
	}
	return strings.TrimSpace(header[len(prefix):])
}

func agentCredentialKey(token string) string {
	sum := sha256.Sum256([]byte(token))
	return hex.EncodeToString(sum[:8])
}

func writeAgentRPCError(w http.ResponseWriter, err error) {
	var rpcErr *rpcError
	if errors.As(err, &rpcErr) {
		switch rpcErr.Code {
		case "auth":
			writeAPIError(w, http.StatusUnauthorized, "agent_auth", "invalid agent credentials")
		case "not_found":
			writeAPIError(w, http.StatusNotFound, "not_found", "agent not found")
		case "validation", "invalid_argument":
			writeAPIError(w, http.StatusBadRequest, "validation", rpcErr.Message)
		case "limit":
			writeAPIError(w, http.StatusRequestEntityTooLarge, "limit", rpcErr.Message)
		case "conflict":
			writeAPIError(w, http.StatusConflict, "conflict", rpcErr.Message)
		default:
			writeAPIError(w, http.StatusInternalServerError, "agent_failed", "agent request failed")
		}
		return
	}
	writeAPIError(w, http.StatusInternalServerError, "agent_failed", "agent request failed")
}

func (s *server) handleAgentEnroll(w http.ResponseWriter, r *http.Request) {
	if len(s.state.Users) == 0 {
		writeAPIError(w, http.StatusServiceUnavailable, "setup_required",
			"complete server setup before enrolling agents")
		return
	}
	ip := s.clientIP(r)
	if !s.agentEnroll.allow("ip:" + ip) {
		w.Header().Set("Retry-After", "900")
		writeAPIError(w, http.StatusTooManyRequests, "rate_limited",
			"too many enrollment attempts; try again later")
		return
	}
	var request agentEnrollRequest
	r.Body = http.MaxBytesReader(w, r.Body, agentMaxEnrollBytes)
	if err := decodeJSONLimit(r, &request, agentMaxEnrollBytes); err != nil {
		s.agentEnroll.fail("ip:" + ip)
		writeAPIError(w, http.StatusBadRequest, "invalid_json", err.Error())
		return
	}
	if !validAgentToken(request.Token) {
		s.agentEnroll.fail("ip:" + ip)
		writeAPIError(w, http.StatusUnauthorized, "enrollment_token",
			"invalid or expired enrollment token")
		return
	}
	if err := request.Host.validate(); err != nil {
		s.agentEnroll.fail("ip:" + ip)
		writeAPIError(w, http.StatusBadRequest, "host", err.Error())
		return
	}
	params := agentHostParams(&request.Host)
	params["token"] = request.Token
	params["last_seen_ip"] = ip
	raw, err := s.rpc.call("agents.enroll", params, "agent-enroll")
	if err != nil {
		s.agentEnroll.fail("ip:" + ip)
		s.audit("anonymous", "agent-enroll-failed", "agent", "", `{"reason":"rejected"}`)
		writeAgentRPCError(w, err)
		return
	}
	s.agentEnroll.reset("ip:" + ip)
	var result map[string]any
	if err := json.Unmarshal(raw, &result); err != nil {
		writeAPIError(w, http.StatusInternalServerError, "agent_failed",
			"invalid enrollment response")
		return
	}
	if reused, _ := result["reused"].(bool); reused {
		log.Printf("WARNING: agent enrollment reused an existing record for machine ID %q (hostname %q); check for cloned hosts sharing /etc/machine-id",
			request.Host.MachineID, request.Host.Hostname)
	}
	result["interval_seconds"] = int(s.agentInterval.Seconds())
	writeJSON(w, http.StatusOK, result)
}

func (s *server) handleAgentReport(w http.ResponseWriter, r *http.Request) {
	if len(s.state.Users) == 0 {
		writeAPIError(w, http.StatusServiceUnavailable, "setup_required",
			"complete server setup before accepting reports")
		return
	}
	token := bearerToken(r)
	if !validAgentToken(token) {
		writeAPIError(w, http.StatusUnauthorized, "agent_auth", "invalid agent credentials")
		return
	}
	ip := s.clientIP(r)
	credential := agentCredentialKey(token)
	if !s.agentReportIP.allow("ip:"+ip) ||
		!s.agentReportToken.allow("token:"+credential) {
		w.Header().Set("Retry-After", "900")
		writeAPIError(w, http.StatusTooManyRequests, "rate_limited",
			"too many report attempts; try again later")
		return
	}
	var request agentReportRequest
	r.Body = http.MaxBytesReader(w, r.Body, agentMaxReportBytes)
	if err := decodeJSONLimit(r, &request, agentMaxReportBytes); err != nil {
		s.agentReportIP.fail("ip:" + ip)
		writeAPIError(w, http.StatusBadRequest, "invalid_json", err.Error())
		return
	}
	if err := request.validate(); err != nil {
		s.agentReportIP.fail("ip:" + ip)
		writeAPIError(w, http.StatusBadRequest, "validation", err.Error())
		return
	}
	rawAuth, err := s.rpc.call("agents.auth", map[string]any{"token": token}, "agent")
	if err != nil {
		s.agentReportIP.fail("ip:" + ip)
		s.agentReportToken.fail("token:" + credential)
		s.audit("anonymous", "agent-report-rejected", "agent", "", `{"reason":"auth"}`)
		writeAgentRPCError(w, err)
		return
	}
	var authResult struct {
		Agent agentIdentity `json:"agent"`
	}
	if err := json.Unmarshal(rawAuth, &authResult); err != nil || authResult.Agent.ID == "" {
		writeAPIError(w, http.StatusInternalServerError, "agent_failed",
			"invalid agent authentication response")
		return
	}
	params := agentHostParams(&request.Host)
	params["agent_id"] = authResult.Agent.ID
	params["collected_at"] = request.CollectedAt
	params["collector_errors"] = request.CollectorErrors
	params["last_seen_ip"] = ip
	params["apps"] = request.Apps
	params["retention_days"] = s.agentRetentionDays
	params["max_reports"] = s.agentReportsPerHost
	complete := true
	if request.Complete != nil {
		complete = *request.Complete
	}
	params["complete"] = complete
	raw, err := s.rpc.call("agents.report", params,
		"agent:"+authResult.Agent.ID)
	if err != nil {
		s.agentReportIP.fail("ip:" + ip)
		s.agentReportToken.fail("token:" + credential)
		writeAgentRPCError(w, err)
		return
	}
	s.agentReportIP.reset("ip:" + ip)
	s.agentReportToken.reset("token:" + credential)
	writeJSON(w, http.StatusOK, map[string]any{
		"ok":     true,
		"result": json.RawMessage(raw),
	})
}
