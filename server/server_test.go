package main

import (
	"net"
	"net/http/httptest"
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
