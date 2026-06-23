package model

import "path/filepath"

type FieldInfo struct {
	Name       string
	ProtoType  string
	Constraint string
	VisibleS   bool
	VisibleC   bool
	ColIndex   int
}

type SheetResult struct {
	LogicalName string
	SheetName   string
	MsgName     string
	PackageName string
	ProtoRelDir string
	Fields      []FieldInfo
	DataRows    [][]string
	SourceXLSX  string
}

func (s SheetResult) ServerFields() []FieldInfo {
	var out []FieldInfo
	for _, field := range s.Fields {
		if field.VisibleS {
			out = append(out, field)
		}
	}
	return out
}

func (s SheetResult) ClientFields() []FieldInfo {
	var out []FieldInfo
	for _, field := range s.Fields {
		if field.VisibleC {
			out = append(out, field)
		}
	}
	return out
}

type TableOutput struct {
	LogicalName  string
	ServerFile   string
	ClientFile   string
	ServerSchema string
	ClientSchema string
	RowCount     int
	ServerSHA256 string
	ClientSHA256 string
}

func BuildLogicalName(rawRoot, xlsxPath, sheetName string) (string, string, string) {
	rel, err := filepath.Rel(rawRoot, xlsxPath)
	if err != nil {
		rel = filepath.Base(xlsxPath)
	}
	relDir := filepath.Dir(rel)
	stem := sheetName
	if stem == "" || stem == "Sheet1" {
		stem = fileStemWithoutExt(filepath.Base(xlsxPath))
	}

	logicalName := toSnakeCase(stem)
	if relDir != "." && relDir != "" {
		relDirSlash := filepath.ToSlash(relDir)
		if filepath.Base(relDirSlash) == logicalName {
			logicalName = relDirSlash
		} else {
			logicalName = filepath.ToSlash(filepath.Join(relDir, logicalName))
		}
	}

	msgName := toCamelCase(stem)
	protoRelDir := relDir
	if protoRelDir == "." {
		protoRelDir = ""
	}
	return logicalName, msgName, filepath.ToSlash(protoRelDir)
}
