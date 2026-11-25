package main

import (
	"bytes"
	"encoding/base64"
	"encoding/json"
	"errors"
	"fmt"
	"log"
	"net"
	"net/http"
	"net/url"
	"strings"
	"time"
	"unicode"
	"unicode/utf8"

	"github.com/go-webauthn/webauthn/protocol"
	"github.com/go-webauthn/webauthn/webauthn"
)

const (
	maxPasskeysPerUser = 10
	passkeyNameMax     = 64
)

type passkeyRecord struct {
	Credential webauthn.Credential `json:"credential"`
	Name       string              `json:"name,omitempty"`
	CreatedAt  string              `json:"created_at"`
	LastUsedAt string              `json:"last_used_at,omitempty"`
}

type passkeyCeremony struct {
	data webauthn.SessionData
	kind string
	name string
}

type passkeyService struct {
	wa *webauthn.WebAuthn
}

func newPasskeyService(rpID, origin string) (*passkeyService, error) {
	rpID = strings.ToLower(strings.TrimSpace(rpID))
	origin = strings.TrimSuffix(strings.TrimSpace(origin), "/")
	if rpID == "" || origin == "" {
		return nil, errors.New("both the WebAuthn RP ID and origin are required")
	}
	if len(rpID) > 253 || strings.ContainsAny(rpID, "/:?#@") {
		return nil, errors.New("WebAuthn RP ID must be a hostname without a scheme or port")
	}
	parsed, err := url.Parse(origin)
	if err != nil || parsed.Host == "" {
		return nil, fmt.Errorf("invalid WebAuthn origin: %w", err)
	}
	if parsed.Path != "" && parsed.Path != "/" {
		return nil, errors.New("WebAuthn origin must not include a path")
	}
	if parsed.RawQuery != "" || parsed.Fragment != "" || parsed.User != nil {
		return nil, errors.New("WebAuthn origin must not include credentials, a query, or a fragment")
	}
	if parsed.Scheme != "https" &&
		!(parsed.Scheme == "http" && loopbackHost(parsed.Hostname())) {
		return nil, errors.New("WebAuthn origin must use HTTPS (HTTP is allowed only for loopback)")
	}
	host := strings.ToLower(parsed.Hostname())
	relyingParty := strings.ToLower(rpID)
	if host != relyingParty && !strings.HasSuffix(host, "."+relyingParty) {
		return nil, errors.New("WebAuthn origin host must match the RP ID or a subdomain of it")
	}
	if net.ParseIP(rpID) != nil && rpID != "127.0.0.1" {
		return nil, errors.New("WebAuthn RP ID must be a hostname, not an IP address")
	}
	originHost := strings.ToLower(parsed.Host)
	if (parsed.Scheme == "https" && parsed.Port() == "443") ||
		(parsed.Scheme == "http" && parsed.Port() == "80") {
		originHost = host
	}
	wa, err := webauthn.New(&webauthn.Config{
		RPDisplayName: "cmdb-lite",
		RPID:          rpID,
		RPOrigins:     []string{parsed.Scheme + "://" + originHost},
		AuthenticatorSelection: protocol.AuthenticatorSelection{
			UserVerification:   protocol.VerificationRequired,
			ResidentKey:        protocol.ResidentKeyRequirementPreferred,
			RequireResidentKey: protocol.ResidentKeyNotRequired(),
		},
	})
	if err != nil {
		return nil, err
	}
	return &passkeyService{wa: wa}, nil
}

func loopbackHost(host string) bool {
	if strings.EqualFold(host, "localhost") {
		return true
	}
	ip := net.ParseIP(host)
	return ip != nil && ip.IsLoopback()
}

func (u *userRecord) WebAuthnID() []byte {
	return []byte(u.ID)
}

func (u *userRecord) WebAuthnName() string {
	return u.Username
}

func (u *userRecord) WebAuthnDisplayName() string {
	return u.Username
}

func (u *userRecord) WebAuthnIcon() string {
	return ""
}

