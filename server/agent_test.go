package main

import (
	"net/http/httptest"
	"strings"
	"testing"
)

func validAgentHost() agentHost {
	return agentHost{
		DisplayName:  "app01",
		Hostname:     "app01",
		MachineID:    "machine-1",
		OSFamily:     "linux",
		OSName:       "Ubuntu",
		OSVersion:    "24.04",
		Architecture: "x86_64",
		AgentVersion: "0.7.0",
	}
}

func TestAgentHostValidation(t *testing.T) {
	host := validAgentHost()
	if err := host.validate(); err != nil {
		t.Fatalf("valid host rejected: %v", err)
	}
	host.Hostname = "bad\nhost"
	if err := host.validate(); err == nil {
		t.Fatal("control character accepted")
	}
	host = agentHost{}
	if err := host.validate(); err == nil {
		t.Fatal("empty host accepted")
	}
	if agentTextOK("a\u202eb", 10) {
		t.Fatal("Unicode format character accepted")
	}
}

func TestAgentReportValidation(t *testing.T) {
	report := agentReportRequest{
		Host: validAgentHost(),
		Apps: []agentApp{
			{Key: "deb:bash:amd64", Name: "bash", Version: "5.2", SizeKB: 10},
		},
	}
	if err := report.validate(); err != nil {
		t.Fatalf("valid report rejected: %v", err)
	}
	report.Apps[0].Key = ""
	if err := report.validate(); err == nil {
		t.Fatal("missing app key accepted")
	}
	report.Apps[0].Key = "deb:bash:amd64"
	report.Apps[0].Name = strings.Repeat("x", 512)
	if err := report.validate(); err == nil {
		t.Fatal("oversized app name accepted")
	}
	report.Apps = make([]agentApp, agentMaxApps+1)
	if err := report.validate(); err == nil {
		t.Fatal("oversized application list accepted")
	}
}

func TestAgentTokenFormat(t *testing.T) {
	if !validAgentToken(strings.Repeat("ab", 32)) {
		t.Fatal("valid token rejected")
	}
	if validAgentToken("abc") {
		t.Fatal("short token accepted")
	}
	if validAgentToken(strings.Repeat("zz", 32)) {
		t.Fatal("non-hex token accepted")
	}
}

func TestAgentCredentialKeyDoesNotExposeToken(t *testing.T) {
	token := strings.Repeat("ab", 32)
	key := agentCredentialKey(token)
	if key == "" || strings.Contains(key, token) || len(key) != 16 {
		t.Fatalf("credential key = %q", key)
	}
}

func TestBearerToken(t *testing.T) {
	token := strings.Repeat("ab", 32)
	for _, header := range []string{"Bearer " + token, "bearer " + token} {
		request := httptest.NewRequest("POST", "/api/v1/agent/report", nil)
		request.Header.Set("Authorization", header)
		if got := bearerToken(request); got != token {
			t.Fatalf("bearerToken(%q) = %q", header, got)
		}
	}
	for _, header := range []string{"", "Basic abc", "Bearer"} {
		request := httptest.NewRequest("POST", "/api/v1/agent/report", nil)
		if header != "" {
			request.Header.Set("Authorization", header)
		}
		if got := bearerToken(request); got != "" {
			t.Fatalf("bearerToken(%q) = %q, want empty", header, got)
		}
	}
	request := httptest.NewRequest("POST", "/api/v1/agent/report", nil)
	request.Header.Set("Authorization", "Bearer short")
	if got := bearerToken(request); got != "short" {
		t.Fatalf("bearerToken(short) = %q", got)
	}
	if validAgentToken("short") {
		t.Fatal("short bearer token accepted by format validation")
	}
}
