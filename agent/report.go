package main

import (
	"bytes"
	"context"
	"crypto/tls"
	"crypto/x509"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"os"
	"strings"
	"time"
)

type HostFacts struct {
	DisplayName   string `json:"display_name"`
	Hostname      string `json:"hostname"`
	MachineID     string `json:"machine_id"`
	OSFamily      string `json:"os_family"`
	OSName        string `json:"os_name"`
	OSVersion     string `json:"os_version"`
	Architecture  string `json:"architecture"`
	AgentVersion  string `json:"agent_version"`
	IPAddress     string `json:"ip_address"`
	SerialNumber  string `json:"serial_number"`
	CPUCores      int    `json:"cpu_cores"`
	MemoryMB      int64  `json:"memory_mb"`
	KernelVersion string `json:"kernel_version"`
}

type Application struct {
	Key             string `json:"key"`
	Name            string `json:"name"`
	Version         string `json:"version"`
	Publisher       string `json:"publisher"`
	Source          string `json:"source"`
	Architecture    string `json:"architecture"`
	InstallDate     string `json:"install_date"`
	InstallLocation string `json:"install_location"`
	SizeKB          int64  `json:"size_kb"`
}

type Collection struct {
	CollectedAt time.Time
	Host        HostFacts
	Apps        []Application
	Errors      []string
	Complete    bool
}

type enrollResponse struct {
	Agent struct {
		ID          string `json:"id"`
		DisplayName string `json:"display_name"`
		Hostname    string `json:"hostname"`
		Status      string `json:"status"`
	} `json:"agent"`
	Token           string `json:"token"`
	Reused          bool   `json:"reused"`
	IntervalSeconds int    `json:"interval_seconds"`
}

type reportResponse struct {
	OK     bool `json:"ok"`
	Result struct {
		Report struct {
			ID           string `json:"id"`
			AppCount     int    `json:"app_count"`
			AddedCount   int    `json:"added_count"`
			RemovedCount int    `json:"removed_count"`
			UpdatedCount int    `json:"updated_count"`
		} `json:"report"`
	} `json:"result"`
}

type apiError struct {
	Status  int
	Code    string
	Message string
}

func (e *apiError) Error() string {
	if e.Code == "" {
		return fmt.Sprintf("server returned HTTP %d", e.Status)
	}
	return fmt.Sprintf("server returned HTTP %d (%s): %s", e.Status, e.Code, e.Message)
}

func isUnauthorized(err error) bool {
	var apiErr *apiError
	return errors.As(err, &apiErr) && apiErr.Status == http.StatusUnauthorized
}

type agentClient struct {
	baseURL      string
	http         *http.Client
	agentVersion string
}

func newAgentClient(serverURL, caFile, agentVersion string) (*agentClient, error) {
	base, err := normalizeServerURL(serverURL)
	if err != nil {
		return nil, err
	}
	parsed, err := url.Parse(base)
	if err != nil {
		return nil, err
	}
	if parsed.Scheme != "https" && !(parsed.Scheme == "http" && isLoopbackHost(parsed.Hostname())) {
		return nil, errors.New("agent reporting requires HTTPS")
	}
	roots, err := x509.SystemCertPool()
	if err != nil {
		return nil, fmt.Errorf("load system certificate store: %w", err)
	}
	if roots == nil {
		roots = x509.NewCertPool()
	}
	if caFile != "" {
		pem, err := os.ReadFile(caFile)
		if err != nil {
			return nil, fmt.Errorf("read CA file: %w", err)
		}
		if !roots.AppendCertsFromPEM(pem) {
			return nil, errors.New("CA file contains no usable certificates")
		}
	}
	transport := &http.Transport{
		Proxy: http.ProxyFromEnvironment,
		TLSClientConfig: &tls.Config{
			MinVersion: tls.VersionTLS12,
			RootCAs:    roots,
		},
		ForceAttemptHTTP2: true,
	}
	return &agentClient{
		baseURL: base,
		http: &http.Client{
			Transport: transport,
			Timeout:   60 * time.Second,
			CheckRedirect: func(_ *http.Request, _ []*http.Request) error {
				return http.ErrUseLastResponse
			},
		},
		agentVersion: agentVersion,
	}, nil
}

func (c *agentClient) post(ctx context.Context, path, token string,
	payload any, response any, maxResponse int64) error {
	body, err := json.Marshal(payload)
	if err != nil {
		return err
	}
	request, err := http.NewRequestWithContext(ctx, http.MethodPost,
		joinServerPath(c.baseURL, path), bytes.NewReader(body))
	if err != nil {
		return err
	}
	request.Header.Set("Content-Type", "application/json")
	request.Header.Set("Accept", "application/json")
	request.Header.Set("User-Agent", "cmdb-agent/"+c.agentVersion)
	if token != "" {
		request.Header.Set("Authorization", "Bearer "+token)
	}
	httpResponse, err := c.http.Do(request)
	if err != nil {
		return err
	}
	defer httpResponse.Body.Close()
	data, err := io.ReadAll(io.LimitReader(httpResponse.Body, maxResponse+1))
	if err != nil {
		return err
	}
	if int64(len(data)) > maxResponse {
		return errors.New("server response is too large")
	}
	if httpResponse.StatusCode < 200 || httpResponse.StatusCode >= 300 {
		var envelope struct {
			Error struct {
				Code    string `json:"code"`
				Message string `json:"message"`
			} `json:"error"`
		}
		_ = json.Unmarshal(data, &envelope)
		return &apiError{
			Status:  httpResponse.StatusCode,
			Code:    envelope.Error.Code,
			Message: strings.TrimSpace(envelope.Error.Message),
		}
	}
	if response == nil {
		return nil
	}
	if err := json.Unmarshal(data, response); err != nil {
		return fmt.Errorf("decode server response: %w", err)
	}
	return nil
}

func (c *agentClient) enroll(ctx context.Context, enrollmentToken string,
	host HostFacts) (*enrollResponse, error) {
	payload := map[string]any{
		"token": enrollmentToken,
		"host":  host,
	}
	var response enrollResponse
	if err := c.post(ctx, "/api/v1/agent/enroll", "", payload, &response,
		1<<20); err != nil {
		return nil, err
	}
	if response.Token == "" || !validTokenFormat(response.Token) {
		return nil, errors.New("server returned an invalid agent token")
	}
	if response.Agent.ID == "" {
		return nil, errors.New("server returned an invalid agent identity")
	}
	if response.IntervalSeconds < 900 {
		response.IntervalSeconds = 24 * 60 * 60
	}
	return &response, nil
}

func (c *agentClient) report(ctx context.Context, token string,
	collection Collection) (*reportResponse, error) {
	errorsText := strings.Join(collection.Errors, "; ")
	if len(errorsText) > 2047 {
		errorsText = errorsText[:2047]
	}
	payload := map[string]any{
		"collected_at":     collection.CollectedAt.UTC().Format(time.RFC3339),
		"host":             collection.Host,
		"apps":             collection.Apps,
		"collector_errors": errorsText,
		"complete":         collection.Complete,
	}
	var response reportResponse
	if err := c.post(ctx, "/api/v1/agent/report", token, payload, &response,
		8<<20); err != nil {
		return nil, err
	}
	if !response.OK {
		return nil, errors.New("server did not accept the report")
	}
	return &response, nil
}
