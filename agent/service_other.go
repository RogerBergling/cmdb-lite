//go:build !linux && !windows

package main

import "errors"

func isWindowsService() (bool, error) {
	return false, nil
}

func runWindowsService(_ string) error {
	return errors.New("unsupported operating system")
}

func startService(_ string) error {
	return errors.New("unsupported operating system")
}

func installService(_ string) error {
	return errors.New("unsupported operating system")
}

func uninstallService() error {
	return errors.New("unsupported operating system")
}

func startInstalledService() error {
	return errors.New("unsupported operating system")
}

func stopInstalledService() error {
	return errors.New("unsupported operating system")
}
