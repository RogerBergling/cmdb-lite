//go:build windows

package main

import (
	"errors"
	"os"
	"runtime"
	"strconv"
	"strings"

	"golang.org/x/sys/windows/registry"
)

const uninstallKey = `SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall`

func collectHostFacts() (HostFacts, error) {
	hostname, _ := os.Hostname()
	hostname = cleanText(hostname, 255)
	osName := "Windows"
	osVersion := ""
	key, err := registry.OpenKey(registry.LOCAL_MACHINE,
		`SOFTWARE\Microsoft\Windows NT\CurrentVersion`,
		registry.QUERY_VALUE|registry.WOW64_64KEY)
	if err == nil {
		if value, _, readErr := key.GetStringValue("ProductName"); readErr == nil {
			osName = cleanText(value, 127)
		}
		displayVersion, _, _ := key.GetStringValue("DisplayVersion")
		build, _, _ := key.GetStringValue("CurrentBuild")
		ubr, _, _ := key.GetIntegerValue("UBR")
		if build != "" {
			if buildNumber, parseErr := strconv.Atoi(build); parseErr == nil && buildNumber >= 22000 &&
				strings.Contains(osName, "Windows 10") {
				osName = strings.Replace(osName, "Windows 10", "Windows 11", 1)
			}
			osVersion = build
			if ubr > 0 {
				osVersion += "." + strconv.FormatUint(ubr, 10)
			}
			if displayVersion != "" {
				osVersion = displayVersion + " (build " + osVersion + ")"
			}
		}
		key.Close()
	}
	machineID := ""
	if cryptoKey, cryptoErr := registry.OpenKey(registry.LOCAL_MACHINE,
		`SOFTWARE\Microsoft\Cryptography`,
		registry.QUERY_VALUE|registry.WOW64_64KEY); cryptoErr == nil {
		if value, _, readErr := cryptoKey.GetStringValue("MachineGuid"); readErr == nil {
			machineID = value
		}
		cryptoKey.Close()
	}
	facts := HostFacts{
		DisplayName:  hostname,
		Hostname:     hostname,
		MachineID:    cleanText(machineID, 127),
		OSFamily:     "windows",
		OSName:       osName,
		OSVersion:    cleanText(osVersion, 127),
		Architecture: normalizedArchitecture(runtime.GOARCH),
		AgentVersion: version,
	}
	if facts.Hostname == "" && facts.MachineID == "" {
		return facts, errors.New("cannot determine the host identity")
	}
	return facts, nil
}

func stringValue(key registry.Key, name string) string {
	value, _, err := key.GetStringValue(name)
	if err != nil {
		return ""
	}
	return value
}

func dwordValue(key registry.Key, name string) uint64 {
	value, _, err := key.GetIntegerValue(name)
	if err != nil {
		return 0
	}
	return value
}

func skipUninstallEntry(key registry.Key) bool {
	if dwordValue(key, "SystemComponent") != 0 {
		return true
	}
	if strings.TrimSpace(stringValue(key, "ParentKeyName")) != "" {
		return true
	}
	switch strings.ToLower(strings.TrimSpace(stringValue(key, "ReleaseType"))) {
	case "update", "hotfix", "security update", "servicepack":
		return true
	}
	name := strings.ToLower(strings.TrimSpace(stringValue(key, "DisplayName")))
	return strings.HasPrefix(name, "update for ") ||
		strings.HasPrefix(name, "hotfix for ") ||
		strings.HasPrefix(name, "security update for ")
}

func windowsInstallDate(value string) string {
	value = strings.TrimSpace(value)
	if len(value) == 8 {
		if _, err := strconv.Atoi(value); err == nil {
			return value[0:4] + "-" + value[4:6] + "-" + value[6:8]
		}
	}
	return value
}

func collectUninstallView(view uint32, architecture string) ([]Application, []string) {
	var apps []Application
	var errs []string
	root, err := registry.OpenKey(registry.LOCAL_MACHINE, uninstallKey,
		registry.READ|view)
	if err != nil {
		return nil, []string{"cannot open the " + architecture + " uninstall registry: " + err.Error()}
	}
	defer root.Close()
	names, err := root.ReadSubKeyNames(-1)
	if err != nil {
		return nil, []string{"cannot enumerate the " + architecture + " uninstall registry: " + err.Error()}
	}
	skipped := 0
	for _, name := range names {
		if len(apps) >= maxApplications {
			errs = append(errs, "application inventory was truncated at 20000 entries")
			break
		}
		subkey, err := registry.OpenKey(root, name, registry.READ|view)
		if err != nil {
			skipped++
			continue
		}
		if skipUninstallEntry(subkey) {
			subkey.Close()
			continue
		}
		displayName := cleanText(stringValue(subkey, "DisplayName"), 511)
		if displayName == "" {
			subkey.Close()
			continue
		}
		sizeKB := int64(dwordValue(subkey, "EstimatedSize"))
		app := Application{
			Key:             "win:" + architecture + ":" + strings.ToLower(name),
			Name:            displayName,
			Version:         cleanText(stringValue(subkey, "DisplayVersion"), 255),
			Publisher:       cleanText(stringValue(subkey, "Publisher"), 255),
			Source:          "windows",
			Architecture:    architecture,
			InstallDate:     windowsInstallDate(stringValue(subkey, "InstallDate")),
			InstallLocation: cleanText(stringValue(subkey, "InstallLocation"), 1023),
			SizeKB:          sizeKB,
		}
		subkey.Close()
		apps = append(apps, app)
	}
	if skipped > 0 {
		errs = append(errs, "skipped "+strconv.Itoa(skipped)+" unreadable uninstall registry entries")
	}
	return apps, errs
}

func collectApplications() ([]Application, []string) {
	apps64, errs64 := collectUninstallView(registry.WOW64_64KEY, "x86_64")
	apps32, errs32 := collectUninstallView(registry.WOW64_32KEY, "x86")
	apps := append(apps64, apps32...)
	errs := append(errs64, errs32...)
	if len(apps) == 0 && len(errs) == 0 {
		errs = append(errs, "no installed applications were found in the uninstall registry")
	}
	return apps, errs
}
