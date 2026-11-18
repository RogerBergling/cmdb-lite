//go:build !windows

package main

import (
	"fmt"
	"os"
	"os/user"
	"strconv"
)

const serviceGroup = "cmdb-agent"

func secureConfigDirectory(dir string) error {
	if os.Geteuid() != 0 {
		return nil
	}
	group, err := user.LookupGroup(serviceGroup)
	if err != nil {
		return fmt.Errorf("group %q does not exist; install the cmdb-lite agent package or create the group", serviceGroup)
	}
	gid, err := strconv.Atoi(group.Gid)
	if err != nil {
		return fmt.Errorf("group %q has an invalid ID", serviceGroup)
	}
	if err := os.Chown(dir, 0, gid); err != nil {
		return fmt.Errorf("set configuration directory owner: %w", err)
	}
	if err := os.Chmod(dir, 0o750); err != nil {
		return fmt.Errorf("set configuration directory mode: %w", err)
	}
	return nil
}

func secureConfigFile(path string) error {
	if os.Geteuid() != 0 {
		return os.Chmod(path, 0o600)
	}
	group, err := user.LookupGroup(serviceGroup)
	if err != nil {
		return fmt.Errorf("group %q does not exist; install the cmdb-lite agent package or create the group", serviceGroup)
	}
	gid, err := strconv.Atoi(group.Gid)
	if err != nil {
		return fmt.Errorf("group %q has an invalid ID", serviceGroup)
	}
	if err := os.Chown(path, 0, gid); err != nil {
		return fmt.Errorf("set configuration owner: %w", err)
	}
	if err := os.Chmod(path, 0o640); err != nil {
		return fmt.Errorf("set configuration mode: %w", err)
	}
	return nil
}
