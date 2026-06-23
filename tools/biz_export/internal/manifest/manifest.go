package manifest

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"time"

	"github.com/beastserver/biz_export/internal/model"
)

type Document struct {
	Version string                       `json:"version"`
	Tables  map[string]TableManifestEntry `json:"tables"`
}

type TableManifestEntry struct {
	File         string `json:"file"`
	ClientFile   string `json:"client_file,omitempty"`
	Schema       string `json:"schema"`
	ClientSchema string `json:"client_schema,omitempty"`
	RowCount     int    `json:"row_count"`
	SHA256       string `json:"sha256,omitempty"`
	ClientSHA256 string `json:"client_sha256,omitempty"`
}

func Write(path string, tables []model.TableOutput) error {
	doc := Document{
		Version: time.Now().UTC().Format("20060102.150405"),
		Tables:  map[string]TableManifestEntry{},
	}

	for _, table := range tables {
		doc.Tables[table.LogicalName] = TableManifestEntry{
			File:         table.ServerFile,
			ClientFile:   table.ClientFile,
			Schema:       table.ServerSchema,
			ClientSchema: table.ClientSchema,
			RowCount:     table.RowCount,
			SHA256:       table.ServerSHA256,
			ClientSHA256: table.ClientSHA256,
		}
	}

	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return fmt.Errorf("mkdir manifest dir: %w", err)
	}

	payload, err := json.MarshalIndent(doc, "", "  ")
	if err != nil {
		return fmt.Errorf("marshal manifest: %w", err)
	}
	payload = append(payload, '\n')

	if err := os.WriteFile(path, payload, 0o644); err != nil {
		return fmt.Errorf("write manifest: %w", err)
	}
	return nil
}
