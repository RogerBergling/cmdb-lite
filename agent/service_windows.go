//go:build windows

package main

import (
	"context"
	"errors"
	"fmt"
	"log"
	"os"
	"time"

	"golang.org/x/sys/windows"
	"golang.org/x/sys/windows/svc"
	"golang.org/x/sys/windows/svc/mgr"
)

type agentService struct {
	configPath string
}

func isWindowsService() (bool, error) {
	return svc.IsWindowsService()
}

func runWindowsService(configPath string) error {
	return svc.Run(serviceName, &agentService{configPath: configPath})
}

func (s *agentService) Execute(_ []string, requests <-chan svc.ChangeRequest,
	changes chan<- svc.Status) (bool, uint32) {
	changes <- svc.Status{State: svc.StartPending}
	cfg, err := loadConfig(s.configPath)
	if err != nil {
		log.Printf("load configuration: %v", err)
		return false, 1
	}
	client, err := newAgentClient(cfg.ServerURL, cfg.CAFile, version)
	if err != nil {
		log.Printf("create agent client: %v", err)
		return false, 1
	}
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	done := make(chan struct{})
	go func() {
		defer close(done)
		if err := runLoop(ctx, cfg, client); err != nil {
			log.Printf("reporting loop stopped: %v", err)
		}
	}()
	changes <- svc.Status{State: svc.Running, Accepts: svc.AcceptStop | svc.AcceptShutdown}
	for {
		select {
		case request := <-requests:
			switch request.Cmd {
			case svc.Stop, svc.Shutdown:
				changes <- svc.Status{State: svc.StopPending}
				cancel()
				select {
				case <-done:
				case <-time.After(30 * time.Second):
					log.Printf("reporting loop did not stop within 30 seconds")
				}
				return false, 0
			}
		case <-done:
			return false, 1
		}
	}
}

func startService(configPath string) error {
	return installAndStartService(configPath)
}

func installAndStartService(configPath string) error {
	manager, err := mgr.Connect()
	if err != nil {
		return fmt.Errorf("connect to the Windows service manager: %w", err)
	}
	defer manager.Disconnect()
	service, err := manager.OpenService(serviceName)
	if err == nil {
		defer service.Close()
		if err := service.Start(); err != nil {
			status, queryErr := service.Query()
			if queryErr != nil || status.State != svc.Running {
				return fmt.Errorf("start %s: %w", serviceName, err)
			}
		}
		return nil
	}
	executable, err := os.Executable()
	if err != nil {
		return fmt.Errorf("locate cmdb-agent executable: %w", err)
	}
	config := mgr.Config{
		DisplayName: "cmdb-lite agent",
		Description: "Reports installed application inventory to a cmdb-lite server.",
		StartType:   mgr.StartAutomatic,
	}
	service, err = manager.CreateService(serviceName, executable, config,
		"run", "--config", configPath)
	if err != nil {
		return fmt.Errorf("create %s service: %w", serviceName, err)
	}
	defer service.Close()
	if err := service.Start(); err != nil {
		return fmt.Errorf("start %s: %w", serviceName, err)
	}
	return nil
}

func installService(configPath string) error {
	return installAndStartService(configPath)
}

func uninstallService() error {
	manager, err := mgr.Connect()
	if err != nil {
		return fmt.Errorf("connect to the Windows service manager: %w", err)
	}
	defer manager.Disconnect()
	service, err := manager.OpenService(serviceName)
	if err != nil {
		// Treat a missing service as already removed. This keeps MSI
		// uninstall and repeated manual cleanup safe.
		if errors.Is(err, windows.ERROR_SERVICE_DOES_NOT_EXIST) {
			return nil
		}
		return fmt.Errorf("open %s service: %w", serviceName, err)
	}
	defer service.Close()
	status, err := service.Control(svc.Stop)
	if err == nil {
		_ = status
		deadline := time.Now().Add(20 * time.Second)
		for time.Now().Before(deadline) {
			current, queryErr := service.Query()
			if queryErr != nil || current.State == svc.Stopped {
				break
			}
			time.Sleep(500 * time.Millisecond)
		}
	}
	if err := service.Delete(); err != nil {
		return fmt.Errorf("delete %s service: %w", serviceName, err)
	}
	return nil
}

func startInstalledService() error {
	manager, err := mgr.Connect()
	if err != nil {
		return fmt.Errorf("connect to the Windows service manager: %w", err)
	}
	defer manager.Disconnect()
	service, err := manager.OpenService(serviceName)
	if err != nil {
		if errors.Is(err, windows.ERROR_SERVICE_DOES_NOT_EXIST) {
			return nil
		}
		return fmt.Errorf("open %s service: %w", serviceName, err)
	}
	defer service.Close()
	if err := service.Start(); err != nil {
		status, queryErr := service.Query()
		if queryErr != nil || status.State != svc.Running {
			return fmt.Errorf("start %s: %w", serviceName, err)
		}
	}
	return nil
}

func stopInstalledService() error {
	manager, err := mgr.Connect()
	if err != nil {
		return fmt.Errorf("connect to the Windows service manager: %w", err)
	}
	defer manager.Disconnect()
	service, err := manager.OpenService(serviceName)
	if err != nil {
		if errors.Is(err, windows.ERROR_SERVICE_DOES_NOT_EXIST) {
			return nil
		}
		return fmt.Errorf("open %s service: %w", serviceName, err)
	}
	defer service.Close()
	status, err := service.Control(svc.Stop)
	if err != nil && !errors.Is(err, windows.ERROR_SERVICE_NOT_ACTIVE) {
		return fmt.Errorf("stop %s: %w", serviceName, err)
	}
	deadline := time.Now().Add(20 * time.Second)
	for time.Now().Before(deadline) {
		current, queryErr := service.Query()
		if queryErr != nil || current.State == svc.Stopped {
			return nil
		}
		time.Sleep(500 * time.Millisecond)
	}
	_ = status
	return fmt.Errorf("service %s did not stop within 20 seconds", serviceName)
}
