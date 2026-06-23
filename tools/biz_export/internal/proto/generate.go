package proto

import (
	"bytes"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"text/template"

	"github.com/beastserver/biz_export/internal/model"
	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/types/descriptorpb"
)

const protoTemplate = `syntax = "proto3";

package {{.PackageName}};
option go_package = "{{.PackageName}}";

message {{.MsgName}}RowServer {
{{- range $index, $field := .ServerFields}}
    {{$field.ProtoType}} {{$field.Name}} = {{add $index 1}};
{{- end}}
}

message {{.MsgName}}ServerConfig {
    repeated {{.MsgName}}RowServer rows = 1;
}

message {{.MsgName}}RowClient {
{{- range $index, $field := .ClientFields}}
    {{$field.ProtoType}} {{$field.Name}} = {{add $index 1}};
{{- end}}
}

message {{.MsgName}}ClientConfig {
    repeated {{.MsgName}}RowClient rows = 1;
}
`

type generateView struct {
	PackageName  string
	MsgName      string
	ServerFields []model.FieldInfo
	ClientFields []model.FieldInfo
}

func GenerateSchema(sheet model.SheetResult, schemaRoot string) (string, error) {
	tmpl, err := template.New("proto").Funcs(template.FuncMap{
		"add": func(a, b int) int { return a + b },
	}).Parse(protoTemplate)
	if err != nil {
		return "", fmt.Errorf("parse proto template: %w", err)
	}

	outputDir := schemaRoot
	if sheet.ProtoRelDir != "" {
		outputDir = filepath.Join(schemaRoot, filepath.FromSlash(sheet.ProtoRelDir))
	}
	if err := os.MkdirAll(outputDir, 0o755); err != nil {
		return "", fmt.Errorf("mkdir schema dir: %w", err)
	}

	fileName := tableFileName(sheet.LogicalName) + ".proto"
	filePath := filepath.Join(outputDir, fileName)

	var buffer bytes.Buffer
	if err := tmpl.Execute(&buffer, generateView{
		PackageName:  sheet.PackageName,
		MsgName:      sheet.MsgName,
		ServerFields: sheet.ServerFields(),
		ClientFields: sheet.ClientFields(),
	}); err != nil {
		return "", fmt.Errorf("render proto: %w", err)
	}

	if err := os.WriteFile(filePath, buffer.Bytes(), 0o644); err != nil {
		return "", fmt.Errorf("write proto: %w", err)
	}
	return filePath, nil
}

func CompileDescriptorSet(protoPath string) (*descriptorpb.FileDescriptorSet, error) {
	tmpFile, err := os.CreateTemp("", "beast_biz_desc_*.pb")
	if err != nil {
		return nil, fmt.Errorf("create temp descriptor file: %w", err)
	}
	tmpPath := tmpFile.Name()
	tmpFile.Close()
	defer os.Remove(tmpPath)

	protoDir := filepath.Dir(protoPath)
	cmd := exec.Command(
		"protoc",
		"--descriptor_set_out="+tmpPath,
		"--include_imports",
		"--proto_path="+protoDir,
		filepath.Base(protoPath),
	)
	cmd.Stderr = os.Stderr
	if err := cmd.Run(); err != nil {
		return nil, fmt.Errorf("protoc failed for %s: %w", protoPath, err)
	}

	data, err := os.ReadFile(tmpPath)
	if err != nil {
		return nil, fmt.Errorf("read descriptor set: %w", err)
	}

	fds := &descriptorpb.FileDescriptorSet{}
	if err := proto.Unmarshal(data, fds); err != nil {
		return nil, fmt.Errorf("unmarshal descriptor set: %w", err)
	}
	return fds, nil
}

func tableFileName(logicalName string) string {
	parts := strings.Split(logicalName, "/")
	return parts[len(parts)-1]
}

func ServerSchemaName(sheet model.SheetResult) string {
	return sheet.PackageName + "." + sheet.MsgName + "ServerConfig"
}

func ClientSchemaName(sheet model.SheetResult) string {
	return sheet.PackageName + "." + sheet.MsgName + "ClientConfig"
}

func ServerRowSchemaName(sheet model.SheetResult) string {
	return sheet.PackageName + "." + sheet.MsgName + "RowServer"
}

func ClientRowSchemaName(sheet model.SheetResult) string {
	return sheet.PackageName + "." + sheet.MsgName + "RowClient"
}

func OutputBaseName(logicalName string) string {
	return tableFileName(logicalName)
}
