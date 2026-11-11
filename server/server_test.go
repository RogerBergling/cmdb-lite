package main

import (
	"net"
	"net/http/httptest"
	"strings"
	"testing"
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
	allowed := []string{"cis.list", "scan.start", "import.preview", "prefs.get"}
	for _, method := range allowed {
		if !serverRPCAllowed[method] {
			t.Fatalf("%s should be allowed", method)
		}
	}
	blocked := []string{"db.open", "db.status", "app.info", "export.save", "auth.state.get", "settings.get"}
	for _, method := range blocked {
		if serverRPCAllowed[method] {
			t.Fatalf("%s should be blocked", method)
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
