package main

import (
	"os"
	"runtime"
	"strings"
	"testing"
)

func testConfig() *Config {
	return &Config{
		Version:         configVersion,
		ServerURL:       "https://cmdb.example.internal",
		AgentID:         "11111111-1111-4111-8111-111111111111",
		AgentToken:      strings.Repeat("ab", 32),
		IntervalSeconds: 86400,
		EnrolledAt:      "2026-11-18T00:00:00Z",
		Hostname:        "app01",
		MachineID:       "machine-1",
	}
}

func TestSaveAndLoadConfig(t *testing.T) {
	path := t.TempDir() + "/agent.json"
	cfg := testConfig()
	if err := saveConfig(path, cfg); err != nil {
		t.Fatal(err)
	}
	info, err := os.Stat(path)
	if err != nil {
		t.Fatal(err)
	}
	if runtime.GOOS != "windows" && info.Mode().Perm()&0o007 != 0 {
		t.Fatalf("config mode = %o, expected no other access", info.Mode().Perm())
	}
	loaded, err := loadConfig(path)
	if err != nil {
		t.Fatal(err)
	}
	if loaded.AgentID != cfg.AgentID || loaded.ServerURL != cfg.ServerURL ||
		loaded.AgentToken != cfg.AgentToken {
		t.Fatalf("loaded config differs: %#v", loaded)
	}
}

func TestLoadConfigRejectsInsecureMode(t *testing.T) {
	if runtime.GOOS == "windows" {
		t.Skip("Windows uses ACLs rather than POSIX mode bits")
	}
	path := t.TempDir() + "/agent.json"
	cfg := testConfig()
	if err := saveConfig(path, cfg); err != nil {
		t.Fatal(err)
	}
	if err := os.Chmod(path, 0o644); err != nil {
		t.Fatal(err)
	}
	if _, err := loadConfig(path); err == nil {
		t.Fatal("world-readable config accepted")
	}
}

func TestLoadConfigRejectsSymlink(t *testing.T) {
	if runtime.GOOS == "windows" {
		t.Skip("symlink creation needs privileges on Windows")
	}
	dir := t.TempDir()
	target := dir + "/real.json"
	link := dir + "/agent.json"
	if err := saveConfig(target, testConfig()); err != nil {
		t.Fatal(err)
	}
	if err := os.Symlink(target, link); err != nil {
		t.Fatal(err)
	}
	if _, err := loadConfig(link); err == nil {
		t.Fatal("symlinked config accepted")
	}
}

func TestConfigRejectsInvalidValues(t *testing.T) {
	cfg := testConfig()
	cfg.ServerURL = "http://remote.example.internal"
	if err := cfg.validate(); err == nil {
		t.Fatal("insecure server URL accepted")
	}
	cfg = testConfig()
	cfg.AgentToken = "short"
	if err := cfg.validate(); err == nil {
		t.Fatal("short token accepted")
	}
	cfg = testConfig()
	cfg.IntervalSeconds = 10
	if err := cfg.validate(); err == nil {
		t.Fatal("short interval accepted")
	}
}
