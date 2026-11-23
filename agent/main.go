package main

import (
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"math/rand"
	"os"
	"os/signal"
	"strings"
	"syscall"
	"time"
)

var (
	version     = "0.0.0"
	buildNumber = "dev"
	buildCommit = "unknown"
	buildDate   = "unknown"
	buildTarget = "unknown"
	buildType   = "unknown"
)

const serviceName = "cmdb-agent"

func main() {
	log.SetFlags(log.LstdFlags | log.LUTC)
	log.SetPrefix("cmdb-agent: ")
	if len(os.Args) < 2 {
		usage()
		os.Exit(2)
	}
	var err error
	switch os.Args[1] {
	case "enroll":
		err = cmdEnroll(os.Args[2:])
	case "run":
		err = cmdRun(os.Args[2:])
	case "once":
		err = cmdOnce(os.Args[2:])
	case "collect":
		err = cmdCollect(os.Args[2:])
	case "status":
		err = cmdStatus(os.Args[2:])
	case "install-service":
		err = cmdInstallService(os.Args[2:])
	case "uninstall-service":
		err = cmdUninstallService(os.Args[2:])
	case "start-service":
		err = cmdStartService(os.Args[2:])
	case "stop-service":
		err = cmdStopService(os.Args[2:])
	case "remove-config":
		err = cmdRemoveConfig(os.Args[2:])
	case "version", "--version", "-version":
		printVersion()
	case "help", "--help", "-h":
		usage()
	default:
		fmt.Fprintf(os.Stderr, "unknown command %q\n\n", os.Args[1])
		usage()
		os.Exit(2)
	}
	if err != nil {
		log.Print(err)
		os.Exit(1)
	}
}

func usage() {
	fmt.Fprint(os.Stderr, `cmdb-agent - report installed application inventory to cmdb-lite

Usage:
  cmdb-agent enroll --server URL --token TOKEN [options]
      Exchange a one-time enrollment token for a per-host token, write the
      protected configuration, and start the service.

  cmdb-agent run [--config PATH]
      Run the reporting service loop (used by systemd/the Windows service).

  cmdb-agent once [--config PATH]
      Collect and report once, then exit.

  cmdb-agent collect [--json]
      Print a local collection summary (--json includes the full inventory).

  cmdb-agent status [--config PATH]
      Show the enrolled server and host identity without printing secrets.

  cmdb-agent install-service [--config PATH]
  cmdb-agent uninstall-service [--purge]
  cmdb-agent start-service
  cmdb-agent stop-service
  cmdb-agent remove-config [--config PATH]
      Windows service management. Linux uses the packaged systemd unit.

  cmdb-agent version
      Print version and build information.

Enrollment options:
  --server URL        cmdb-server base URL (HTTPS required; HTTP is loopback-only)
  --token TOKEN       one-time enrollment token created by a CMDB administrator
  --config PATH       configuration path (default: system-specific)
  --ca-file PATH      additional CA bundle for a private TLS certificate
  --interval DURATION reporting interval (default: value from the server)
  --no-start          write the configuration but do not start the service
`)
}

func printVersion() {
	fmt.Printf("cmdb-agent %s\n", version)
	fmt.Printf("build:  %s\n", buildNumber)
	fmt.Printf("commit: %s\n", buildCommit)
	fmt.Printf("date:   %s\n", buildDate)
	fmt.Printf("target: %s\n", buildTarget)
	fmt.Printf("type:   %s\n", buildType)
}

