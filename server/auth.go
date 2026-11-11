package main

import (
	"crypto/hmac"
	"crypto/rand"
	"crypto/sha1"
	"crypto/sha256"
	"crypto/subtle"
	"encoding/base32"
	"encoding/base64"
	"encoding/binary"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"net/url"
	"regexp"
	"strconv"
	"strings"
	"time"
	"unicode/utf8"

	"golang.org/x/crypto/argon2"
)

const (
	passwordMinLength  = 12
	passwordMaxLength  = 1024
	passwordSaltLen    = 16
	passwordKeyLen     = 32
	passwordTime       = 3
	passwordMemory     = 64 * 1024
	passwordThreads    = 2
	maxPasswordMemory  = 256 * 1024
	maxPasswordTime    = 10
	maxPasswordThreads = 16

	totpPeriod    = 30
	totpDigits    = 6
	totpSkew      = 1
	totpSecretLen = 20

	recoveryCodeCount = 10
)

var usernamePattern = regexp.MustCompile(`^[A-Za-z0-9][A-Za-z0-9_.-]{2,63}$`)

type recoveryCode struct {
	Hash   string `json:"hash"`
	UsedAt string `json:"used_at,omitempty"`
}

type userRecord struct {
	ID                 string         `json:"id"`
	Username           string         `json:"username"`
	PasswordHash       string         `json:"password_hash"`
	IsAdmin            bool           `json:"is_admin"`
	Disabled           bool           `json:"disabled"`
	MustChangePassword bool           `json:"must_change_password,omitempty"`
	TOTPSecret         string         `json:"totp_secret,omitempty"`
	TOTPConfirmed      bool           `json:"totp_confirmed"`
	TOTPLastStep       int64          `json:"totp_last_step"`
	RecoveryCodes      []recoveryCode `json:"recovery_codes,omitempty"`
	CreatedAt          string         `json:"created_at"`
	UpdatedAt          string         `json:"updated_at"`
}

type authState struct {
	Version int          `json:"version"`
	Users   []userRecord `json:"users"`
}

func validateUsername(username string) error {
	if username != strings.TrimSpace(username) || !usernamePattern.MatchString(username) {
		return errors.New("username must be 3-64 characters and start with a letter or digit")
	}
	return nil
}

func validatePassword(password string) error {
	if utf8.RuneCountInString(password) < passwordMinLength {
		return fmt.Errorf("password must be at least %d characters", passwordMinLength)
	}
	if len(password) > passwordMaxLength {
		return fmt.Errorf("password must be at most %d bytes", passwordMaxLength)
	}
	return nil
}

func hashPassword(password string) (string, error) {
	if err := validatePassword(password); err != nil {
		return "", err
	}
	salt := make([]byte, passwordSaltLen)
	if _, err := rand.Read(salt); err != nil {
		return "", err
	}
	key := argon2.IDKey([]byte(password), salt, passwordTime, passwordMemory, passwordThreads, passwordKeyLen)
	return fmt.Sprintf("$argon2id$v=19$m=%d,t=%d,p=%d$%s$%s",
		passwordMemory, passwordTime, passwordThreads,
		base64.RawStdEncoding.EncodeToString(salt),
		base64.RawStdEncoding.EncodeToString(key)), nil
}

func verifyPassword(encoded, password string) bool {
	parts := strings.Split(encoded, "$")
	if len(parts) != 6 || parts[0] != "" || parts[1] != "argon2id" || parts[2] != "v=19" {
		return false
	}
	var memory, iterations, threads int
	if _, err := fmt.Sscanf(parts[3], "m=%d,t=%d,p=%d", &memory, &iterations, &threads); err != nil {
		return false
	}
	if memory < 64*1024 || memory > maxPasswordMemory ||
		iterations < 1 || iterations > maxPasswordTime ||
		threads < 1 || threads > maxPasswordThreads {
		return false
	}
	salt, ok := decodeBase64(parts[4])
	if !ok || len(salt) < 8 {
		return false
	}
	want, ok := decodeBase64(parts[5])
	if !ok || len(want) < 16 || len(want) > 64 {
		return false
	}
	got := argon2.IDKey([]byte(password), salt, uint32(iterations), uint32(memory), uint8(threads), uint32(len(want)))
	return subtle.ConstantTimeCompare(got, want) == 1
}

func decodeBase64(value string) ([]byte, bool) {
	if decoded, err := base64.RawStdEncoding.DecodeString(value); err == nil {
		return decoded, true
	}
	if decoded, err := base64.StdEncoding.DecodeString(value); err == nil {
		return decoded, true
	}
	return nil, false
}

