package main

import (
	"encoding/json"
	"net"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"
)

func TestClientIPUsesRightmostForwardedEntry(t *testing.T) {
	_, loopback, err := net.ParseCIDR("127.0.0.1/32")
	if err != nil {
		t.Fatal(err)
	}
	s := &server{proxies: []*net.IPNet{loopback}}

	request := httptest.NewRequest("GET", "/", nil)
	request.RemoteAddr = "127.0.0.1:54321"
	request.Header.Set("X-Forwarded-For", "203.0.113.9, 198.51.100.7")
	if got := s.clientIP(request); got != "198.51.100.7" {
		t.Fatalf("clientIP = %q, want the proxy-appended address", got)
	}

	untrusted := httptest.NewRequest("GET", "/", nil)
	untrusted.RemoteAddr = "192.0.2.10:54321"
	untrusted.Header.Set("X-Forwarded-For", "198.51.100.7")
	if got := s.clientIP(untrusted); got != "192.0.2.10" {
		t.Fatalf("untrusted clientIP = %q, want the socket peer", got)
	}
}

func TestServerRPCAllowlist(t *testing.T) {
	allowed := []string{"cis.list", "scan.start", "import.preview", "prefs.get",
		"agents.list", "agents.get", "agents.history", "agents.token_create",
		"agents.delete"}
	for _, method := range allowed {
		if !serverRPCAllowed[method] {
			t.Fatalf("%s should be allowed", method)
		}
	}
	blocked := []string{"db.open", "db.status", "app.info", "export.save",
		"auth.state.get", "settings.get", "agents.enroll", "agents.auth",
		"agents.report"}
	for _, method := range blocked {
		if serverRPCAllowed[method] {
			t.Fatalf("%s should be blocked", method)
		}
	}
}

func TestAgentAdminRPC(t *testing.T) {
	admin := []string{"agents.token_create", "agents.token_list",
		"agents.token_revoke", "agents.revoke", "agents.prune", "agents.delete"}
	for _, method := range admin {
		if !serverAdminRPC[method] {
			t.Fatalf("%s should require admin", method)
		}
	}
	for _, method := range []string{"agents.list", "agents.get", "agents.history",
		"agents.link_ci"} {
		if serverAdminRPC[method] {
			t.Fatalf("%s should not require admin", method)
		}
	}
}

func TestDecodeJSONRequiresJSONContentType(t *testing.T) {
	request := httptest.NewRequest("POST", "/", strings.NewReader(`{}`))
	request.Header.Set("Content-Type", "text/plain")
	var value map[string]any
	if err := decodeJSON(request, &value); err == nil {
		t.Fatal("non-JSON Content-Type accepted")
	}
}

func TestPendingEnrollmentSessionCanReachSecurityOnly(t *testing.T) {
	const token = "pending-enrollment-token"
	user := userRecord{ID: "user-1", Username: "admin"}
	s := &server{
		sessions: map[string]*session{
			sha256Hex(token): {
				tokenHash:       sha256Hex(token),
				userID:          user.ID,
				username:        user.Username,
				csrf:            "csrf-token",
				mfaVerified:     false,
				pending:         true,
				idleExpires:     time.Now().Add(time.Minute),
				absoluteExpires: time.Now().Add(time.Minute),
			},
		},
		state: authState{Users: []userRecord{user}},
	}

	request := httptest.NewRequest(http.MethodGet, "/api/v1/auth/session", nil)
	request.AddCookie(&http.Cookie{Name: sessionCookieName, Value: token})
	recorder := httptest.NewRecorder()
	s.handleSession(recorder, request)
	if recorder.Code != http.StatusOK {
		t.Fatalf("pending enrollment session got %d, want 200", recorder.Code)
	}
	var body map[string]any
	if err := json.Unmarshal(recorder.Body.Bytes(), &body); err != nil {
		t.Fatal(err)
	}
	if body["authenticated"] != false || body["mfa_enrollment_required"] != true {
		t.Fatalf("unexpected pending session body: %v", body)
	}

	// The same pending session must not pass the inventory gate.
	nextCalled := false
	handler := s.requireReady(func(http.ResponseWriter, *http.Request) { nextCalled = true })
	recorder = httptest.NewRecorder()
	handler(recorder, request)
	if recorder.Code != http.StatusUnauthorized || nextCalled {
		t.Fatalf("pending session reached inventory: status=%d next=%v", recorder.Code, nextCalled)
	}
}

func TestReadiness(t *testing.T) {
	user := &userRecord{MustChangePassword: true}
	if got := readiness(user); got != "password_change_required" {
		t.Fatalf("empty user readiness = %q", got)
	}
	user.MustChangePassword = false
	if got := readiness(user); got != "mfa_enrollment_required" {
		t.Fatalf("un-enrolled user readiness = %q", got)
	}
	user.TOTPConfirmed = true
	if got := readiness(user); got != "recovery_codes_required" {
		t.Fatalf("no-recovery user readiness = %q", got)
	}
	user.RecoveryCodes = []recoveryCode{{Hash: "hash"}}
	if got := readiness(user); got != "" {
		t.Fatalf("ready user readiness = %q", got)
	}
	user.RecoveryCodes[0].UsedAt = nowISO()
	if got := readiness(user); got != "recovery_codes_required" {
		t.Fatalf("used-recovery user readiness = %q", got)
	}
}
