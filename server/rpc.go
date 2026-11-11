package main

import (
	"bufio"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"os/exec"
	"sync"
)

// rpcClient owns the cmdb-rpc child process. The C core is single-connection
// and not thread-safe, so every call is serialized.
type rpcClient struct {
	cmd    *exec.Cmd
	stdin  io.WriteCloser
	stdout *bufio.Reader
	mu     sync.Mutex
	next   int
}

func startRPC(path string) (*rpcClient, error) {
	cmd := exec.Command(path)
	cmd.Stderr = os.Stderr
	stdin, err := cmd.StdinPipe()
	if err != nil {
		return nil, err
	}
	stdout, err := cmd.StdoutPipe()
	if err != nil {
		return nil, err
	}
	if err := cmd.Start(); err != nil {
		return nil, err
	}
	return &rpcClient{cmd: cmd, stdin: stdin, stdout: bufio.NewReaderSize(stdout, 64*1024)}, nil
}

func (r *rpcClient) call(method string, params map[string]any, actor string) (json.RawMessage, error) {
	r.mu.Lock()
	defer r.mu.Unlock()
	r.next++
	request := map[string]any{
		"id":     r.next,
		"method": method,
		"params": params,
		"actor":  actor,
	}
	payload, err := json.Marshal(request)
	if err != nil {
		return nil, err
	}
	if _, err := r.stdin.Write(append(payload, '\n')); err != nil {
		return nil, err
	}
	line, err := r.stdout.ReadBytes('\n')
	if err != nil {
		return nil, err
	}
	var response map[string]any
	if err := json.Unmarshal(line, &response); err != nil {
		return nil, fmt.Errorf("invalid RPC response: %w", err)
	}
	ok, _ := response["ok"].(bool)
	if !ok {
		message := "RPC request failed"
		if e, exists := response["error"].(map[string]any); exists {
			if m, exists := e["message"].(string); exists && m != "" {
				message = m
			}
			if d, exists := e["detail"].(string); exists && d != "" {
				message += ": " + d
			}
		}
		return nil, fmt.Errorf("%s", message)
	}
	result, exists := response["result"]
	if !exists {
		return json.RawMessage("null"), nil
	}
	return json.Marshal(result)
}

func (r *rpcClient) callResult(method string, params map[string]any, actor string, out any) error {
	raw, err := r.call(method, params, actor)
	if err != nil {
		return err
	}
	return json.Unmarshal(raw, out)
}

func (r *rpcClient) close() {
	if r == nil {
		return
	}
	_ = r.stdin.Close()
	_ = r.cmd.Wait()
}
