package main

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"github.com/go-webauthn/webauthn/webauthn"
)

func TestPasskeyServiceConfig(t *testing.T) {
	valid := []struct {
		rpID   string
		origin string
	}{
		{"example.com", "https://cmdb.example.com"},
		{"example.com", "https://example.com"},
		{"localhost", "http://localhost:8080"},
	}
	for _, test := range valid {
		if _, err := newPasskeyService(test.rpID, test.origin); err != nil {
			t.Fatalf("newPasskeyService(%q, %q) = %v", test.rpID, test.origin, err)
		}
	}
	invalid := []struct {
		rpID   string
		origin string
	}{
		{"", "https://cmdb.example.com"},
		{"example.com", ""},
		{"example.com", "http://cmdb.example.com"},
		{"example.com", "https://cmdb.example.com/login"},
		{"example.com", "https://other.example.net"},
		{"example.com", "https://cmdb.example.com?x=1"},
	}
	for _, test := range invalid {
		if _, err := newPasskeyService(test.rpID, test.origin); err == nil {
			t.Fatalf("newPasskeyService(%q, %q) unexpectedly succeeded", test.rpID, test.origin)
		}
	}
}

func TestValidatePasskeyName(t *testing.T) {
	if name, err := validatePasskeyName("  Laptop  "); err != nil || name != "Laptop" {
		t.Fatalf("name = %q, err = %v", name, err)
	}
	if name, err := validatePasskeyName(""); err != nil || name != "Passkey" {
		t.Fatalf("empty name = %q, err = %v", name, err)
	}
	if _, err := validatePasskeyName(strings.Repeat("x", passkeyNameMax+1)); err == nil {
		t.Fatal("oversized passkey name accepted")
	}
	if _, err := validatePasskeyName("bad\nname"); err == nil {
		t.Fatal("control character accepted")
	}
}

func TestPasskeyLoginAvailable(t *testing.T) {
	service, err := newPasskeyService("example.com", "https://cmdb.example.com")
	if err != nil {
		t.Fatal(err)
	}
	s := &server{passkeys: service}
	user := &userRecord{ID: "user-1", TOTPConfirmed: true}
	if s.passkeyLoginAvailable(user) {
		t.Fatal("passkey offered without a registered credential")
	}
	user.Passkeys = []passkeyRecord{{Name: "Laptop"}}
	if !s.passkeyLoginAvailable(user) {
		t.Fatal("passkey not offered for enrolled user")
	}
	user.TOTPConfirmed = false
	if s.passkeyLoginAvailable(user) {
		t.Fatal("passkey offered before TOTP enrollment")
	}
}

func TestPasskeyStatusAndLoginRoute(t *testing.T) {
	service, err := newPasskeyService("example.com", "https://cmdb.example.com")
	if err != nil {
		t.Fatal(err)
	}
	s := &server{
		passkeys: service,
		sessions: make(map[string]*session),
	}
	handler := s.handler()

	statusRequest := httptest.NewRequest("GET", "/api/v1/auth/status", nil)
	statusResponse := httptest.NewRecorder()
	handler.ServeHTTP(statusResponse, statusRequest)
	if statusResponse.Code != http.StatusOK {
		t.Fatalf("status code = %d", statusResponse.Code)
	}
	var status map[string]any
	if err := json.Unmarshal(statusResponse.Body.Bytes(), &status); err != nil {
		t.Fatal(err)
	}
	if status["passkeys_enabled"] != true {
		t.Fatalf("passkeys_enabled = %#v", status["passkeys_enabled"])
	}

	loginRequest := httptest.NewRequest("POST", "/api/v1/auth/passkey/login/begin",
		strings.NewReader(`{}`))
	loginRequest.Header.Set("Content-Type", "application/json")
	loginResponse := httptest.NewRecorder()
	handler.ServeHTTP(loginResponse, loginRequest)
	if loginResponse.Code != http.StatusUnauthorized {
		t.Fatalf("login begin without pending session = %d", loginResponse.Code)
	}
}

func TestPasskeyLoginBeginReturnsOptions(t *testing.T) {
	service, err := newPasskeyService("example.com", "https://cmdb.example.com")
	if err != nil {
		t.Fatal(err)
	}
	user := userRecord{
		ID:            "user-1",
		Username:      "alice",
		TOTPConfirmed: true,
		Passkeys: []passkeyRecord{{
			Credential: webauthn.Credential{ID: []byte("credential-id")},
			Name:       "Laptop",
		}},
	}
	now := time.Now()
	s := &server{
		passkeys: service,
		state:    authState{Version: 1, Users: []userRecord{user}},
		sessions: map[string]*session{
			sha256Hex("session-token"): {
				userID:          user.ID,
				username:        user.Username,
				pending:         true,
				idleExpires:     now.Add(time.Minute),
				absoluteExpires: now.Add(time.Minute),
			},
		},
	}
	request := httptest.NewRequest("POST", "/api/v1/auth/passkey/login/begin",
		strings.NewReader(`{}`))
	request.Header.Set("Content-Type", "application/json")
	request.AddCookie(&http.Cookie{Name: sessionCookieName, Value: "session-token"})
	response := httptest.NewRecorder()
	s.handler().ServeHTTP(response, request)
	if response.Code != http.StatusOK {
		t.Fatalf("status = %d, body = %s", response.Code, response.Body.String())
	}
	var payload struct {
		PublicKey struct {
			Challenge string `json:"challenge"`
		} `json:"publicKey"`
	}
	if err := json.Unmarshal(response.Body.Bytes(), &payload); err != nil {
		t.Fatal(err)
	}
	if payload.PublicKey.Challenge == "" {
		t.Fatal("empty passkey challenge")
	}
}

func TestResetMFAAndPasswordClearsPasskeys(t *testing.T) {
	user := userRecord{
		ID:            "user-1",
		Username:      "alice",
		TOTPConfirmed: true,
		Passkeys:      []passkeyRecord{{Name: "Laptop"}},
	}
	if _, err := resetUserMFAAndPassword(&user); err != nil {
		t.Fatal(err)
	}
	if len(user.Passkeys) != 0 {
		t.Fatal("passkeys were not cleared")
	}
	if user.TOTPConfirmed || user.TOTPSecret != "" || len(user.RecoveryCodes) != 0 {
		t.Fatal("MFA state was not cleared")
	}
	if !user.MustChangePassword {
		t.Fatal("password reset flag was not set")
	}
}
