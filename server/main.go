package main

import (
	"crypto/rand"
	"crypto/sha256"
	"embed"
	"encoding/hex"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"log"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"

	"golang.org/x/term"
)

//go:embed login.html security.html users.html
var pageFiles embed.FS

const (
	sessionCookieName = "cmdb_session"
	defaultIdle       = 30 * time.Minute
	defaultAbsolute   = 12 * time.Hour
	pendingTTL        = 5 * time.Minute
	setupTokenTTL     = 24 * time.Hour
)

type session struct {
	tokenHash       string
	userID          string
	username        string
	isAdmin         bool
	csrf            string
	created         time.Time
	lastSeen        time.Time
	idleExpires     time.Time
	absoluteExpires time.Time
	mfaVerified     bool
	pending         bool
}

type server struct {
	rpc       *rpcClient
	webDir    string
	state     authState
	stateMu   sync.Mutex
	sessions  map[string]*session
	sessMu    sync.Mutex
	reqMu     sync.Mutex
	loginIP   *attemptLimiter
	loginUser *attemptLimiter
	mfaIP     *attemptLimiter
	reauth    *attemptLimiter
	setupHash string
	setupExp  time.Time
	idle      time.Duration
	absolute  time.Duration
	proxies   []*net.IPNet
}

type attemptLimiter struct {
	mu      sync.Mutex
	limit   int
	window  time.Duration
	entries map[string]attemptEntry
}

type attemptEntry struct {
	count int
	reset time.Time
}

func newAttemptLimiter(limit int, window time.Duration) *attemptLimiter {
	return &attemptLimiter{limit: limit, window: window, entries: make(map[string]attemptEntry)}
}

func (l *attemptLimiter) allow(key string) bool {
	l.mu.Lock()
	defer l.mu.Unlock()
	entry, ok := l.entries[key]
	if !ok || time.Now().After(entry.reset) {
		return true
	}
	return entry.count < l.limit
}

func (l *attemptLimiter) fail(key string) {
	l.mu.Lock()
	defer l.mu.Unlock()
	now := time.Now()
	entry, ok := l.entries[key]
	if !ok || now.After(entry.reset) {
		entry = attemptEntry{reset: now.Add(l.window)}
	}
	entry.count++
	l.entries[key] = entry
}

func (l *attemptLimiter) reset(key string) {
	l.mu.Lock()
	defer l.mu.Unlock()
	delete(l.entries, key)
}

func main() {
	var (
		dbPath    = flag.String("db", "", "encrypted database path")
		listen    = flag.String("listen", "127.0.0.1:8080", "HTTP listen address")
		rpcPath   = flag.String("rpc", "", "cmdb-rpc executable path")
		webDir    = flag.String("web-dir", "", "directory containing index.html, app.js, and style.css")
		proxyList = flag.String("proxy", "127.0.0.1/32,::1/128", "trusted proxy CIDRs for X-Forwarded-*")
		idleFlag  = flag.Duration("session-idle", defaultIdle, "idle session timeout")
		absolute  = flag.Duration("session-absolute", defaultAbsolute, "absolute session timeout")
		resetAuth = flag.Bool("reset-auth", false, "clear accounts/TOTP/recovery state and exit (break-glass)")
	)
	flag.Parse()
	if *dbPath == "" {
		log.Fatal("--db is required")
	}
	if *idleFlag <= 0 || *absolute <= 0 || *idleFlag > *absolute {
		log.Fatal("invalid session timeouts")
	}

	passphrase, err := readPassphrase()
	if err != nil {
		log.Fatal(err)
	}
	defer func() { passphrase = "" }()

	rpcPathResolved := resolveRPC(*rpcPath)
	rpc, err := startRPC(rpcPathResolved)
	if err != nil {
		log.Fatalf("start %s: %v", rpcPathResolved, err)
	}
	defer rpc.close()

	opened, err := rpc.call("db.open", map[string]any{
		"path":       *dbPath,
		"passphrase": passphrase,
	}, "system")
	if err != nil {
		log.Fatalf("open database: %v", err)
	}
	log.Printf("database opened: %s", string(opened))

	if *resetAuth {
		var result map[string]any
		state := `{"version":1,"users":[]}`
		if err := rpc.callResult("auth.state.set", map[string]any{"state": state}, "break-glass", &result); err != nil {
			log.Fatalf("reset auth state: %v", err)
		}
		_ = rpc.callResult("auth.audit", map[string]any{
			"actor":       "break-glass",
			"action":      "reset-auth-state",
			"entity_type": "auth",
			"entity_id":   "",
			"detail":      `{"reason":"operator break-glass"}`,
		}, "break-glass", &result)
		log.Printf("Authentication state cleared. Restart cmdb-server and use the new first-run setup token.")
		return
	}

	s := &server{
		rpc:       rpc,
		webDir:    resolveWebDir(*webDir),
		sessions:  make(map[string]*session),
		loginIP:   newAttemptLimiter(10, 15*time.Minute),
		loginUser: newAttemptLimiter(5, 15*time.Minute),
		mfaIP:     newAttemptLimiter(20, 15*time.Minute),
		reauth:    newAttemptLimiter(10, 15*time.Minute),
		idle:      *idleFlag,
		absolute:  *absolute,
	}
	s.proxies, err = parseProxies(*proxyList)
	if err != nil {
		log.Fatalf("proxy CIDR: %v", err)
	}
	if err := s.loadState(); err != nil {
		log.Fatalf("load auth state: %v", err)
	}
	if len(s.state.Users) == 0 {
		token, hash, err := newSetupToken()
		if err != nil {
			log.Fatalf("setup token: %v", err)
		}
		s.setupHash = hash
		s.setupExp = time.Now().Add(setupTokenTTL)
		log.Printf("FIRST-RUN SETUP TOKEN: %s", token)
		log.Printf("Create the first admin at http://%s/login", *listen)
	}

	srv := &http.Server{
		Addr:              *listen,
		Handler:           s.handler(),
		ReadHeaderTimeout: 10 * time.Second,
		ReadTimeout:       30 * time.Second,
		WriteTimeout:      60 * time.Second,
		IdleTimeout:       120 * time.Second,
	}
	log.Printf("cmdb-server listening on http://%s", *listen)
	if err := srv.ListenAndServe(); err != nil && !errors.Is(err, http.ErrServerClosed) {
		log.Fatal(err)
	}
}