func (u *userRecord) WebAuthnCredentials() []webauthn.Credential {
	credentials := make([]webauthn.Credential, len(u.Passkeys))
	for i := range u.Passkeys {
		credentials[i] = u.Passkeys[i].Credential
	}
	return credentials
}

func passkeyID(id []byte) string {
	return base64.RawURLEncoding.EncodeToString(id)
}

func passkeySummaries(user *userRecord) []map[string]any {
	summaries := make([]map[string]any, 0, len(user.Passkeys))
	for i := range user.Passkeys {
		record := &user.Passkeys[i]
		name := record.Name
		if name == "" {
			name = "Passkey"
		}
		summaries = append(summaries, map[string]any{
			"id":           passkeyID(record.Credential.ID),
			"name":         name,
			"created_at":   record.CreatedAt,
			"last_used_at": record.LastUsedAt,
		})
	}
	return summaries
}

func validatePasskeyName(name string) (string, error) {
	name = strings.TrimSpace(name)
	if name == "" {
		return "Passkey", nil
	}
	if utf8.RuneCountInString(name) > passkeyNameMax {
		return "", fmt.Errorf("passkey name must be at most %d characters", passkeyNameMax)
	}
	for _, character := range name {
		if unicode.IsControl(character) || unicode.Is(unicode.Cf, character) {
			return "", errors.New("passkey name contains invalid characters")
		}
	}
	return name, nil
}

func passkeyAuditDetail(name string) string {
	payload, err := json.Marshal(map[string]string{"name": name})
	if err != nil {
		return "{}"
	}
	return string(payload)
}

func findPasskey(user *userRecord, credentialID []byte) int {
	for i := range user.Passkeys {
		if bytes.Equal(user.Passkeys[i].Credential.ID, credentialID) {
			return i
		}
	}
	return -1
}

func (s *server) passkeysEnabled() bool {
	return s != nil && s.passkeys != nil
}

func (s *server) passkeyLoginAvailable(user *userRecord) bool {
	return s.passkeysEnabled() && user != nil && user.TOTPConfirmed &&
		len(user.Passkeys) > 0
}

func (s *server) passkeyJSONError(w http.ResponseWriter, err error) {
	log.Printf("passkey: %v", err)
	writeAPIError(w, http.StatusBadRequest, "passkey", "passkey operation failed")
}

func (s *server) handlePasskeyLoginBegin(w http.ResponseWriter, r *http.Request) {
	sess := s.sessionFromRequest(r)
	if sess == nil || !sess.pending {
		writeAPIError(w, http.StatusUnauthorized, "mfa_pending", "password login required")
		return
	}
	if !s.passkeysEnabled() {
		writeAPIError(w, http.StatusBadRequest, "passkey_disabled", "passkeys are not configured")
		return
	}
	user := s.state.findUserByID(sess.userID)
	if user == nil || user.Disabled {
		writeAPIError(w, http.StatusUnauthorized, "auth", "unknown user")
		return
	}
	if !user.TOTPConfirmed || len(user.Passkeys) == 0 {
		writeAPIError(w, http.StatusBadRequest, "passkey_unavailable", "no passkey is registered for this account")
		return
	}
	options, session, err := s.passkeys.wa.BeginLogin(
		user, webauthn.WithUserVerification(protocol.VerificationRequired))
	if err != nil {
		s.passkeyJSONError(w, err)
		return
	}
	session.Expires = time.Now().Add(pendingTTL)
	sess.passkey = &passkeyCeremony{data: *session, kind: "login"}
	writeJSON(w, http.StatusOK, options)
}

