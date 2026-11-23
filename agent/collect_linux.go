//go:build !windows

package main

import (
	"bufio"
	"context"
	"errors"
	"fmt"
	"io"
	"os"
	"os/exec"
	"runtime"
	"strconv"
	"strings"
	"time"
)

const dpkgStatusPath = "/var/lib/dpkg/status"

func collectHostFacts() (HostFacts, error) {
	hostname, _ := os.Hostname()
	hostname = cleanText(hostname, 255)
	machineID := readFirstLine("/etc/machine-id")
	if machineID == "" {
		machineID = readFirstLine("/var/lib/dbus/machine-id")
	}
	osName, osVersion := linuxOSRelease()
	kernelVersion := cleanText(readFirstLine("/proc/sys/kernel/osrelease"), 127)
	if osVersion == "" {
		osVersion = kernelVersion
	}
	facts := HostFacts{
		DisplayName:  hostname,
		Hostname:     hostname,
		MachineID:    cleanText(machineID, 127),
		OSFamily:     "linux",
		OSName:       osName,
		OSVersion:    osVersion,
		Architecture: normalizedArchitecture(runtime.GOARCH),
		AgentVersion: version,
		IPAddress:    primaryIPAddress(),
		SerialNumber: cleanSerial(firstNonEmptyLine(
			"/sys/class/dmi/id/product_serial",
			"/sys/class/dmi/id/board_serial",
			"/sys/devices/virtual/dmi/id/product_serial",
		)),
		CPUCores:      runtime.NumCPU(),
		MemoryMB:      memTotalMB("/proc/meminfo"),
		KernelVersion: kernelVersion,
	}
	if facts.Hostname == "" && facts.MachineID == "" {
		return facts, errors.New("cannot determine the host identity")
	}
	return facts, nil
}

func readFirstLine(path string) string {
	file, err := os.Open(path)
	if err != nil {
		return ""
	}
	defer file.Close()
	scanner := bufio.NewScanner(io.LimitReader(file, 4096))
	if scanner.Scan() {
		return strings.TrimSpace(scanner.Text())
	}
	return ""
}

func firstNonEmptyLine(paths ...string) string {
	for _, path := range paths {
		if value := strings.TrimSpace(readFirstLine(path)); value != "" {
			return value
		}
	}
	return ""
}

func memTotalMB(path string) int64 {
	file, err := os.Open(path)
	if err != nil {
		return 0
	}
	defer file.Close()
	scanner := bufio.NewScanner(io.LimitReader(file, 64<<10))
	for scanner.Scan() {
		fields := strings.Fields(scanner.Text())
		if len(fields) < 2 || fields[0] != "MemTotal:" {
			continue
		}
		kilobytes, err := strconv.ParseInt(fields[1], 10, 64)
		if err != nil || kilobytes <= 0 {
			return 0
		}
		return kilobytes / 1024
	}
	return 0
}

func linuxOSRelease() (string, string) {
	file, err := os.Open("/etc/os-release")
	if err != nil {
		return "Linux", ""
	}
	defer file.Close()
	values := make(map[string]string)
	scanner := bufio.NewScanner(io.LimitReader(file, 64<<10))
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}
		key, value, found := strings.Cut(line, "=")
		if !found {
			continue
		}
		value = strings.Trim(value, `"'`)
		values[key] = value
	}
	name := values["PRETTY_NAME"]
	if name == "" {
		name = values["NAME"]
	}
	if name == "" {
		name = "Linux"
	}
	versionText := values["VERSION_ID"]
	if versionText == "" {
		versionText = values["VERSION"]
	}
	return cleanText(name, 127), cleanText(versionText, 127)
}

func collectApplications() ([]Application, []string) {
	if _, err := os.Stat(dpkgStatusPath); err == nil {
		return readDpkgStatus(dpkgStatusPath)
	}
	return readRPMDatabase(context.Background())
}