func readPassphrase() (string, error) {
	if value := os.Getenv("CMDB_DB_PASSPHRASE"); value != "" {
		_ = os.Unsetenv("CMDB_DB_PASSPHRASE")
		return value, nil
	}
	if !term.IsTerminal(int(os.Stdin.Fd())) {
		return "", errors.New("database passphrase required: set CMDB_DB_PASSPHRASE or run interactively")
	}
	fmt.Fprint(os.Stderr, "Database passphrase: ")
	value, err := term.ReadPassword(int(os.Stdin.Fd()))
	fmt.Fprintln(os.Stderr)
	if err != nil {
		return "", err
	}
	return string(value), nil
}

func resolveRPC(path string) string {
	if path != "" {
		return path
	}
	if exe, err := os.Executable(); err == nil {
		candidate := filepath.Join(filepath.Dir(exe), "cmdb-rpc")
		if _, err := os.Stat(candidate); err == nil {
			return candidate
		}
	}
	return "cmdb-rpc"
}

func resolveWebDir(path string) string {
	if path != "" {
		return path
	}
	if env := os.Getenv("CMDB_WEB_DIR"); env != "" {
		return env
	}
	if exe, err := os.Executable(); err == nil {
		dir := filepath.Dir(exe)
		for _, candidate := range []string{
			filepath.Join(dir, "web"),
			filepath.Join(dir, "..", "share", "cmdb-lite", "web"),
			filepath.Join(dir, "..", "share", "cmdb-lite", "web"),
		} {
			if _, err := os.Stat(filepath.Join(candidate, "index.html")); err == nil {
				return candidate
			}
		}
	}
	return "src/web"
}

func parseProxies(value string) ([]*net.IPNet, error) {
	var result []*net.IPNet
	for _, item := range strings.Split(value, ",") {
		item = strings.TrimSpace(item)
		if item == "" {
			continue
		}
		if !strings.Contains(item, "/") {
			if ip := net.ParseIP(item); ip != nil {
				if ip.To4() != nil {
					item += "/32"
				} else {
					item += "/128"
				}
			}
		}
		_, network, err := net.ParseCIDR(item)
		if err != nil {
			return nil, fmt.Errorf("%s: %w", item, err)
		}
		result = append(result, network)
	}
	return result, nil
}

func newSetupToken() (string, string, error) {
	raw := make([]byte, 32)
	if _, err := rand.Read(raw); err != nil {
		return "", "", err
	}
	token := hex.EncodeToString(raw)
	sum := sha256.Sum256([]byte(token))
	return token, hex.EncodeToString(sum[:]), nil
}

func (s *server) loadState() error {
	var result struct {
		State string `json:"state"`
	}
	if err := s.rpc.callResult("auth.state.get", map[string]any{}, "system", &result); err != nil {
		return err
	}
	s.state = authState{Version: 1}
	if strings.TrimSpace(result.State) != "" {
		if err := json.Unmarshal([]byte(result.State), &s.state); err != nil {
			return err
		}
	}
	if s.state.Version == 0 {
		s.state.Version = 1
	}
	return nil
}

func (s *server) saveState() error {
	s.stateMu.Lock()
	defer s.stateMu.Unlock()
	payload, err := json.Marshal(s.state)
	if err != nil {
		return err
	}
	var result map[string]any
	return s.rpc.callResult("auth.state.set", map[string]any{"state": string(payload)}, "system", &result)
}

func (s *server) audit(actor, action, entityType, entityID, detail string) {
	if detail == "" {
		detail = "{}"
	}
	var result map[string]any
	if err := s.rpc.callResult("auth.audit", map[string]any{
		"actor":       actor,
		"action":      action,
		"entity_type": entityType,
		"entity_id":   entityID,
		"detail":      detail,
	}, actor, &result); err != nil {
		log.Printf("audit %s/%s: %v", actor, action, err)
	}
}

func (s *server) handler() http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("GET /login", s.handleLoginPage)
	mux.HandleFunc("GET /security", s.handleSecurityPage)
	mux.HandleFunc("GET /users", s.handleUsersPage)
	mux.HandleFunc("GET /api/v1/auth/status", s.handleAuthStatus)
	mux.HandleFunc("POST /api/v1/auth/setup", s.handleSetup)
	mux.HandleFunc("POST /api/v1/auth/login", s.handleLogin)
	mux.HandleFunc("POST /api/v1/auth/mfa", s.handleMFA)
	mux.HandleFunc("POST /api/v1/auth/recovery", s.handleRecovery)
	mux.HandleFunc("POST /api/v1/auth/logout", s.handleLogout)
	mux.HandleFunc("GET /api/v1/auth/session", s.handleSession)
	mux.HandleFunc("POST /api/v1/auth/password", s.requireAuth(s.handlePassword))
	mux.HandleFunc("POST /api/v1/auth/totp/setup", s.requireAuth(s.handleTOTPSetup))
	mux.HandleFunc("POST /api/v1/auth/totp/confirm", s.requireAuth(s.handleTOTPConfirm))
	mux.HandleFunc("POST /api/v1/auth/recovery/generate", s.requireMFAReady(s.handleRecoveryGenerate))
	mux.HandleFunc("GET /api/v1/users", s.requireAdmin(s.handleListUsers))
	mux.HandleFunc("POST /api/v1/users", s.requireAdmin(s.handleCreateUser))
	mux.HandleFunc("POST /api/v1/users/{id}/disable", s.requireAdmin(s.handleDisableUser))
	mux.HandleFunc("POST /api/v1/users/{id}/enable", s.requireAdmin(s.handleEnableUser))
	mux.HandleFunc("POST /api/v1/users/{id}/reset-password", s.requireAdmin(s.handleResetPassword))
	mux.HandleFunc("POST /api/v1/users/{id}/reset-mfa", s.requireAdmin(s.handleResetMFA))
	mux.HandleFunc("POST /api/v1/rpc", s.requireReady(s.handleRPC))
	mux.HandleFunc("GET /", s.handleIndex)
	mux.HandleFunc("GET /index.html", s.handleIndex)
	mux.HandleFunc("GET /app.js", s.handleStatic("app.js", "application/javascript"))
	mux.HandleFunc("GET /style.css", s.handleStatic("style.css", "text/css"))
	return securityHeaders(s.serializeAPI(mux))
}