func cmdEnroll(args []string) error {
	flags := flag.NewFlagSet("enroll", flag.ContinueOnError)
	server := flags.String("server", "", "cmdb-server base URL")
	token := flags.String("token", "", "one-time enrollment token")
	configPath := flags.String("config", defaultConfigPath(), "configuration path")
	caFile := flags.String("ca-file", "", "additional CA bundle")
	interval := flags.Duration("interval", 0, "reporting interval")
	noStart := flags.Bool("no-start", false, "write configuration without starting the service")
	if err := flags.Parse(args); err != nil {
		return err
	}
	if *server == "" || *token == "" {
		return fmt.Errorf("--server and --token are required")
	}
	if !validTokenFormat(*token) {
		return fmt.Errorf("enrollment token must be 64 hexadecimal characters")
	}
	normalizedServer, err := normalizeServerURL(*server)
	if err != nil {
		return err
	}
	if *interval != 0 && (*interval < 15*time.Minute || *interval > 7*24*time.Hour) {
		return fmt.Errorf("--interval must be between 15m and 168h")
	}
	host, err := collectHostFacts()
	if err != nil {
		return err
	}
	client, err := newAgentClient(normalizedServer, *caFile, version)
	if err != nil {
		return err
	}
	ctx, cancel := context.WithTimeout(context.Background(), 60*time.Second)
	response, err := client.enroll(ctx, *token, host)
	cancel()
	if err != nil {
		return fmt.Errorf("enrollment failed: %w", err)
	}
	intervalSeconds := response.IntervalSeconds
	if *interval != 0 {
		intervalSeconds = int(interval.Seconds())
	}
	if intervalSeconds < 900 {
		intervalSeconds = 24 * 60 * 60
	}
	cfg := &Config{
		Version:         configVersion,
		ServerURL:       normalizedServer,
		AgentID:         response.Agent.ID,
		AgentToken:      response.Token,
		IntervalSeconds: intervalSeconds,
		CAFile:          *caFile,
		EnrolledAt:      time.Now().UTC().Format(time.RFC3339),
		Hostname:        host.Hostname,
		MachineID:       host.MachineID,
	}
	if response.Reused {
		log.Printf("WARNING: machine ID %q is already enrolled; the existing agent record was updated. If this is a cloned host, generate a unique machine ID (systemd-machine-id-setup) and re-enroll with a new token.", host.MachineID)
	}
	if err := saveConfig(*configPath, cfg); err != nil {
		return err
	}
	log.Printf("enrolled host %q as agent %s", cfg.Hostname, cfg.AgentID)
	log.Printf("configuration written to %s", *configPath)
	if *noStart {
		return nil
	}
	if err := startService(*configPath); err != nil {
		return fmt.Errorf("configuration saved, but the service could not be started: %w", err)
	}
	log.Printf("service %s started", serviceName)
	return nil
}

func cmdRun(args []string) error {
	flags := flag.NewFlagSet("run", flag.ContinueOnError)
	configPath := flags.String("config", defaultConfigPath(), "configuration path")
	if err := flags.Parse(args); err != nil {
		return err
	}
	if running, err := isWindowsService(); err != nil {
		return err
	} else if running {
		return runWindowsService(*configPath)
	}
	cfg, err := loadConfig(*configPath)
	if err != nil {
		return err
	}
	client, err := newAgentClient(cfg.ServerURL, cfg.CAFile, version)
	if err != nil {
		return err
	}
	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()
	return runLoop(ctx, cfg, client)
}

func runLoop(ctx context.Context, cfg *Config, client *agentClient) error {
	interval := time.Duration(cfg.IntervalSeconds) * time.Second
	if interval < 15*time.Minute {
		interval = 24 * time.Hour
	}
	attempt := 0
	for {
		if ctx.Err() != nil {
			return nil
		}
		collection := collectNow(time.Now())
		reportCtx, cancel := context.WithTimeout(ctx, 2*time.Minute)
		response, err := client.report(reportCtx, cfg.AgentToken, collection)
		cancel()
		if ctx.Err() != nil {
			return nil
		}
		var wait time.Duration
		if err != nil {
			attempt++
			wait = retryDelay(attempt)
			log.Printf("report failed (%s): %v; retrying in %s", cfg.Hostname, err, wait.Round(time.Second))
		} else {
			attempt = 0
			wait = jitter(interval)
			completeness := "complete"
			if !collection.Complete {
				completeness = "partial; applications not present in this report are retained"
			}
			log.Printf("reported %d applications (%d added, %d removed, %d updated; %s); next report in %s",
				len(collection.Apps), response.Result.Report.AddedCount,
				response.Result.Report.RemovedCount, response.Result.Report.UpdatedCount,
				completeness, wait.Round(time.Second))
		}
		timer := time.NewTimer(wait)
		select {
		case <-ctx.Done():
			timer.Stop()
			return nil
		case <-timer.C:
		}
	}
}

func retryDelay(attempt int) time.Duration {
	if attempt < 1 {
		attempt = 1
	}
	delay := 5 * time.Minute
	for i := 1; i < attempt && delay < 60*time.Minute; i++ {
		delay *= 2
	}
	if delay > 60*time.Minute {
		delay = 60 * time.Minute
	}
	return jitter(delay)
}

func jitter(value time.Duration) time.Duration {
	factor := 0.9 + rand.Float64()*0.2
	return time.Duration(float64(value) * factor)
}

func cmdOnce(args []string) error {
	flags := flag.NewFlagSet("once", flag.ContinueOnError)
	configPath := flags.String("config", defaultConfigPath(), "configuration path")
	if err := flags.Parse(args); err != nil {
		return err
	}
	cfg, err := loadConfig(*configPath)
	if err != nil {
		return err
	}
	client, err := newAgentClient(cfg.ServerURL, cfg.CAFile, version)
	if err != nil {
		return err
	}
	collection := collectNow(time.Now())
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Minute)
	defer cancel()
	response, err := client.report(ctx, cfg.AgentToken, collection)
	if err != nil {
		return fmt.Errorf("report failed: %w", err)
	}
	fmt.Printf("reported %d applications: %d added, %d removed, %d updated\n",
		len(collection.Apps), response.Result.Report.AddedCount,
		response.Result.Report.RemovedCount, response.Result.Report.UpdatedCount)
	if len(collection.Errors) > 0 {
		fmt.Printf("collector warnings: %s\n", strings.Join(collection.Errors, "; "))
	}
	return nil
}

