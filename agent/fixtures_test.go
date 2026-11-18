package main

import "os"

func writeFixture(path, content string) error {
	return os.WriteFile(path, []byte(content), 0o600)
}