func (s *server) serializeAPI(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if strings.HasPrefix(r.URL.Path, "/api/") ||
			r.URL.Path == "/" || r.URL.Path == "/index.html" ||
			r.URL.Path == "/login" || r.URL.Path == "/security" ||
			r.URL.Path == "/users" {
			s.reqMu.Lock()
			defer s.reqMu.Unlock()
		}
		next.ServeHTTP(w, r)
	})
}

func securityHeaders(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("X-Content-Type-Options", "nosniff")
		w.Header().Set("X-Frame-Options", "DENY")
		w.Header().Set("Referrer-Policy", "no-referrer")
		w.Header().Set("Cache-Control", "no-store")
		next.ServeHTTP(w, r)
	})
}

func writeJSON(w http.ResponseWriter, status int, value any) {
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(value)
}

func writeAPIError(w http.ResponseWriter, status int, code, message string) {
	writeJSON(w, status, map[string]any{
		"ok": false,
		"error": map[string]any{
			"code":    code,
			"message": message,
		},
	})
}

func decodeJSON(r *http.Request, out any) error {
	return decodeJSONLimit(r, out, 1<<20)
}

func decodeJSONLimit(r *http.Request, out any, limit int64) error {
	if !strings.HasPrefix(strings.ToLower(r.Header.Get("Content-Type")), "application/json") {
		return errors.New("Content-Type must be application/json")
	}
	defer r.Body.Close()
	decoder := json.NewDecoder(io.LimitReader(r.Body, limit))
	return decoder.Decode(out)
}

func (s *server) handleLoginPage(w http.ResponseWriter, r *http.Request) {
	if s.fullSession(r) != nil {
		http.Redirect(w, r, "/", http.StatusSeeOther)
		return
	}
	data, _ := pageFiles.ReadFile("login.html")
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	w.Header().Set("Content-Security-Policy", "default-src 'none'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; connect-src 'self'; img-src data:; form-action 'self'; base-uri 'none'")
	_, _ = w.Write(data)
}

func (s *server) handleSecurityPage(w http.ResponseWriter, r *http.Request) {
	if s.fullSession(r) == nil {
		http.Redirect(w, r, "/login", http.StatusSeeOther)
		return
	}
	data, _ := pageFiles.ReadFile("security.html")
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	w.Header().Set("Content-Security-Policy", "default-src 'none'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; connect-src 'self'; img-src data:; form-action 'self'; base-uri 'none'")
	_, _ = w.Write(data)
}

func (s *server) handleUsersPage(w http.ResponseWriter, r *http.Request) {
	sess := s.fullSession(r)
	if sess == nil {
		http.Redirect(w, r, "/login", http.StatusSeeOther)
		return
	}
	if !sess.isAdmin {
		http.Error(w, "admin required", http.StatusForbidden)
		return
	}
	if readiness(s.userForSession(sess)) != "" {
		http.Redirect(w, r, "/security", http.StatusSeeOther)
		return
	}
	data, _ := pageFiles.ReadFile("users.html")
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	w.Header().Set("Content-Security-Policy", "default-src 'none'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; connect-src 'self'; img-src data:; form-action 'self'; base-uri 'none'")
	_, _ = w.Write(data)
}

func (s *server) handleIndex(w http.ResponseWriter, r *http.Request) {
	if len(s.state.Users) == 0 {
		http.Redirect(w, r, "/login", http.StatusSeeOther)
		return
	}
	sess := s.fullSession(r)
	if sess == nil {
		http.Redirect(w, r, "/login", http.StatusSeeOther)
		return
	}
	if readiness(s.userForSession(sess)) != "" {
		http.Redirect(w, r, "/security", http.StatusSeeOther)
		return
	}
	path := filepath.Join(s.webDir, "index.html")
	http.ServeFile(w, r, path)
}

func (s *server) handleStatic(name, contentType string) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", contentType)
		http.ServeFile(w, r, filepath.Join(s.webDir, name))
	}
}

func (s *server) fullSession(r *http.Request) *session {
	sess := s.sessionFromRequest(r)
	if sess == nil || !sess.mfaVerified || sess.pending {
		return nil
	}
	return sess
}

func (s *server) sessionFromRequest(r *http.Request) *session {
	cookie, err := r.Cookie(sessionCookieName)
	if err != nil || cookie.Value == "" {
		return nil
	}
	hash := sha256Hex(cookie.Value)
	s.sessMu.Lock()
	defer s.sessMu.Unlock()
	sess, ok := s.sessions[hash]
	if !ok {
		return nil
	}
	now := time.Now()
	if now.After(sess.idleExpires) || now.After(sess.absoluteExpires) {
		delete(s.sessions, hash)
		return nil
	}
	user := s.state.findUserByID(sess.userID)
	if user == nil || user.Disabled {
		delete(s.sessions, hash)
		return nil
	}
	sess.lastSeen = now
	sess.idleExpires = now.Add(s.idle)
	if sess.idleExpires.After(sess.absoluteExpires) {
		sess.idleExpires = sess.absoluteExpires
	}
	return sess
}

func (s *server) requireAuth(next http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		sess := s.fullSession(r)
		if sess == nil {
			writeAPIError(w, http.StatusUnauthorized, "auth_required", "login required")
			return
		}
		if r.Method != http.MethodGet && !s.validCSRF(r, sess) {
			writeAPIError(w, http.StatusForbidden, "csrf", "invalid CSRF token")
			return
		}
		next(w, r)
	}
}