func (s *server) handlePasskeyLoginComplete(w http.ResponseWriter, r *http.Request) {
	sess := s.sessionFromRequest(r)
	if sess == nil || !sess.pending || sess.passkey == nil ||
		sess.passkey.kind != "login" {
		writeAPIError(w, http.StatusUnauthorized, "passkey_pending", "start passkey login first")
		return
	}
	if !s.passkeysEnabled() {
		writeAPIError(w, http.StatusBadRequest, "passkey_disabled", "passkeys are not configured")
		return
	}
	ipKey := "ip:" + s.clientIP(r)
	if !s.mfaIP.allow(ipKey) {
		writeAPIError(w, http.StatusTooManyRequests, "rate_limited", "too many attempts")
		return
	}
	user := s.state.findUserByID(sess.userID)
	if user == nil || user.Disabled {
		sess.passkey = nil
		writeAPIError(w, http.StatusUnauthorized, "auth", "unknown user")
		return
	}
	ceremony := sess.passkey
	sess.passkey = nil
	credential, err := s.passkeys.wa.FinishLogin(user, ceremony.data, r)
	if err != nil {
		s.mfaIP.fail(ipKey)
		s.audit(user.Username, "passkey-failed", "user", user.ID, `{"method":"passkey"}`)
		writeAPIError(w, http.StatusUnauthorized, "invalid_passkey", "passkey verification failed")
		return
	}
	index := findPasskey(user, credential.ID)
	if index < 0 {
		s.mfaIP.fail(ipKey)
		s.audit(user.Username, "passkey-failed", "user", user.ID,
			`{"method":"passkey","reason":"unknown_credential"}`)
		writeAPIError(w, http.StatusUnauthorized, "invalid_passkey", "passkey verification failed")
		return
	}
	if credential.Authenticator.CloneWarning {
		s.audit(user.Username, "passkey-clone-warning", "user", user.ID,
			`{"method":"passkey"}`)
	}
	user.Passkeys[index].Credential = *credential
	user.Passkeys[index].LastUsedAt = nowISO()
	user.touch()
	if err := s.saveState(); err != nil {
		writeAPIError(w, http.StatusInternalServerError, "state", err.Error())
		return
	}
	full, err := s.rotateSession(w, r, sess, user)
	if err != nil {
		writeAPIError(w, http.StatusInternalServerError, "session", err.Error())
		return
	}
	s.mfaIP.reset(ipKey)
	s.audit(user.Username, "login-passkey", "user", user.ID, `{"method":"passkey"}`)
	writeJSON(w, http.StatusOK, map[string]any{
		"ok":   true,
		"user": userView(user),
		"csrf": full.csrf,
	})
}

func (s *server) handlePasskeyRegisterBegin(w http.ResponseWriter, r *http.Request) {
	sess := s.sessionFromRequest(r)
	user := s.userForSession(sess)
	if user == nil {
		writeAPIError(w, http.StatusUnauthorized, "auth_required", "login required")
		return
	}
	if !s.passkeysEnabled() {
		writeAPIError(w, http.StatusBadRequest, "passkey_disabled", "passkeys are not configured")
		return
	}
	if user.MustChangePassword {
		writeAPIError(w, http.StatusForbidden, "password_change_required", "change the account password before continuing")
		return
	}
	if demoBlockCredentialChange(w, user) {
		return
	}
	var request struct {
		Current string `json:"current"`
		Name    string `json:"name"`
	}
	if err := decodeJSON(r, &request); err != nil {
		writeAPIError(w, http.StatusBadRequest, "invalid_json", err.Error())
		return
	}
	if !s.verifyCurrentPassword(w, r, user, request.Current) {
		return
	}
	name, err := validatePasskeyName(request.Name)
	if err != nil {
		writeAPIError(w, http.StatusBadRequest, "passkey_name", err.Error())
		return
	}
	if len(user.Passkeys) >= maxPasskeysPerUser {
		writeAPIError(w, http.StatusConflict, "passkey_limit", "too many passkeys for this account")
		return
	}
	exclusions := make([]protocol.CredentialDescriptor, len(user.Passkeys))
	for i := range user.Passkeys {
		exclusions[i] = user.Passkeys[i].Credential.Descriptor()
	}
	options, session, err := s.passkeys.wa.BeginRegistration(
		user,
		webauthn.WithAuthenticatorSelection(protocol.AuthenticatorSelection{
			UserVerification:   protocol.VerificationRequired,
			ResidentKey:        protocol.ResidentKeyRequirementPreferred,
			RequireResidentKey: protocol.ResidentKeyNotRequired(),
		}),
		webauthn.WithExclusions(exclusions),
	)
	if err != nil {
		s.passkeyJSONError(w, err)
		return
	}
	session.Expires = time.Now().Add(pendingTTL)
	sess.passkey = &passkeyCeremony{data: *session, kind: "register", name: name}
	writeJSON(w, http.StatusOK, options)
}

