package main

import (
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"runtime"
	"strings"
)

const configVersion = 1

type Config struct {
	Version         int    `json:"version"`
	ServerURL       string `json:"server_url"`
	AgentID         string `json:"agent_id"`
	AgentToken      string `json:"agent_token"`
	IntervalSeconds int    `json:"interval_seconds"`
	CAFile          string `json:"ca_file,omitempty"`
	EnrolledAt      string `json:"enrolled_at"`
	Hostname        string `json:"hostname,omitempty"`
	MachineID       string `json:"machine_id,omitempty"`
}

func defaultConfigPath() string {
	if runtime.GOOS == "windows" {
		programData := os.Getenv("ProgramData")
		if programData == "" {
			programData = `C:\ProgramData`
		}
		return filepath.Join(programData, "cmdb-lite", "agent.json")
	}
	return "/etc/cmdb-lite-agent/agent.json"
}

func (c *Config) validate() error {
	if c.Version != configVersion {
		return fmt.Errorf("unsupported agent configuration version %d", c.Version)
	}
	if _, err := normalizeServerURL(c.ServerURL); err != nil {
		return err
	}
	if c.AgentID == "" || !validTokenFormat(c.AgentToken) {
		return errors.New("agent configuration has no valid identity")
	}
	if c.IntervalSeconds < 900 || c.IntervalSeconds > 7*24*60*60 {
		return errors.New("agent interval must be between 15 minutes and 7 days")
	}
	if c.CAFile != "" {
		if !filepath.IsAbs(c.CAFile) {
			return errors.New("CA file path must be absolute")
		}
		if _, err := os.Stat(c.CAFile); err != nil {
			return fmt.Errorf("CA file: %w", err)
		}
	}
	return nil
}

func loadConfig(path string) (*Config, error) {
	info, err := os.Lstat(path)
	if err != nil {
		return nil, fmt.Errorf("read agent configuration: %w", err)
	}
	if info.Mode()&os.ModeSymlink != 0 {
		return nil, errors.New("agent configuration must not be a symbolic link")
	}
	if !info.Mode().IsRegular() {
		return nil, errors.New("agent configuration is not a regular file")
	}
	if info.Size() > 64<<10 {
		return nil, errors.New("agent configuration is too large")
	}
	if runtime.GOOS != "windows" {
		if info.Mode().Perm()&0o007 != 0 {
			return nil, errors.New("agent configuration is accessible to other users; expected mode 0600 or 0640")
		}
	}
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("read agent configuration: %w", err)
	}
	var cfg Config
	if err := json.Unmarshal(data, &cfg); err != nil {
		return nil, fmt.Errorf("parse agent configuration: %w", err)
	}
	if err := cfg.validate(); err != nil {
		return nil, err
	}
	return &cfg, nil
}

func saveConfig(path string, cfg *Config) error {
	if err := cfg.validate(); err != nil {
		return err
	}
	dir := filepath.Dir(path)
	useServiceGroup := runtime.GOOS != "windows" &&
		filepath.Clean(path) == filepath.Clean(defaultConfigPath())
	if err := os.MkdirAll(dir, 0o750); err != nil {
		return fmt.Errorf("create configuration directory: %w", err)
	}
	if runtime.GOOS != "windows" {
		if err := secureConfigDirectory(dir, useServiceGroup); err != nil {
			return err
		}
	}
	payload, err := json.MarshalIndent(cfg, "", "  ")
	if err != nil {
		return err
	}
	payload = append(payload, '\n')
	temp, err := os.CreateTemp(dir, ".agent-*.tmp")
	if err != nil {
		return fmt.Errorf("create temporary configuration: %w", err)
	}
	tempName := temp.Name()
	cleanup := func() {
		_ = temp.Close()
		_ = os.Remove(tempName)
		_ = os.Remove(path + ".tmp")
	}
	if err := temp.Chmod(0o600); err != nil {
		cleanup()
		return err
	}
	if _, err := temp.Write(payload); err != nil {
		cleanup()
		return err
	}
	if err := temp.Sync(); err != nil {
		cleanup()
		return err
	}
	if err := temp.Close(); err != nil {
		_ = os.Remove(tempName)
		return err
	}
	if runtime.GOOS != "windows" {
		if err := secureConfigFile(tempName, useServiceGroup); err != nil {
			_ = os.Remove(tempName)
			return err
		}
	}
	if err := os.Rename(tempName, path); err != nil {
		_ = os.Remove(tempName)
		return fmt.Errorf("install configuration: %w", err)
	}
	if runtime.GOOS == "windows" {
		if err := secureConfigFile(path, false); err != nil {
			return err
		}
	}
	return nil
}

func normalizeServerURL(raw string) (string, error) {
	value := strings.TrimSpace(raw)
	if value == "" {
		return "", errors.New("server URL is required")
	}
	parsed, err := parseURL(value)
	if err != nil {
		return "", err
	}
	if parsed.Scheme != "https" {
		if parsed.Scheme != "http" || !isLoopbackHost(parsed.Hostname()) {
			return "", errors.New("server URL must use HTTPS (plain HTTP is allowed only for loopback testing)")
		}
	}
	if parsed.User != nil {
		return "", errors.New("server URL must not contain credentials")
	}
	if parsed.RawQuery != "" || parsed.Fragment != "" {
		return "", errors.New("server URL must not contain a query or fragment")
	}
	parsed.Path = strings.TrimRight(parsed.Path, "/")
	return parsed.String(), nil
}