func (s *server) requireMFAReady(next http.HandlerFunc) http.HandlerFunc {
	return s.requireAuth(func(w http.ResponseWriter, r *http.Request) {
		sess := s.fullSession(r)
		user := s.userForSession(sess)
		if user == nil {
			writeAPIError(w, http.StatusUnauthorized, "auth_required", "login required")
			return
		}
		if user.MustChangePassword {
			writeAPIError(w, http.StatusForbidden, "password_change_required", "change the account password before continuing")
			return
		}
		if !user.TOTPConfirmed {
			writeAPIError(w, http.StatusForbidden, "mfa_enrollment_required", "enroll an authenticator app before continuing")
			return
		}
		next(w, r)
	})
}

func (s *server) requireReady(next http.HandlerFunc) http.HandlerFunc {
	return s.requireMFAReady(func(w http.ResponseWriter, r *http.Request) {
		sess := s.fullSession(r)
		user := s.userForSession(sess)
		if user == nil {
			writeAPIError(w, http.StatusUnauthorized, "auth_required", "login required")
			return
		}
		if recoveryCodesRemaining(user) == 0 {
			writeAPIError(w, http.StatusForbidden, "recovery_codes_required", "generate recovery codes before continuing")
			return
		}
		next(w, r)
	})
}

func (s *server) requireAdmin(next http.HandlerFunc) http.HandlerFunc {
	return s.requireReady(func(w http.ResponseWriter, r *http.Request) {
		sess := s.fullSession(r)
		if sess == nil || !sess.isAdmin {
			writeAPIError(w, http.StatusForbidden, "admin_required", "admin required")
			return
		}
		next(w, r)
	})
}

func (s *server) userForSession(sess *session) *userRecord {
	if sess == nil {
		return nil
	}
	return s.state.findUserByID(sess.userID)
}

func (s *server) verifyCurrentPassword(w http.ResponseWriter, r *http.Request, user *userRecord, password string) bool {
	key := "user:" + user.ID + "|ip:" + s.clientIP(r)
	if !s.reauth.allow(key) {
		writeAPIError(w, http.StatusTooManyRequests, "rate_limited", "too many password attempts; try again later")
		return false
	}
	if !verifyPassword(user.PasswordHash, password) {
		s.reauth.fail(key)
		writeAPIError(w, http.StatusUnauthorized, "invalid_password", "current password is incorrect")
		return false
	}
	s.reauth.reset(key)
	return true
}

func (s *server) validCSRF(r *http.Request, sess *session) bool {
	value := r.Header.Get("X-CSRF-Token")
	return value != "" && subtleCompare(value, sess.csrf)
}

func subtleCompare(a, b string) bool {
	if len(a) != len(b) {
		return false
	}
	var diff byte
	for i := 0; i < len(a); i++ {
		diff |= a[i] ^ b[i]
	}
	return diff == 0
}

func (s *server) createSession(w http.ResponseWriter, r *http.Request, user *userRecord, mfa bool, ttl time.Duration) (*session, error) {
	token, err := randomHex(32)
	if err != nil {
		return nil, err
	}
	csrf, err := randomHex(32)
	if err != nil {
		return nil, err
	}
	now := time.Now()
	absolute := now.Add(ttl)
	idle := now.Add(s.idle)
	if !mfa {
		idle = now.Add(pendingTTL)
		absolute = now.Add(pendingTTL)
	}
	if idle.After(absolute) {
		idle = absolute
	}
	sess := &session{
		tokenHash:       sha256Hex(token),
		userID:          user.ID,
		username:        user.Username,
		isAdmin:         user.IsAdmin,
		csrf:            csrf,
		created:         now,
		lastSeen:        now,
		idleExpires:     idle,
		absoluteExpires: absolute,
		mfaVerified:     mfa,
		pending:         !mfa,
	}
	s.sessMu.Lock()
	s.sessions[sess.tokenHash] = sess
	s.sessMu.Unlock()
	http.SetCookie(w, &http.Cookie{
		Name:     sessionCookieName,
		Value:    token,
		Path:     "/",
		HttpOnly: true,
		Secure:   s.isHTTPS(r),
		SameSite: http.SameSiteStrictMode,
		MaxAge:   int(time.Until(absolute).Seconds()),
	})
	return sess, nil
}

func (s *server) rotateSession(w http.ResponseWriter, r *http.Request, old *session, user *userRecord) (*session, error) {
	s.sessMu.Lock()
	delete(s.sessions, old.tokenHash)
	s.sessMu.Unlock()
	return s.createSession(w, r, user, true, s.absolute)
}

func (s *server) clearSession(w http.ResponseWriter, r *http.Request) {
	if cookie, err := r.Cookie(sessionCookieName); err == nil {
		hash := sha256Hex(cookie.Value)
		s.sessMu.Lock()
		delete(s.sessions, hash)
		s.sessMu.Unlock()
	}
	http.SetCookie(w, &http.Cookie{
		Name:     sessionCookieName,
		Value:    "",
		Path:     "/",
		HttpOnly: true,
		Secure:   s.isHTTPS(r),
		SameSite: http.SameSiteStrictMode,
		MaxAge:   -1,
	})
}

func (s *server) revokeUserSessions(userID string) {
	s.sessMu.Lock()
	defer s.sessMu.Unlock()
	for key, sess := range s.sessions {
		if sess.userID == userID {
			delete(s.sessions, key)
		}
	}
}

func (s *server) revokeOtherUserSessions(userID, keepTokenHash string) {
	s.sessMu.Lock()
	defer s.sessMu.Unlock()
	for key, sess := range s.sessions {
		if sess.userID == userID && key != keepTokenHash {
			delete(s.sessions, key)
		}
	}
}

func (s *server) clientIP(r *http.Request) string {
	host, _, err := net.SplitHostPort(r.RemoteAddr)
	if err != nil {
		host = r.RemoteAddr
	}
	if s.trustedProxy(net.ParseIP(host)) {
		if forwarded := r.Header.Get("X-Forwarded-For"); forwarded != "" {
			parts := strings.Split(forwarded, ",")
			// Take the rightmost entry: a trusted proxy that appends the peer
			// address puts the real client last, even if the client supplied a
			// forged header.
			last := strings.TrimSpace(parts[len(parts)-1])
			if net.ParseIP(last) != nil {
				return last
			}
		}
	}
	return host
}

