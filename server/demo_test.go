//go:build demo

package main

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func writeDemoCredentials(t *testing.T) {
	t.Helper()
	path := filepath.Join(t.TempDir(), "demo-credentials.json")
	payload, err := json.Marshal(demoCredentials{
		Username:      "guest",
		Password:      "guest",
		TOTPSecret:    "JBSWY3DPEHPK3PXPJBSWY3DPEHPK3PXP",
		RecoveryCodes: []string{"DEMO2AAAAAA2"},
	})
	if err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(path, payload, 0600); err != nil {
		t.Fatal(err)
	}
	t.Setenv("CMDB_DEMO_CREDENTIALS", path)
}

func TestDemoLoginPageInjectsBanner(t *testing.T) {
	writeDemoCredentials(t)
	page := demoLoginPage([]byte("<main><!-- DEMO_BANNER --></main>"))
	html := string(page)
	if !strings.Contains(html, "Public demo") || !strings.Contains(html, "demo-panel") {
		t.Fatalf("demo banner was not injected: %s", html)
	}
	if strings.Contains(html, "<!-- DEMO_BANNER -->") {
		t.Fatal("demo banner marker was left in the page")
	}
}

func TestDemoTOTPReplayAndProtectedAccount(t *testing.T) {
	writeDemoCredentials(t)
	if !demoAllowTOTPReplay("guest") {
		t.Fatal("guest TOTP replay should be allowed in the demo build")
	}
	if demoAllowTOTPReplay("alice") {
		t.Fatal("non-demo accounts must keep replay protection")
	}

	recorder := httptest.NewRecorder()
	if !demoBlockCredentialChange(recorder, &userRecord{Username: "guest"}) {
		t.Fatal("guest credential change should be blocked")
	}
	if recorder.Code != http.StatusForbidden {
		t.Fatalf("expected 403, got %d", recorder.Code)
	}
	var body struct {
		Error struct {
			Code string `json:"code"`
		} `json:"error"`
	}
	if err := json.Unmarshal(recorder.Body.Bytes(), &body); err != nil {
		t.Fatal(err)
	}
	if body.Error.Code != "demo_locked" {
		t.Fatalf("expected demo_locked, got %q", body.Error.Code)
	}

	recorder = httptest.NewRecorder()
	if demoBlockCredentialChange(recorder, &userRecord{Username: "alice"}) {
		t.Fatal("non-demo accounts must remain editable")
	}
	if recorder.Code != http.StatusOK {
		t.Fatalf("unexpected response for alice: %d", recorder.Code)
	}
}

func TestDemoStatusReturnsCurrentCode(t *testing.T) {
	writeDemoCredentials(t)
	secret := "JBSWY3DPEHPK3PXPJBSWY3DPEHPK3PXP"
	s := &server{state: authState{Version: 1, Users: []userRecord{{
		ID: "11111111-1111-4111-8111-111111111111", Username: "guest",
		IsAdmin: true, TOTPSecret: secret, TOTPConfirmed: true,
	}}}}
	request := httptest.NewRequest(http.MethodGet, "/api/v1/demo/status", nil)
	recorder := httptest.NewRecorder()
	s.handleDemoStatus(recorder, request)
	if recorder.Code != http.StatusOK {
		t.Fatalf("expected 200, got %d", recorder.Code)
	}
	var body struct {
		Username   string   `json:"username"`
		Password   string   `json:"password"`
		TOTPCode   string   `json:"totp_code"`
		TOTPSecret string   `json:"totp_secret"`
		Codes      []string `json:"recovery_codes"`
	}
	if err := json.Unmarshal(recorder.Body.Bytes(), &body); err != nil {
		t.Fatal(err)
	}
	if body.Username != "guest" || body.Password != "guest" {
		t.Fatalf("unexpected credentials: %+v", body)
	}
	if len(body.TOTPCode) != totpDigits {
		t.Fatalf("unexpected TOTP code %q", body.TOTPCode)
	}
	if body.TOTPSecret != secret {
		t.Fatalf("expected the account secret, got %q", body.TOTPSecret)
	}
	if len(body.Codes) != 1 || body.Codes[0] != "DEMO2AAAAAA2" {
		t.Fatalf("unexpected recovery codes: %+v", body.Codes)
	}
	if step, ok := validateTOTP(secret, body.TOTPCode, time.Now()); !ok || step == 0 {
		t.Fatalf("returned TOTP code does not validate: %q", body.TOTPCode)
	}
}
