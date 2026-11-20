//go:build demo

package main

import (
	"bytes"
	"encoding/json"
	"net/http"
	"os"
	"strconv"
	"strings"
	"time"
)

// Demo builds are only used by the public demo image. The production build
// uses demo_stub.go and never contains the banner, credentials, or relaxed
// login behavior.
const (
	demoLoginIPLimit   = 300
	demoLoginUserLimit = 1000
	demoMFAIPLimit     = 300
	demoReauthLimit    = 100
)

type demoCredentials struct {
	Username      string   `json:"username"`
	Password      string   `json:"password"`
	TOTPSecret    string   `json:"totp_secret"`
	RecoveryCodes []string `json:"recovery_codes"`
}

func demoCredentialsPath() string {
	if path := strings.TrimSpace(os.Getenv("CMDB_DEMO_CREDENTIALS")); path != "" {
		return path
	}
	return "/data/demo-credentials.json"
}

func readDemoCredentials() demoCredentials {
	info := demoCredentials{Username: "guest", Password: "guest"}
	data, err := os.ReadFile(demoCredentialsPath())
	if err == nil {
		_ = json.Unmarshal(data, &info)
	}
	if strings.TrimSpace(info.Username) == "" {
		info.Username = "guest"
	}
	return info
}

func demoUsername() string {
	return readDemoCredentials().Username
}

func demoAllowTOTPReplay(username string) bool {
	return strings.EqualFold(username, demoUsername())
}

func demoBlockCredentialChange(w http.ResponseWriter, user *userRecord) bool {
	if user == nil || !strings.EqualFold(user.Username, demoUsername()) {
		return false
	}
	writeAPIError(w, http.StatusForbidden, "demo_locked",
		"The demo account is protected. Its password, TOTP, recovery codes, and enabled state are fixed; inventory and user-management flows remain available.")
	return true
}

func demoRegisterRoutes(mux *http.ServeMux, s *server) {
	mux.HandleFunc("GET /api/v1/demo/status", s.handleDemoStatus)
}

func (s *server) handleDemoStatus(w http.ResponseWriter, r *http.Request) {
	info := readDemoCredentials()
	secret := info.TOTPSecret
	if user := s.state.findUser(info.Username); user != nil && user.TOTPSecret != "" {
		secret = user.TOTPSecret
	}
	code := ""
	if key, err := decodeTOTPSecret(secret); err == nil {
		code, _ = hotp(key, uint64(time.Now().Unix()/totpPeriod))
	}
	resetHours := 24
	if value := strings.TrimSpace(os.Getenv("CMDB_DEMO_RESET_HOURS")); value != "" {
		if parsed, err := strconv.Atoi(value); err == nil && parsed >= 0 {
			resetHours = parsed
		}
	}
	w.Header().Set("Cache-Control", "no-store")
	writeJSON(w, http.StatusOK, map[string]any{
		"username":       info.Username,
		"password":       info.Password,
		"totp_secret":    secret,
		"totp_code":      code,
		"totp_period":    totpPeriod,
		"recovery_codes": info.RecoveryCodes,
		"reset_hours":    resetHours,
	})
}

func demoLoginPage(data []byte) []byte {
	return bytes.Replace(data, []byte("<!-- DEMO_BANNER -->"),
		[]byte(demoBannerHTML), 1)
}

const demoBannerHTML = `
<div class="demo-panel">
  <div class="demo-title">Public demo &mdash; fake data only</div>
  <p class="demo-note">Everything here is simulated and the database resets periodically (24 hours by default). Do not enter real information. Credential changes for the demo account are disabled.</p>
  <div class="demo-row"><span>Username</span><code id="demo-user">guest</code></div>
  <div class="demo-row"><span>Password</span><code id="demo-pass">guest</code></div>
  <div class="demo-row"><span>TOTP secret</span><code id="demo-secret">loading&hellip;</code></div>
  <div class="demo-row"><span>Current TOTP</span><code id="demo-code">loading&hellip;</code></div>
  <div class="demo-actions">
    <button id="demo-fill" type="button" class="secondary">Fill login</button>
    <button id="demo-code-use" type="button" class="secondary">Use current code</button>
  </div>
  <details>
    <summary>Demo recovery codes (single-use)</summary>
    <ul id="demo-recovery" class="demo-codes"></ul>
  </details>
</div>
<style>
.demo-panel { margin: .9rem 0 1rem; padding: .8rem; border: 1px solid #3b6b78; border-radius: 10px; background: #0e2430; color: #e7f1f7; font-size: .82rem; }
.demo-title { font-weight: 700; color: #2dd4bf; margin-bottom: .25rem; }
.demo-note { margin: 0 0 .55rem; color: #9bb4c5; font-size: .78rem; }
.demo-row { display: flex; justify-content: space-between; gap: .5rem; padding: .15rem 0; color: #9bb4c5; }
.demo-row code { color: #e7f1f7; font-family: ui-monospace, monospace; text-align: right; word-break: break-all; }
.demo-actions { display: flex; gap: .4rem; margin-top: .55rem; }
.demo-actions button { width: auto; flex: 1; margin: 0; padding: .4rem .5rem; font-size: .78rem; }
.demo-panel details { margin-top: .55rem; color: #9bb4c5; }
.demo-panel summary { cursor: pointer; }
.demo-codes { margin: .4rem 0 0; padding-left: 1.1rem; columns: 2; font-family: ui-monospace, monospace; color: #e7f1f7; }
</style>
<script>
(function () {
  const byId = (id) => document.getElementById(id);
  let demo = null;
  function render(data) {
    demo = data;
    byId("demo-user").textContent = data.username || "guest";
    byId("demo-pass").textContent = data.password || "guest";
    byId("demo-secret").textContent = data.totp_secret || "unavailable";
    byId("demo-code").textContent = data.totp_code || "unavailable";
    const list = byId("demo-recovery");
    list.textContent = "";
    for (const code of data.recovery_codes || []) {
      const item = document.createElement("li");
      item.textContent = code;
      list.appendChild(item);
    }
  }
  function refresh() {
    fetch("/api/v1/demo/status", { cache: "no-store" })
      .then((response) => response.ok ? response.json() : Promise.reject())
      .then(render)
      .catch(() => { byId("demo-code").textContent = "unavailable"; });
  }
  window.addEventListener("DOMContentLoaded", function () {
    refresh();
    setInterval(refresh, 5000);
    byId("demo-fill").addEventListener("click", function () {
      if (!demo) { return; }
      byId("login-user").value = demo.username || "guest";
      byId("login-pass").value = demo.password || "guest";
      byId("login-user").focus();
    });
    byId("demo-code-use").addEventListener("click", function () {
      byId("mfa-code").value = byId("demo-code").textContent;
      byId("mfa-code").focus();
    });
  });
})();
</script>
`