func (s *server) isHTTPS(r *http.Request) bool {
	if r.TLS != nil {
		return true
	}
	host, _, _ := net.SplitHostPort(r.RemoteAddr)
	if s.trustedProxy(net.ParseIP(host)) {
		return strings.EqualFold(r.Header.Get("X-Forwarded-Proto"), "https")
	}
	return false
}

func (s *server) trustedProxy(ip net.IP) bool {
	if ip == nil {
		return false
	}
	for _, network := range s.proxies {
		if network.Contains(ip) {
			return true
		}
	}
	return false
}

func (s *server) handleAuthStatus(w http.ResponseWriter, r *http.Request) {
	response := map[string]any{
		"setup_required": len(s.state.Users) == 0,
		"authenticated":  false,
	}
	sess := s.sessionFromRequest(r)
	if sess != nil {
		user := s.state.findUserByID(sess.userID)
		if user != nil {
			response["authenticated"] = sess.mfaVerified && !sess.pending
			response["mfa_required"] = sess.pending
			response["user"] = userView(user)
			if sess.mfaVerified && !sess.pending {
				response["csrf"] = sess.csrf
			}
		}
	}
	writeJSON(w, http.StatusOK, response)
}

func userView(user *userRecord) map[string]any {
	return map[string]any{
		"id":                       user.ID,
		"username":                 user.Username,
		"is_admin":                 user.IsAdmin,
		"disabled":                 user.Disabled,
		"must_change_password":     user.MustChangePassword,
		"totp_enabled":             user.TOTPConfirmed,
		"recovery_codes_remaining": recoveryCodesRemaining(user),
	}
}

func recoveryCodesRemaining(user *userRecord) int {
	remaining := 0
	for i := range user.RecoveryCodes {
		if user.RecoveryCodes[i].UsedAt == "" {
			remaining++
		}
	}
	return remaining
}

func readiness(user *userRecord) string {
	if user == nil || user.Disabled {
		return "auth_required"
	}
	if user.MustChangePassword {
		return "password_change_required"
	}
	if !user.TOTPConfirmed {
		return "mfa_enrollment_required"
	}
	if recoveryCodesRemaining(user) == 0 {
		return "recovery_codes_required"
	}
	return ""
}

func (s *server) handleSetup(w http.ResponseWriter, r *http.Request) {
	var request struct {
		Token    string `json:"token"`
		Username string `json:"username"`
		Password string `json:"password"`
	}
	if err := decodeJSON(r, &request); err != nil {
		writeAPIError(w, http.StatusBadRequest, "invalid_json", err.Error())
		return
	}
	if len(s.state.Users) > 0 {
		writeAPIError(w, http.StatusConflict, "setup_complete", "setup has already been completed")
		return
	}
	if s.setupHash == "" || time.Now().After(s.setupExp) ||
		sha256Hex(strings.TrimSpace(request.Token)) != s.setupHash {
		writeAPIError(w, http.StatusForbidden, "setup_token", "invalid or expired setup token")
		return
	}
	if err := validateUsername(request.Username); err != nil {
		writeAPIError(w, http.StatusBadRequest, "username", err.Error())
		return
	}
	hash, err := hashPassword(request.Password)
	if err != nil {
		writeAPIError(w, http.StatusBadRequest, "password", err.Error())
		return
	}
	user := userRecord{
		ID:           newID(),
		Username:     request.Username,
		PasswordHash: hash,
		IsAdmin:      true,
		CreatedAt:    nowISO(),
		UpdatedAt:    nowISO(),
	}
	s.state.Users = append(s.state.Users, user)
	if err := s.saveState(); err != nil {
		s.state.Users = s.state.Users[:len(s.state.Users)-1]
		writeAPIError(w, http.StatusInternalServerError, "state", err.Error())
		return
	}
	s.setupHash = ""
	sess, err := s.createSession(w, r, &s.state.Users[0], true, s.absolute)
	if err != nil {
		writeAPIError(w, http.StatusInternalServerError, "session", err.Error())
		return
	}
	s.audit(user.Username, "setup", "user", user.ID, `{"role":"admin"}`)
	writeJSON(w, http.StatusOK, map[string]any{
		"ok":   true,
		"user": userView(&s.state.Users[0]),
		"csrf": sess.csrf,
	})
}

func (s *server) handleLogin(w http.ResponseWriter, r *http.Request) {
	var request struct {
		Username string `json:"username"`
		Password string `json:"password"`
	}
	if err := decodeJSON(r, &request); err != nil {
		writeAPIError(w, http.StatusBadRequest, "invalid_json", err.Error())
		return
	}
	ip := s.clientIP(r)
	ipKey := "ip:" + ip
	userKey := "user:" + strings.ToLower(strings.TrimSpace(request.Username))
	if !s.loginIP.allow(ipKey) || !s.loginUser.allow(userKey) {
		w.Header().Set("Retry-After", "900")
		writeAPIError(w, http.StatusTooManyRequests, "rate_limited", "too many login attempts; try again later")
		return
	}
	user := s.state.findUser(strings.TrimSpace(request.Username))
	hash := dummyPasswordHash()
	if user != nil {
		hash = user.PasswordHash
	}
	valid := verifyPassword(hash, request.Password)
	if user == nil || user.Disabled || !valid {
		s.loginIP.fail(ipKey)
		s.loginUser.fail(userKey)
		s.audit("anonymous", "login-failed", "user", "", `{"reason":"invalid_credentials"}`)
		writeAPIError(w, http.StatusUnauthorized, "invalid_credentials", "invalid username or password")
		return
	}
	s.loginIP.reset(ipKey)
	s.loginUser.reset(userKey)
	if user.TOTPConfirmed {
		if _, err := s.createSession(w, r, user, false, pendingTTL); err != nil {
			writeAPIError(w, http.StatusInternalServerError, "session", err.Error())
			return
		}
		s.audit(user.Username, "login-password", "user", user.ID, `{"mfa":"pending"}`)
		writeJSON(w, http.StatusOK, map[string]any{
			"ok":           true,
			"mfa_required": true,
			"user":         userView(user),
		})
		return
	}
	sess, err := s.createSession(w, r, user, true, s.absolute)
	if err != nil {
		writeAPIError(w, http.StatusInternalServerError, "session", err.Error())
		return
	}
	s.audit(user.Username, "login", "user", user.ID, `{"mfa":"none"}`)
	writeJSON(w, http.StatusOK, map[string]any{
		"ok":   true,
		"user": userView(user),
		"csrf": sess.csrf,
	})
}