func cmdCollect(args []string) error {
	flags := flag.NewFlagSet("collect", flag.ContinueOnError)
	asJSON := flags.Bool("json", false, "print the complete inventory as JSON")
	if err := flags.Parse(args); err != nil {
		return err
	}
	collection := collectNow(time.Now())
	if *asJSON {
		encoder := json.NewEncoder(os.Stdout)
		encoder.SetIndent("", "  ")
		return encoder.Encode(map[string]any{
			"collected_at":     collection.CollectedAt.UTC().Format(time.RFC3339),
			"host":             collection.Host,
			"applications":     collection.Apps,
			"collector_errors": collection.Errors,
			"complete":         collection.Complete,
		})
	}
	fmt.Printf("host: %s (%s %s, %s)\n", collection.Host.Hostname,
		collection.Host.OSName, collection.Host.OSVersion,
		collection.Host.Architecture)
	fmt.Printf("applications: %d\n", len(collection.Apps))
	fmt.Printf("complete: %s\n", map[bool]string{true: "yes", false: "no (partial)"}[collection.Complete])
	for _, warning := range collection.Errors {
		fmt.Printf("warning: %s\n", warning)
	}
	return nil
}

func cmdStatus(args []string) error {
	flags := flag.NewFlagSet("status", flag.ContinueOnError)
	configPath := flags.String("config", defaultConfigPath(), "configuration path")
	if err := flags.Parse(args); err != nil {
		return err
	}
	cfg, err := loadConfig(*configPath)
	if err != nil {
		return err
	}
	fmt.Printf("agent id:   %s\n", cfg.AgentID)
	fmt.Printf("server:     %s\n", cfg.ServerURL)
	fmt.Printf("host:       %s\n", cfg.Hostname)
	fmt.Printf("interval:   %s\n", (time.Duration(cfg.IntervalSeconds) * time.Second).String())
	fmt.Printf("enrolled:   %s\n", cfg.EnrolledAt)
	fmt.Printf("config:     %s\n", *configPath)
	return nil
}

func cmdInstallService(args []string) error {
	flags := flag.NewFlagSet("install-service", flag.ContinueOnError)
	configPath := flags.String("config", defaultConfigPath(), "configuration path")
	if err := flags.Parse(args); err != nil {
		return err
	}
	if _, err := loadConfig(*configPath); err != nil {
		return fmt.Errorf("load configuration before installing the service: %w", err)
	}
	if err := installService(*configPath); err != nil {
		return err
	}
	log.Printf("service %s installed", serviceName)
	return nil
}

func cmdUninstallService(args []string) error {
	flags := flag.NewFlagSet("uninstall-service", flag.ContinueOnError)
	purge := flags.Bool("purge", false, "remove the agent configuration after removing the service")
	if err := flags.Parse(args); err != nil {
		return err
	}
	if err := uninstallService(); err != nil {
		return err
	}
	if *purge {
		if err := os.Remove(defaultConfigPath()); err != nil && !os.IsNotExist(err) {
			return fmt.Errorf("remove configuration: %w", err)
		}
	}
	log.Printf("service %s removed", serviceName)
	return nil
}

func cmdStartService(args []string) error {
	flags := flag.NewFlagSet("start-service", flag.ContinueOnError)
	if err := flags.Parse(args); err != nil {
		return err
	}
	if err := startInstalledService(); err != nil {
		return err
	}
	log.Printf("service %s started", serviceName)
	return nil
}

func cmdStopService(args []string) error {
	flags := flag.NewFlagSet("stop-service", flag.ContinueOnError)
	if err := flags.Parse(args); err != nil {
		return err
	}
	if err := stopInstalledService(); err != nil {
		return err
	}
	log.Printf("service %s stopped", serviceName)
	return nil
}

func cmdRemoveConfig(args []string) error {
	flags := flag.NewFlagSet("remove-config", flag.ContinueOnError)
	configPath := flags.String("config", defaultConfigPath(), "configuration path")
	if err := flags.Parse(args); err != nil {
		return err
	}
	if err := os.Remove(*configPath); err != nil && !os.IsNotExist(err) {
		return fmt.Errorf("remove configuration: %w", err)
	}
	log.Printf("configuration removed from %s", *configPath)
	return nil
}
