//go:build !demo

package main

import "net/http"

// Production defaults. The demo build replaces these values in demo.go.
const (
	demoLoginIPLimit   = 10
	demoLoginUserLimit = 5
	demoMFAIPLimit     = 20
	demoReauthLimit    = 10
)

func demoLoginPage(data []byte) []byte {
	return data
}

func demoRegisterRoutes(*http.ServeMux, *server) {}

func demoAllowTOTPReplay(string) bool {
	return false
}

func demoBlockCredentialChange(http.ResponseWriter, *userRecord) bool {
	return false
}
