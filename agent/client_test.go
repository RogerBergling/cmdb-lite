package main

import (
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"
)

func TestEnrollAndReport(t *testing.T) {
	agentToken := strings.Repeat("cd", 32)
	var sawEnroll, sawReport bool
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		switch r.URL.Path {
		case "/api/v1/agent/enroll":
			sawEnroll = true
			if r.Method != http.MethodPost {
				t.Errorf("enroll method = %s", r.Method)
			}
			var body struct {
				Token string    `json:"token"`
				Host  HostFacts `json:"host"`
			}
			if err := json.NewDecoder(r.Body).Decode(&body); err != nil {
				t.Errorf("decode enroll: %v", err)
			}
			if body.Token != strings.Repeat("ab", 32) || body.Host.Hostname != "app01" {
				t.Errorf("unexpected enroll body: %#v", body)
			}
			_ = json.NewEncoder(w).Encode(map[string]any{
				"agent": map[string]any{
					"id":           "11111111-1111-4111-8111-111111111111",
					"display_name": "app01",
					"hostname":     "app01",
					"status":       "active",
				},
				"token":            agentToken,
				"reused":           false,
				"interval_seconds": 3600,
			})
		case "/api/v1/agent/report":
			sawReport = true
			if got := r.Header.Get("Authorization"); got != "Bearer "+agentToken {
				t.Errorf("authorization = %q", got)
			}
			var body struct {
				Host     HostFacts     `json:"host"`
				Apps     []Application `json:"apps"`
				Error    string        `json:"collector_errors"`
				Complete bool          `json:"complete"`
			}
			if err := json.NewDecoder(r.Body).Decode(&body); err != nil {
				t.Errorf("decode report: %v", err)
			}
			if len(body.Apps) != 1 || body.Apps[0].Name != "bash" {
				t.Errorf("unexpected apps: %#v", body.Apps)
			}
			if !body.Complete {
				t.Errorf("complete flag was not sent")
			}
			_ = json.NewEncoder(w).Encode(map[string]any{
				"ok": true,
				"result": map[string]any{
					"report": map[string]any{
						"id": "22222222-2222-4222-8222-222222222222",
					},
				},
			})
		default:
			http.NotFound(w, r)
		}
	}))
	defer server.Close()

	client, err := newAgentClient(server.URL, "", "test")
	if err != nil {
		t.Fatal(err)
	}
	host := HostFacts{
		DisplayName:  "app01",
		Hostname:     "app01",
		MachineID:    "machine-1",
		OSFamily:     "linux",
		OSName:       "Ubuntu",
		OSVersion:    "24.04",
		Architecture: "x86_64",
		AgentVersion: "test",
	}
	enrolled, err := client.enroll(context.Background(), strings.Repeat("ab", 32), host)
	if err != nil {
		t.Fatal(err)
	}
	if enrolled.Token != agentToken || enrolled.Agent.ID == "" {
		t.Fatalf("unexpected enrollment: %#v", enrolled)
	}
	collection := Collection{
		CollectedAt: time.Now(),
		Host:        host,
		Apps: []Application{
			{Key: "deb:bash:amd64", Name: "bash", Source: "deb", SizeKB: 1},
		},
		Complete: true,
	}
	if _, err := client.report(context.Background(), enrolled.Token, collection); err != nil {
		t.Fatal(err)
	}
	if !sawEnroll || !sawReport {
		t.Fatalf("handlers: enroll=%v report=%v", sawEnroll, sawReport)
	}
}

func TestReportUnauthorized(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(http.StatusUnauthorized)
		_ = json.NewEncoder(w).Encode(map[string]any{
			"ok": false,
			"error": map[string]any{
				"code":    "agent_auth",
				"message": "invalid agent credentials",
			},
		})
	}))
	defer server.Close()
	client, err := newAgentClient(server.URL, "", "test")
	if err != nil {
		t.Fatal(err)
	}
	_, err = client.report(context.Background(), strings.Repeat("cd", 32), Collection{
		CollectedAt: time.Now(),
		Host: HostFacts{
			Hostname: "app01",
		},
	})
	if err == nil || !isUnauthorized(err) {
		t.Fatalf("expected unauthorized error, got %v", err)
	}
}
