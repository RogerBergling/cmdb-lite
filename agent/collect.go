package main

import (
	"fmt"
	"net"
	"sort"
	"strings"
	"time"
	"unicode"
	"unicode/utf8"
)

const maxApplications = 20000

func cleanText(value string, max int) string {
	value = strings.TrimSpace(value)
	value = strings.Map(func(r rune) rune {
		if unicode.IsControl(r) || unicode.Is(unicode.Cf, r) ||
			r == '\u2028' || r == '\u2029' {
			return ' '
		}
		return r
	}, value)
	value = strings.Join(strings.Fields(value), " ")
	if len(value) <= max {
		return value
	}
	for max > 0 && !utf8.RuneStart(value[max]) {
		max--
	}
	return strings.TrimSpace(value[:max])
}

func normalizedArchitecture(goarch string) string {
	switch goarch {
	case "amd64":
		return "x86_64"
	case "386":
		return "x86"
	case "arm64":
		return "aarch64"
	default:
		return goarch
	}
}

func finishApplications(apps []Application, errs []string) ([]Application, []string) {
	seen := make(map[string]bool, len(apps))
	filtered := apps[:0]
	dropped := 0
	for _, app := range apps {
		app.Key = cleanText(app.Key, 255)
		app.Name = cleanText(app.Name, 511)
		app.Version = cleanText(app.Version, 255)
		app.Publisher = cleanText(app.Publisher, 255)
		app.Source = cleanText(app.Source, 31)
		app.Architecture = cleanText(app.Architecture, 31)
		app.InstallDate = cleanText(app.InstallDate, 31)
		app.InstallLocation = cleanText(app.InstallLocation, 1023)
		if app.Key == "" || app.Name == "" || seen[app.Key] {
			dropped++
			continue
		}
		if app.SizeKB < 0 {
			app.SizeKB = 0
		}
		seen[app.Key] = true
		filtered = append(filtered, app)
		if len(filtered) >= maxApplications {
			errs = append(errs, "application inventory was truncated at 20000 entries")
			break
		}
	}
	if dropped > 0 {
		errs = append(errs, fmt.Sprintf("ignored %d malformed or duplicate application entries", dropped))
	}
	sort.Slice(filtered, func(i, j int) bool {
		if filtered[i].Name != filtered[j].Name {
			return strings.ToLower(filtered[i].Name) < strings.ToLower(filtered[j].Name)
		}
		if filtered[i].Version != filtered[j].Version {
			return filtered[i].Version < filtered[j].Version
		}
		return filtered[i].Key < filtered[j].Key
	})
	if len(errs) > 32 {
		errs = errs[:32]
	}
	return filtered, errs
}

func primaryIPAddress() string {
	for _, network := range []string{"udp4", "udp6"} {
		address := "192.0.2.1:9"
		if network == "udp6" {
			address = "[2001:db8::1]:9"
		}
		conn, err := net.Dial(network, address)
		if err != nil {
			continue
		}
		udp, ok := conn.LocalAddr().(*net.UDPAddr)
		conn.Close()
		if ok && usableIP(udp.IP) {
			return udp.IP.String()
		}
	}
	var ipv4, ipv6 string
	interfaces, err := net.Interfaces()
	if err != nil {
		return ""
	}
	for _, iface := range interfaces {
		if iface.Flags&net.FlagUp == 0 || iface.Flags&net.FlagLoopback != 0 {
			continue
		}
		addresses, err := iface.Addrs()
		if err != nil {
			continue
		}
		for _, address := range addresses {
			ip, _, err := net.ParseCIDR(address.String())
			if err != nil || !usableIP(ip) {
				continue
			}
			if ip.To4() != nil {
				if ipv4 == "" {
					ipv4 = ip.String()
				}
			} else if ipv6 == "" {
				ipv6 = ip.String()
			}
		}
	}
	if ipv4 != "" {
		return ipv4
	}
	return ipv6
}

func usableIP(ip net.IP) bool {
	return ip != nil && !ip.IsLoopback() && !ip.IsUnspecified() &&
		!ip.IsLinkLocalUnicast() && !ip.IsLinkLocalMulticast()
}

func cleanSerial(value string) string {
	value = cleanText(value, 127)
	switch strings.ToLower(value) {
	case "", "none", "unknown", "not applicable", "not available", "n/a",
		"default string", "system serial number", "to be filled by o.e.m.",
		"to be filled by oem":
		return ""
	}
	return value
}

func collectNow(now time.Time) Collection {
	host, err := collectHostFacts()
	collection := Collection{CollectedAt: now, Host: host}
	if err != nil {
		collection.Errors = append(collection.Errors, err.Error())
	}
	apps, errs := collectApplications()
	collection.Apps, collection.Errors = finishApplications(apps, append(collection.Errors, errs...))
	collection.Complete = len(collection.Errors) == 0
	return collection
}
