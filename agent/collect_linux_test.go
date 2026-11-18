//go:build linux

package main

import (
	"testing"
)

func TestReadDpkgStatus(t *testing.T) {
	path := t.TempDir() + "/status"
	content := `Package: bash
Status: install ok installed
Architecture: amd64
Version: 5.2.15-2
Maintainer: Ubuntu Developers <ubuntu-devel@lists.ubuntu.com>
Installed-Size: 1234
Description: GNU Bourne Again SHell
 multi-line description

Package: removed
Status: deinstall ok config-files
Architecture: amd64
Version: 1.0

Package: curl
Status: install ok installed
Architecture: amd64
Version: 8.5.0-2
Maintainer: Debian Curl Maintainers
Installed-Size: 456
Description: command line tool

Package: held
Status: hold ok installed
Architecture: amd64
Version: 2.0
Description: held package
`
	if err := writeFixture(path, content); err != nil {
		t.Fatal(err)
	}
	apps, errs := readDpkgStatus(path)
	if len(errs) != 0 {
		t.Fatalf("unexpected errors: %#v", errs)
	}
	if len(apps) != 3 {
		t.Fatalf("got %d apps, want 3: %#v", len(apps), apps)
	}
	if apps[0].Key != "deb:bash:amd64" || apps[0].Version != "5.2.15-2" || apps[0].SizeKB != 1234 {
		t.Fatalf("unexpected bash record: %#v", apps[0])
	}
	if apps[1].Key != "deb:curl:amd64" {
		t.Fatalf("unexpected curl record: %#v", apps[1])
	}
	if apps[2].Key != "deb:held:amd64" {
		t.Fatalf("held installed package was not collected: %#v", apps[2])
	}
	if apps[0].Source != "deb" || apps[0].Publisher == "" {
		t.Fatalf("missing package metadata: %#v", apps[0])
	}
}

func TestReadDpkgEmpty(t *testing.T) {
	path := t.TempDir() + "/status"
	if err := writeFixture(path, ""); err != nil {
		t.Fatal(err)
	}
	apps, errs := readDpkgStatus(path)
	if len(apps) != 0 || len(errs) == 0 {
		t.Fatalf("empty dpkg status = %#v, %#v", apps, errs)
	}
}

func TestParseRPMOutput(t *testing.T) {
	output := "bash\t0\t5.2.15\t2.fc40\tx86_64\tFedora Project\t1710000000\t1234567\n" +
		"bad-record\n" +
		"kernel\t0\t6.8.0\t100.fc40\tx86_64\tFedora Project\t1710000001\t4567890\n"
	apps, errs := parseRPMOutput(output)
	if len(errs) != 1 {
		t.Fatalf("errors = %#v", errs)
	}
	if len(apps) != 2 {
		t.Fatalf("got %d apps, want 2: %#v", len(apps), apps)
	}
	if apps[0].Key != "rpm:bash:x86_64" || apps[0].Version != "5.2.15-2.fc40" {
		t.Fatalf("unexpected bash record: %#v", apps[0])
	}
	if apps[0].InstallDate == "" || apps[0].SizeKB != 1205 {
		t.Fatalf("unexpected rpm metadata: %#v", apps[0])
	}
	if apps[1].Key != "rpm:kernel:x86_64" {
		t.Fatalf("unexpected kernel record: %#v", apps[1])
	}
}