func (s *server) handlePasskeyRegisterComplete(w http.ResponseWriter, r *http.Request) {
	sess := s.sessionFromRequest(r)
	user := s.userForSession(sess)
	if user == nil {
		writeAPIError(w, http.StatusUnauthorized, "auth_required", "login required")
		return
	}
	if !s.passkeysEnabled() {
		writeAPIError(w, http.StatusBadRequest, "passkey_disabled", "passkeys are not configured")
		return
	}
	if sess.passkey == nil || sess.passkey.kind != "register" {
		writeAPIError(w, http.StatusBadRequest, "passkey_pending", "start passkey registration first")
		return
	}
	ceremony := sess.passkey
	sess.passkey = nil
	credential, err := s.passkeys.wa.FinishRegistration(user, ceremony.data, r)
	if err != nil {
		s.passkeyJSONError(w, err)
		return
	}
	if findPasskey(user, credential.ID) >= 0 {
		writeAPIError(w, http.StatusConflict, "passkey_exists", "this passkey is already registered")
		return
	}
	if len(user.Passkeys) >= maxPasskeysPerUser {
		writeAPIError(w, http.StatusConflict, "passkey_limit", "too many passkeys for this account")
		return
	}
	user.Passkeys = append(user.Passkeys, passkeyRecord{
		Credential: *credential,
		Name:       ceremony.name,
		CreatedAt:  nowISO(),
	})
	user.touch()
	if err := s.saveState(); err != nil {
		writeAPIError(w, http.StatusInternalServerError, "state", err.Error())
		return
	}
	s.audit(user.Username, "passkey-register", "user", user.ID,
		passkeyAuditDetail(ceremony.name))
	writeJSON(w, http.StatusOK, map[string]any{
		"ok":       true,
		"passkeys": passkeySummaries(user),
	})
}

func (s *server) handlePasskeyDelete(w http.ResponseWriter, r *http.Request) {
	sess := s.sessionFromRequest(r)
	user := s.userForSession(sess)
	if user == nil {
		writeAPIError(w, http.StatusUnauthorized, "auth_required", "login required")
		return
	}
	if !s.passkeysEnabled() {
		writeAPIError(w, http.StatusBadRequest, "passkey_disabled", "passkeys are not configured")
		return
	}
	if demoBlockCredentialChange(w, user) {
		return
	}
	var request struct {
		ID      string `json:"id"`
		Current string `json:"current"`
	}
	if err := decodeJSON(r, &request); err != nil {
		writeAPIError(w, http.StatusBadRequest, "invalid_json", err.Error())
		return
	}
	if !s.verifyCurrentPassword(w, r, user, request.Current) {
		return
	}
	id, err := base64.RawURLEncoding.DecodeString(request.ID)
	if err != nil || len(id) == 0 {
		writeAPIError(w, http.StatusBadRequest, "passkey", "invalid passkey ID")
		return
	}
	index := findPasskey(user, id)
	if index < 0 {
		writeAPIError(w, http.StatusNotFound, "passkey_not_found", "passkey not found")
		return
	}
	name := user.Passkeys[index].Name
	user.Passkeys = append(user.Passkeys[:index], user.Passkeys[index+1:]...)
	user.touch()
	if err := s.saveState(); err != nil {
		writeAPIError(w, http.StatusInternalServerError, "state", err.Error())
		return
	}
	s.audit(user.Username, "passkey-delete", "user", user.ID,
		passkeyAuditDetail(name))
	writeJSON(w, http.StatusOK, map[string]any{
		"ok":       true,
		"passkeys": passkeySummaries(user),
	})
}
