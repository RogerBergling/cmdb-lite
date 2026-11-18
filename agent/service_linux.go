//go:build linux

package main

import (
	"errors"
	"fmt"
	"log"
	"os"
	"os/exec"
)

func isWindowsService() (bool, error) {
	return false, nil
}

func runWindowsService(_ string) error {
	return errors.New("Windows service mode is not available on Linux")
}

func startService(configPath string) error {
	if os.Geteuid() != 0 {
		log.Printf("not running as root; start the service manually: systemctl enable --now %s", serviceName)
		return nil
	}
	systemctl, err := exec.LookPath("systemctl")
	if err != nil {
		log.Printf("systemd not found; start %s with the installed service manager", serviceName)
		return nil
	}
	command := exec.Command(systemctl, "enable", "--now", serviceName+".service")
	if output, err := command.CombinedOutput(); err != nil {
		return fmt.Errorf("systemctl enable --now %s.service: %w: %s",
			serviceName, err, string(output))
	}
	_ = configPath
	return nil
}

func installService(_ string) error {
	return errors.New("Linux service installation is handled by the cmdb-lite agent package")
}

func uninstallService() error {
	return errors.New("Linux service removal is handled by the cmdb-lite agent package")
}

func startInstalledService() error {
	systemctl, err := exec.LookPath("systemctl")
	if err != nil {
		return errors.New("systemd is not available")
	}
	if output, err := exec.Command(systemctl, "start", serviceName+".service").CombinedOutput(); err != nil {
		return fmt.Errorf("systemctl start %s.service: %w: %s",
			serviceName, err, string(output))
	}
	return nil
}

func stopInstalledService() error {
	systemctl, err := exec.LookPath("systemctl")
	if err != nil {
		return errors.New("systemd is not available")
	}
	if output, err := exec.Command(systemctl, "stop", serviceName+".service").CombinedOutput(); err != nil {
		return fmt.Errorf("systemctl stop %s.service: %w: %s",
			serviceName, err, string(output))
	}
	return nil
}