func (s *server) handleMFA(w http.ResponseWriter, r *http.Request) {
	sess := s.sessionFromRequest(r)
	if sess == nil || !sess.pending {
		writeAPIError(w, http.StatusUnauthorized, "mfa_pending", "password login required")
		return
	}
	ipKey := "ip:" + s.clientIP(r)
	if !s.mfaIP.allow(ipKey) {
		writeAPIError(w, http.StatusTooManyRequests, "rate_limited", "too many attempts")
		return
	}
	var request struct {
		Code string `json:"code"`
	}
	if err := decodeJSON(r, &request); err != nil {
		writeAPIError(w, http.StatusBadRequest, "invalid_json", err.Error())
		return
	}
	user := s.state.findUserByID(sess.userID)
	if user == nil || !user.TOTPConfirmed {
		writeAPIError(w, http.StatusBadRequest, "totp", "TOTP is not enrolled")
		return
	}
	step, ok := validateTOTP(user.TOTPSecret, request.Code, time.Now())
	if !ok || step <= user.TOTPLastStep {
		s.mfaIP.fail(ipKey)
		s.audit(user.Username, "mfa-failed", "user", user.ID, `{"method":"totp"}`)
		writeAPIError(w, http.StatusUnauthorized, "invalid_mfa", "invalid authentication code")
		return
	}
	s.mfaIP.reset(ipKey)
	user.TOTPLastStep = step
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
	s.audit(user.Username, "login-mfa", "user", user.ID, `{"method":"totp"}`)
	writeJSON(w, http.StatusOK, map[string]any{"ok": true, "user": userView(user), "csrf": full.csrf})
}

func (s *server) handleRecovery(w http.ResponseWriter, r *http.Request) {
	sess := s.sessionFromRequest(r)
	if sess == nil || !sess.pending {
		writeAPIError(w, http.StatusUnauthorized, "mfa_pending", "password login required")
		return
	}
	ipKey := "ip:" + s.clientIP(r)
	if !s.mfaIP.allow(ipKey) {
		writeAPIError(w, http.StatusTooManyRequests, "rate_limited", "too many attempts")
		return
	}
	var request struct {
		Code string `json:"code"`
	}
	if err := decodeJSON(r, &request); err != nil {
		writeAPIError(w, http.StatusBadRequest, "invalid_json", err.Error())
		return
	}
	user := s.state.findUserByID(sess.userID)
	if user == nil {
		writeAPIError(w, http.StatusUnauthorized, "auth", "unknown user")
		return
	}
	code := strings.ToUpper(strings.TrimSpace(request.Code))
	matched := -1
	for i := range user.RecoveryCodes {
		if user.RecoveryCodes[i].UsedAt != "" {
			continue
		}
		if verifyRecoveryCode(user.RecoveryCodes[i].Hash, code) {
			matched = i
			break
		}
	}
	if matched < 0 {
		s.mfaIP.fail(ipKey)
		s.audit(user.Username, "recovery-failed", "user", user.ID, `{}`)
		writeAPIError(w, http.StatusUnauthorized, "invalid_recovery", "invalid recovery code")
		return
	}
	s.mfaIP.reset(ipKey)
	user.RecoveryCodes[matched].UsedAt = nowISO()
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
	s.audit(user.Username, "recovery-login", "user", user.ID, `{}`)
	writeJSON(w, http.StatusOK, map[string]any{"ok": true, "user": userView(user), "csrf": full.csrf})
}

func (s *server) handleLogout(w http.ResponseWriter, r *http.Request) {
	sess := s.sessionFromRequest(r)
	if sess != nil {
		s.audit(sess.username, "logout", "user", sess.userID, `{}`)
	}
	s.clearSession(w, r)
	writeJSON(w, http.StatusOK, map[string]any{"ok": true})
}

func (s *server) handleSession(w http.ResponseWriter, r *http.Request) {
	sess := s.sessionFromRequest(r)
	if sess == nil || !sess.mfaVerified || sess.pending {
		writeAPIError(w, http.StatusUnauthorized, "auth_required", "login required")
		return
	}
	user := s.state.findUserByID(sess.userID)
	if user == nil {
		writeAPIError(w, http.StatusUnauthorized, "auth_required", "login required")
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{
		"authenticated": true,
		"user":          userView(user),
		"csrf":          sess.csrf,
	})
}

func (s *server) handlePassword(w http.ResponseWriter, r *http.Request) {
	sess := s.sessionFromRequest(r)
	user := s.state.findUserByID(sess.userID)
	if user == nil {
		writeAPIError(w, http.StatusUnauthorized, "auth_required", "login required")
		return
	}
	var request struct {
		Current string `json:"current"`
		New     string `json:"new"`
	}
	if err := decodeJSON(r, &request); err != nil {
		writeAPIError(w, http.StatusBadRequest, "invalid_json", err.Error())
		return
	}
	if !s.verifyCurrentPassword(w, r, user, request.Current) {
		return
	}
	if request.New == request.Current {
		writeAPIError(w, http.StatusBadRequest, "password", "new password must differ from the current password")
		return
	}
	hash, err := hashPassword(request.New)
	if err != nil {
		writeAPIError(w, http.StatusBadRequest, "password", err.Error())
		return
	}
	user.PasswordHash = hash
	user.MustChangePassword = false
	user.touch()
	if err := s.saveState(); err != nil {
		writeAPIError(w, http.StatusInternalServerError, "state", err.Error())
		return
	}
	s.revokeOtherUserSessions(user.ID, sess.tokenHash)
	s.audit(user.Username, "password-change", "user", user.ID, `{}`)
	writeJSON(w, http.StatusOK, map[string]any{"ok": true})
}