func readDpkgStatus(path string) ([]Application, []string) {
	file, err := os.Open(path)
	if err != nil {
		return nil, []string{"cannot read dpkg status: " + err.Error()}
	}
	defer file.Close()
	var apps []Application
	var errs []string
	fields := make(map[string]string)
	malformed := 0
	flush := func() {
		if len(fields) == 0 {
			return
		}
		status := fields["Status"]
		statusFields := strings.Fields(status)
		installed := len(statusFields) >= 3 && statusFields[len(statusFields)-1] == "installed"
		if installed {
			name := strings.TrimSpace(fields["Package"])
			arch := strings.TrimSpace(fields["Architecture"])
			appVersion := strings.TrimSpace(fields["Version"])
			if name != "" && arch != "" && appVersion != "" {
				sizeKB := int64(0)
				if value := strings.TrimSpace(fields["Installed-Size"]); value != "" {
					if parsed, parseErr := strconv.ParseInt(value, 10, 64); parseErr == nil && parsed > 0 {
						sizeKB = parsed
					}
				}
				apps = append(apps, Application{
					Key:          "deb:" + name + ":" + arch,
					Name:         name,
					Version:      appVersion,
					Publisher:    strings.TrimSpace(fields["Maintainer"]),
					Source:       "deb",
					Architecture: arch,
					SizeKB:       sizeKB,
				})
			} else {
				malformed++
			}
		}
		fields = make(map[string]string)
	}
	scanner := bufio.NewScanner(file)
	scanner.Buffer(make([]byte, 64<<10), 1<<20)
	for scanner.Scan() {
		line := scanner.Text()
		if strings.TrimSpace(line) == "" {
			flush()
			continue
		}
		if line[0] == ' ' || line[0] == '\t' {
			continue
		}
		key, value, found := strings.Cut(line, ":")
		if !found {
			malformed++
			continue
		}
		fields[strings.TrimSpace(key)] = strings.TrimSpace(value)
	}
	if err := scanner.Err(); err != nil {
		errs = append(errs, "cannot parse dpkg status: "+err.Error())
	}
	flush()
	if malformed > 0 {
		errs = append(errs, fmt.Sprintf("ignored %d malformed dpkg status entries", malformed))
	}
	if len(apps) == 0 && len(errs) == 0 {
		errs = append(errs, "dpkg status contained no installed packages")
	}
	return apps, errs
}

func readRPMDatabase(ctx context.Context) ([]Application, []string) {
	rpm, err := exec.LookPath("rpm")
	if err != nil {
		return nil, []string{"no supported package database found (dpkg or rpm)"}
	}
	query := "%{NAME}\t%{EPOCHNUM}\t%{VERSION}\t%{RELEASE}\t%{ARCH}\t%{VENDOR}\t%{INSTALLTIME}\t%{SIZE}\n"
	command := exec.CommandContext(ctx, rpm, "-qa", "--qf", query)
	command.Env = append(os.Environ(), "LC_ALL=C")
	output, err := command.Output()
	if err != nil {
		return nil, []string{"cannot query rpm database: " + err.Error()}
	}
	apps, errs := parseRPMOutput(string(output))
	if len(apps) == 0 && len(errs) == 0 {
		errs = append(errs, "rpm database contained no packages")
	}
	return apps, errs
}

func parseRPMOutput(output string) ([]Application, []string) {
	var apps []Application
	var errs []string
	for _, line := range strings.Split(output, "\n") {
		line = strings.TrimRight(line, "\r")
		if line == "" {
			continue
		}
		fields := strings.SplitN(line, "\t", 8)
		if len(fields) < 8 {
			errs = append(errs, "ignored malformed rpm record")
			continue
		}
		name := strings.TrimSpace(fields[0])
		arch := strings.TrimSpace(fields[4])
		if name == "" || arch == "" {
			continue
		}
		versionText := strings.TrimSpace(fields[2])
		if release := strings.TrimSpace(fields[3]); release != "" {
			versionText += "-" + release
		}
		if epoch := strings.TrimSpace(fields[1]); epoch != "" && epoch != "0" && epoch != "(none)" {
			versionText = epoch + ":" + versionText
		}
		installDate := ""
		if seconds, parseErr := strconv.ParseInt(strings.TrimSpace(fields[6]), 10, 64); parseErr == nil && seconds > 0 {
			installDate = time.Unix(seconds, 0).UTC().Format("2006-01-02")
		}
		sizeKB := int64(0)
		if size, parseErr := strconv.ParseInt(strings.TrimSpace(fields[7]), 10, 64); parseErr == nil && size > 0 {
			sizeKB = size / 1024
		}
		apps = append(apps, Application{
			Key:          "rpm:" + name + ":" + arch,
			Name:         name,
			Version:      versionText,
			Publisher:    strings.TrimSpace(fields[5]),
			Source:       "rpm",
			Architecture: arch,
			InstallDate:  installDate,
			SizeKB:       sizeKB,
		})
	}
	return apps, errs
}
