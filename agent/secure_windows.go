//go:build windows

package main

import (
	"fmt"
	"os/exec"
)

func runIcacls(args ...string) error {
	command := exec.Command("icacls", args...)
	if output, err := command.CombinedOutput(); err != nil {
		return fmt.Errorf("set Windows file permissions: %w: %s", err, string(output))
	}
	return nil
}

func secureConfigDirectory(dir string, _ bool) error {
	return runIcacls(dir,
		"/inheritance:r",
		"/grant:r", "*S-1-5-18:(OI)(CI)F",
		"*S-1-5-32-544:(OI)(CI)F")
}

func secureConfigFile(path string, _ bool) error {
	return runIcacls(path,
		"/inheritance:r",
		"/grant:r", "*S-1-5-18:F",
		"*S-1-5-32-544:F")
}