func randomHex(bytes int) (string, error) {
	buffer := make([]byte, bytes)
	if _, err := rand.Read(buffer); err != nil {
		return "", err
	}
	return hex.EncodeToString(buffer), nil
}

func sha256Hex(value string) string {
	sum := sha256.Sum256([]byte(value))
	return hex.EncodeToString(sum[:])
}

func hashRecoveryCode(code string) (string, error) {
	// Recovery codes are high-entropy but still hashed with Argon2id.
	return hashPassword("recovery:" + code)
}

func verifyRecoveryCode(encoded, code string) bool {
	return verifyPassword(encoded, "recovery:"+code)
}

func generateRecoveryCodes() ([]string, []recoveryCode, error) {
	plain := make([]string, 0, recoveryCodeCount)
	hashed := make([]recoveryCode, 0, recoveryCodeCount)
	for i := 0; i < recoveryCodeCount; i++ {
		raw := make([]byte, 8)
		if _, err := rand.Read(raw); err != nil {
			return nil, nil, err
		}
		code := strings.ToUpper(base32.StdEncoding.WithPadding(base32.NoPadding).EncodeToString(raw))
		if len(code) > 12 {
			code = code[:12]
		}
		hash, err := hashRecoveryCode(code)
		if err != nil {
			return nil, nil, err
		}
		plain = append(plain, code)
		hashed = append(hashed, recoveryCode{Hash: hash})
	}
	return plain, hashed, nil
}

func generateTOTPSecret() (string, error) {
	secret := make([]byte, totpSecretLen)
	if _, err := rand.Read(secret); err != nil {
		return "", err
	}
	return base32.StdEncoding.WithPadding(base32.NoPadding).EncodeToString(secret), nil
}

func totpURI(issuer, account, secret string) string {
	values := url.Values{}
	values.Set("secret", secret)
	values.Set("issuer", issuer)
	values.Set("algorithm", "SHA1")
	values.Set("digits", strconv.Itoa(totpDigits))
	values.Set("period", strconv.Itoa(totpPeriod))
	return "otpauth://totp/" + url.PathEscape(issuer+":"+account) + "?" + values.Encode()
}

func validateTOTP(secret, code string, at time.Time) (int64, bool) {
	code = strings.TrimSpace(code)
	if len(code) != totpDigits {
		return 0, false
	}
	for _, character := range code {
		if character < '0' || character > '9' {
			return 0, false
		}
	}
	key, err := decodeTOTPSecret(secret)
	if err != nil {
		return 0, false
	}
	current := at.Unix() / totpPeriod
	for offset := -totpSkew; offset <= totpSkew; offset++ {
		step := current + int64(offset)
		if step < 0 {
			continue
		}
		expected, err := hotp(key, uint64(step))
		if err != nil {
			continue
		}
		if subtle.ConstantTimeCompare([]byte(expected), []byte(code)) == 1 {
			return step, true
		}
	}
	return 0, false
}

func decodeTOTPSecret(secret string) ([]byte, error) {
	normalized := strings.ToUpper(strings.ReplaceAll(strings.TrimSpace(secret), " ", ""))
	normalized = strings.TrimRight(normalized, "=")
	key, err := base32.StdEncoding.WithPadding(base32.NoPadding).DecodeString(normalized)
	if err != nil || len(key) < 10 {
		return nil, errors.New("invalid TOTP secret")
	}
	return key, nil
}

func hotp(key []byte, counter uint64) (string, error) {
	mac := hmac.New(sha1.New, key)
	var payload [8]byte
	binary.BigEndian.PutUint64(payload[:], counter)
	if _, err := mac.Write(payload[:]); err != nil {
		return "", err
	}
	sum := mac.Sum(nil)
	offset := sum[len(sum)-1] & 0x0f
	value := (uint32(sum[offset]&0x7f) << 24) |
		(uint32(sum[offset+1]) << 16) |
		(uint32(sum[offset+2]) << 8) |
		uint32(sum[offset+3])
	return fmt.Sprintf("%0*d", totpDigits, value%1_000_000), nil
}

func nowISO() string {
	return time.Now().UTC().Format("2006-01-02T15:04:05Z")
}

func (u *userRecord) touch() {
	u.UpdatedAt = nowISO()
}

func (s *authState) findUser(username string) *userRecord {
	for i := range s.Users {
		if strings.EqualFold(s.Users[i].Username, username) {
			return &s.Users[i]
		}
	}
	return nil
}

func (s *authState) findUserByID(id string) *userRecord {
	for i := range s.Users {
		if s.Users[i].ID == id {
			return &s.Users[i]
		}
	}
	return nil
}

func (s *authState) json() (string, error) {
	payload, err := json.Marshal(s)
	if err != nil {
		return "", err
	}
	return string(payload), nil
}
