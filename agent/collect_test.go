package main

import (
	"strings"
	"testing"
)

func TestCleanText(t *testing.T) {
	got := cleanText("  My\nApp\tName  ", 64)
	if got != "My App Name" {
		t.Fatalf("cleanText = %q", got)
	}
	if got := cleanText("abcdef", 4); got != "abcd" {
		t.Fatalf("truncated cleanText = %q", got)
	}
	if got := cleanText("a\x00b", 10); got != "a b" {
		t.Fatalf("control character cleanText = %q", got)
	}
	if got := cleanText("a\u202eb", 10); got != "a b" {
		t.Fatalf("format character cleanText = %q", got)
	}
}

func TestCleanSerial(t *testing.T) {
	if got := cleanSerial("  SN-1234  "); got != "SN-1234" {
		t.Fatalf("cleanSerial = %q", got)
	}
	for _, placeholder := range []string{"", "None", "unknown", "To be filled by O.E.M."} {
		if got := cleanSerial(placeholder); got != "" {
			t.Fatalf("cleanSerial(%q) = %q", placeholder, got)
		}
	}
}

func TestFinishApplications(t *testing.T) {
	apps, errs := finishApplications([]Application{
		{Key: "b", Name: "Beta", Version: "2"},
		{Key: "a", Name: "alpha", Version: "1"},
		{Key: "a", Name: "duplicate", Version: "9"},
		{Key: "", Name: "no key"},
		{Key: "c", Name: ""},
	}, []string{"warning"})
	if len(apps) != 2 {
		t.Fatalf("got %d apps, want 2: %#v", len(apps), apps)
	}
	if apps[0].Name != "alpha" || apps[1].Name != "Beta" {
		t.Fatalf("apps are not sorted: %#v", apps)
	}
	if len(errs) != 2 || errs[0] != "warning" {
		t.Fatalf("errors = %#v", errs)
	}
}

func TestTokenFormat(t *testing.T) {
	if !validTokenFormat(strings.Repeat("ab", 32)) {
		t.Fatal("valid token rejected")
	}
	if validTokenFormat("abc") {
		t.Fatal("short token accepted")
	}
	if validTokenFormat(strings.Repeat("zz", 32)) {
		t.Fatal("non-hex token accepted")
	}
}

func TestNormalizeServerURL(t *testing.T) {
	if got, err := normalizeServerURL("https://cmdb.example.internal/"); err != nil || got != "https://cmdb.example.internal" {
		t.Fatalf("normalize = %q, %v", got, err)
	}
	if _, err := normalizeServerURL("http://cmdb.example.internal"); err == nil {
		t.Fatal("remote plain HTTP accepted")
	}
	if _, err := normalizeServerURL("http://127.0.0.1:8080"); err != nil {
		t.Fatalf("loopback HTTP rejected: %v", err)
	}
	if _, err := normalizeServerURL("https://user:pass@example.com"); err == nil {
		t.Fatal("URL credentials accepted")
	}
}