func (s *server) handleTOTPSetup(w http.ResponseWriter, r *http.Request) {
	sess := s.sessionFromRequest(r)
	user := s.userForSession(sess)
	if user == nil {
		writeAPIError(w, http.StatusUnauthorized, "auth_required", "login required")
		return
	}
	var request struct {
		Current string `json:"current"`
	}
	if err := decodeJSON(r, &request); err != nil {
		writeAPIError(w, http.StatusBadRequest, "invalid_json", err.Error())
		return
	}
	if !s.verifyCurrentPassword(w, r, user, request.Current) {
		return
	}
	secret, err := generateTOTPSecret()
	if err != nil {
		writeAPIError(w, http.StatusInternalServerError, "totp", err.Error())
		return
	}
	user.TOTPSecret = secret
	user.TOTPConfirmed = false
	user.TOTPLastStep = 0
	user.touch()
	if err := s.saveState(); err != nil {
		writeAPIError(w, http.StatusInternalServerError, "state", err.Error())
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{
		"ok":     true,
		"secret": secret,
		"uri":    totpURI("cmdb-lite", user.Username, secret),
	})
}

func (s *server) handleTOTPConfirm(w http.ResponseWriter, r *http.Request) {
	sess := s.sessionFromRequest(r)
	user := s.state.findUserByID(sess.userID)
	if user == nil || user.TOTPSecret == "" {
		writeAPIError(w, http.StatusBadRequest, "totp", "TOTP setup has not started")
		return
	}
	var request struct {
		Code string `json:"code"`
	}
	if err := decodeJSON(r, &request); err != nil {
		writeAPIError(w, http.StatusBadRequest, "invalid_json", err.Error())
		return
	}
	step, ok := validateTOTP(user.TOTPSecret, request.Code, time.Now())
	if !ok || step <= user.TOTPLastStep {
		writeAPIError(w, http.StatusUnauthorized, "invalid_totp", "invalid authentication code")
		return
	}
	user.TOTPConfirmed = true
	user.TOTPLastStep = step
	user.touch()
	if err := s.saveState(); err != nil {
		writeAPIError(w, http.StatusInternalServerError, "state", err.Error())
		return
	}
	s.audit(user.Username, "totp-enroll", "user", user.ID, `{}`)
	writeJSON(w, http.StatusOK, map[string]any{"ok": true})
}

func (s *server) handleRecoveryGenerate(w http.ResponseWriter, r *http.Request) {
	sess := s.sessionFromRequest(r)
	user := s.userForSession(sess)
	if user == nil {
		writeAPIError(w, http.StatusUnauthorized, "auth_required", "login required")
		return
	}
	var request struct {
		Current string `json:"current"`
	}
	if err := decodeJSON(r, &request); err != nil {
		writeAPIError(w, http.StatusBadRequest, "invalid_json", err.Error())
		return
	}
	if !s.verifyCurrentPassword(w, r, user, request.Current) {
		return
	}
	plain, hashed, err := generateRecoveryCodes()
	if err != nil {
		writeAPIError(w, http.StatusInternalServerError, "recovery", err.Error())
		return
	}
	user.RecoveryCodes = hashed
	user.touch()
	if err := s.saveState(); err != nil {
		writeAPIError(w, http.StatusInternalServerError, "state", err.Error())
		return
	}
	s.audit(user.Username, "recovery-generate", "user", user.ID, `{}`)
	writeJSON(w, http.StatusOK, map[string]any{"ok": true, "codes": plain})
}

func (s *server) handleListUsers(w http.ResponseWriter, r *http.Request) {
	users := make([]map[string]any, 0, len(s.state.Users))
	for i := range s.state.Users {
		users = append(users, userView(&s.state.Users[i]))
	}
	writeJSON(w, http.StatusOK, map[string]any{"ok": true, "users": users})
}

func (s *server) handleCreateUser(w http.ResponseWriter, r *http.Request) {
	var request struct {
		Username string `json:"username"`
		Password string `json:"password"`
		IsAdmin  bool   `json:"is_admin"`
	}
	if err := decodeJSON(r, &request); err != nil {
		writeAPIError(w, http.StatusBadRequest, "invalid_json", err.Error())
		return
	}
	if err := validateUsername(request.Username); err != nil {
		writeAPIError(w, http.StatusBadRequest, "username", err.Error())
		return
	}
	if s.state.findUser(request.Username) != nil {
		writeAPIError(w, http.StatusConflict, "username", "username already exists")
		return
	}
	hash, err := hashPassword(request.Password)
	if err != nil {
		writeAPIError(w, http.StatusBadRequest, "password", err.Error())
		return
	}
	user := userRecord{
		ID:                 newID(),
		Username:           request.Username,
		PasswordHash:       hash,
		IsAdmin:            request.IsAdmin,
		MustChangePassword: true,
		CreatedAt:          nowISO(),
		UpdatedAt:          nowISO(),
	}
	s.state.Users = append(s.state.Users, user)
	if err := s.saveState(); err != nil {
		s.state.Users = s.state.Users[:len(s.state.Users)-1]
		writeAPIError(w, http.StatusInternalServerError, "state", err.Error())
		return
	}
	actor := s.fullSession(r)
	actorName := "system"
	if actor != nil {
		actorName = actor.username
	}
	s.audit(actorName, "user-create", "user", user.ID, `{"username":"`+user.Username+`"}`)
	writeJSON(w, http.StatusOK, map[string]any{"ok": true, "user": userView(&s.state.Users[len(s.state.Users)-1])})
}

func (s *server) handleDisableUser(w http.ResponseWriter, r *http.Request) {
	s.setUserDisabled(w, r, true)
}

func (s *server) handleEnableUser(w http.ResponseWriter, r *http.Request) {
	s.setUserDisabled(w, r, false)
}

