package main

import (
	"testing"
	"time"
)

func TestPasswordHashAndVerify(t *testing.T) {
	hash, err := hashPassword("correct horse battery staple")
	if err != nil {
		t.Fatal(err)
	}
	if !verifyPassword(hash, "correct horse battery staple") {
		t.Fatal("correct password rejected")
	}
	if verifyPassword(hash, "wrong password") {
		t.Fatal("wrong password accepted")
	}
	if _, err := hashPassword("short"); err == nil {
		t.Fatal("short password accepted")
	}
}

func TestTOTP(t *testing.T) {
	secret, err := generateTOTPSecret()
	if err != nil {
		t.Fatal(err)
	}
	now := time.Unix(1_700_000_000, 0)
	code, step, err := hotpForTest(secret, now)
	if err != nil {
		t.Fatal(err)
	}
	gotStep, ok := validateTOTP(secret, code, now)
	if !ok || gotStep != step {
		t.Fatalf("TOTP rejected: got %d want %d", gotStep, step)
	}
	if _, ok := validateTOTP(secret, "000000", now); ok {
		t.Fatal("invalid TOTP accepted")
	}
}

func hotpForTest(secret string, at time.Time) (string, int64, error) {
	key, err := decodeTOTPSecret(secret)
	if err != nil {
		return "", 0, err
	}
	step := at.Unix() / totpPeriod
	code, err := hotp(key, uint64(step))
	return code, step, err
}

func TestRecoveryCodes(t *testing.T) {
	plain, hashed, err := generateRecoveryCodes()
	if err != nil {
		t.Fatal(err)
	}
	if len(plain) != recoveryCodeCount || len(hashed) != recoveryCodeCount {
		t.Fatal("unexpected recovery-code count")
	}
	if !verifyRecoveryCode(hashed[0].Hash, plain[0]) {
		t.Fatal("recovery code rejected")
	}
	if verifyRecoveryCode(hashed[0].Hash, plain[1]) {
		t.Fatal("wrong recovery code accepted")
	}
}
