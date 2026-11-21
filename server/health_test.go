package main

import (
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
)

func TestHealthEndpoints(t *testing.T) {
	s := &server{sessions: make(map[string]*session)}
	handler := s.handler()
	for _, path := range []string{"/healthz", "/readyz"} {
		request := httptest.NewRequest(http.MethodGet, path, nil)
		recorder := httptest.NewRecorder()
		handler.ServeHTTP(recorder, request)
		if recorder.Code != http.StatusOK {
			t.Fatalf("%s: status %d", path, recorder.Code)
		}
		if !strings.Contains(recorder.Body.String(), `"ok":true`) {
			t.Fatalf("%s: unexpected body %q", path, recorder.Body.String())
		}
		if recorder.Header().Get("Cache-Control") != "no-store" {
			t.Fatalf("%s: expected no-store", path)
		}
	}
}