func (s *server) setUserDisabled(w http.ResponseWriter, r *http.Request, disabled bool) {
	actor := s.fullSession(r)
	id := r.PathValue("id")
	user := s.state.findUserByID(id)
	if user == nil {
		writeAPIError(w, http.StatusNotFound, "not_found", "user not found")
		return
	}
	if user.IsAdmin && disabled && s.adminCount() <= 1 {
		writeAPIError(w, http.StatusConflict, "last_admin", "cannot disable the last admin")
		return
	}
	user.Disabled = disabled
	user.touch()
	if err := s.saveState(); err != nil {
		writeAPIError(w, http.StatusInternalServerError, "state", err.Error())
		return
	}
	if disabled {
		s.revokeUserSessions(user.ID)
	}
	action := "user-enable"
	if disabled {
		action = "user-disable"
	}
	actorName := "system"
	if actor != nil {
		actorName = actor.username
	}
	s.audit(actorName, action, "user", user.ID, `{"username":"`+user.Username+`"}`)
	writeJSON(w, http.StatusOK, map[string]any{"ok": true, "user": userView(user)})
}

func (s *server) handleResetPassword(w http.ResponseWriter, r *http.Request) {
	actor := s.fullSession(r)
	id := r.PathValue("id")
	user := s.state.findUserByID(id)
	if user == nil {
		writeAPIError(w, http.StatusNotFound, "not_found", "user not found")
		return
	}
	suffix, err := randomHex(8)
	if err != nil {
		writeAPIError(w, http.StatusInternalServerError, "password", err.Error())
		return
	}
	temp := "cmdb-" + suffix
	hash, err := hashPassword(temp)
	if err != nil {
		writeAPIError(w, http.StatusInternalServerError, "password", err.Error())
		return
	}
	user.PasswordHash = hash
	user.MustChangePassword = true
	user.touch()
	if err := s.saveState(); err != nil {
		writeAPIError(w, http.StatusInternalServerError, "state", err.Error())
		return
	}
	s.revokeUserSessions(user.ID)
	actorName := "system"
	if actor != nil {
		actorName = actor.username
	}
	s.audit(actorName, "admin-reset-password", "user", user.ID, `{"username":"`+user.Username+`"}`)
	writeJSON(w, http.StatusOK, map[string]any{"ok": true, "password": temp})
}

func (s *server) handleResetMFA(w http.ResponseWriter, r *http.Request) {
	actor := s.fullSession(r)
	id := r.PathValue("id")
	user := s.state.findUserByID(id)
	if user == nil {
		writeAPIError(w, http.StatusNotFound, "not_found", "user not found")
		return
	}
	user.TOTPSecret = ""
	user.TOTPConfirmed = false
	user.TOTPLastStep = 0
	user.RecoveryCodes = nil
	user.touch()
	if err := s.saveState(); err != nil {
		writeAPIError(w, http.StatusInternalServerError, "state", err.Error())
		return
	}
	s.revokeUserSessions(user.ID)
	actorName := "system"
	if actor != nil {
		actorName = actor.username
	}
	s.audit(actorName, "admin-reset-mfa", "user", user.ID, `{"username":"`+user.Username+`"}`)
	writeJSON(w, http.StatusOK, map[string]any{"ok": true})
}

func (s *server) adminCount() int {
	count := 0
	for i := range s.state.Users {
		if s.state.Users[i].IsAdmin && !s.state.Users[i].Disabled {
			count++
		}
	}
	return count
}

// serverRPCAllowed is an allowlist. New C API methods are not exposed in server
// mode until they are reviewed and added here.
var serverRPCAllowed = map[string]bool{
	"prefs.get":                true,
	"prefs.set":                true,
	"security.info":            true,
	"audit.list":               true,
	"audit.verify":             true,
	"categories.create":        true,
	"categories.delete":        true,
	"categories.list":          true,
	"categories.update":        true,
	"cis.create":               true,
	"cis.delete":               true,
	"cis.get":                  true,
	"cis.list":                 true,
	"cis.update":               true,
	"diagrams.delete":          true,
	"diagrams.get":             true,
	"diagrams.list":            true,
	"diagrams.save":            true,
	"graph.dependencies":       true,
	"import.commit":            true,
	"import.preview":           true,
	"relationships.create":     true,
	"relationships.delete":     true,
	"relationships.list":       true,
	"relationships.set_impact": true,
	"relationships.types":      true,
	"scan.cancel":              true,
	"scan.discard":             true,
	"scan.import":              true,
	"scan.job":                 true,
	"scan.jobs":                true,
	"scan.start":               true,
	"scan.status":              true,
	"types.create":             true,
	"types.delete":             true,
	"types.field_add":          true,
	"types.field_delete":       true,
	"types.field_update":       true,
	"types.get":                true,
	"types.list":               true,
	"types.update":             true,
}

func (s *server) handleRPC(w http.ResponseWriter, r *http.Request) {
	sess := s.sessionFromRequest(r)
	if sess == nil {
		writeAPIError(w, http.StatusUnauthorized, "auth_required", "login required")
		return
	}
	var request struct {
		Method string         `json:"method"`
		Params map[string]any `json:"params"`
	}
	if err := decodeJSONLimit(r, &request, 64<<20); err != nil {
		writeAPIError(w, http.StatusBadRequest, "invalid_json", err.Error())
		return
	}
	if request.Params == nil {
		request.Params = map[string]any{}
	}
	if !serverRPCAllowed[request.Method] {
		writeAPIError(w, http.StatusForbidden, "forbidden", "method is not available in server mode")
		return
	}
	raw, err := s.rpc.call(request.Method, request.Params, sess.username)
	if err != nil {
		writeAPIError(w, http.StatusBadGateway, "rpc", err.Error())
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{
		"ok":     true,
		"result": json.RawMessage(raw),
	})
}

func newID() string {
	raw := make([]byte, 16)
	_, _ = rand.Read(raw)
	raw[6] = (raw[6] & 0x0f) | 0x40
	raw[8] = (raw[8] & 0x3f) | 0x80
	return fmt.Sprintf("%s-%s-%s-%s-%s",
		hex.EncodeToString(raw[0:4]),
		hex.EncodeToString(raw[4:6]),
		hex.EncodeToString(raw[6:8]),
		hex.EncodeToString(raw[8:10]),
		hex.EncodeToString(raw[10:16]))
}

var dummyHash = func() string {
	hash, _ := hashPassword("cmdb-lite-dummy-password-never-used")
	return hash
}()

func dummyPasswordHash() string { return dummyHash }
