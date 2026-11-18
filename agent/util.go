package main

import (
	"encoding/hex"
	"errors"
	"net"
	"net/url"
	"strings"
)

func parseURL(raw string) (*url.URL, error) {
	parsed, err := url.Parse(raw)
	if err != nil {
		return nil, errors.New("invalid server URL")
	}
	if parsed.Scheme == "" || parsed.Host == "" {
		return nil, errors.New("server URL must include a scheme and host")
	}
	return parsed, nil
}

func isLoopbackHost(host string) bool {
	if strings.EqualFold(host, "localhost") {
		return true
	}
	ip := net.ParseIP(host)
	return ip != nil && ip.IsLoopback()
}

func validTokenFormat(value string) bool {
	if len(value) != 64 {
		return false
	}
	_, err := hex.DecodeString(value)
	return err == nil
}

func joinServerPath(base, path string) string {
	return strings.TrimRight(base, "/") + path
}
