package validate

import (
	"encoding/json"
	"fmt"
	"regexp"
	"strconv"
	"strings"

	"github.com/beastserver/biz_export/internal/excel"
	"github.com/beastserver/biz_export/internal/model"
)

type Error struct {
	Source string
	Row    int
	Field  string
	Reason string
}

func (e Error) String() string {
	if e.Source != "" {
		return fmt.Sprintf("%s Row %d, 字段 %s: %s", e.Source, e.Row, e.Field, e.Reason)
	}
	return fmt.Sprintf("Row %d, 字段 %s: %s", e.Row, e.Field, e.Reason)
}

var indexRegex = regexp.MustCompile(`^[a-z][a-z0-9_]*$`)

func Sheet(sheet model.SheetResult) []Error {
	var errs []Error
	source := fmt.Sprintf("%s/%s", filepathBase(sheet.SourceXLSX), sheet.SheetName)

	onlySets := map[string]map[string]bool{}
	for _, field := range sheet.Fields {
		if strings.Contains(field.Constraint, "only") {
			onlySets[field.Name] = map[string]bool{}
		}
	}

	for rowIdx, row := range sheet.DataRows {
		excelRow := rowIdx + excel.HeaderRows + 1
		for _, field := range sheet.Fields {
			value := excel.CellValue(row, field.ColIndex)
			if strings.Contains(field.Constraint, "notnull") && strings.TrimSpace(value) == "" {
				errs = append(errs, Error{
					Source: source,
					Row:    excelRow,
					Field:  field.Name,
					Reason: "必填字段为空",
				})
				continue
			}
			if strings.TrimSpace(value) == "" {
				continue
			}

			if strings.Contains(field.Constraint, "only") {
				if onlySets[field.Name][value] {
					errs = append(errs, Error{
						Source: source,
						Row:    excelRow,
						Field:  field.Name,
						Reason: fmt.Sprintf("值重复: %s", value),
					})
				}
				onlySets[field.Name][value] = true
			}

			if isIndexField(field.Name, field.ProtoType) && !indexRegex.MatchString(value) {
				errs = append(errs, Error{
					Source: source,
					Row:    excelRow,
					Field:  field.Name,
					Reason: fmt.Sprintf("index 格式非法 (要求 ^[a-z][a-z0-9_]*$): %s", value),
				})
			}

			if err := validateType(field, value, excelRow, source); err != nil {
				errs = append(errs, *err)
			}
		}
	}

	return errs
}

func validateType(field model.FieldInfo, value string, row int, source string) *Error {
	switch field.ProtoType {
	case "int32", "int64", "uint32", "uint64", "sint32", "sint64", "sfixed32", "sfixed64", "fixed32", "fixed64":
		if _, err := strconv.ParseInt(value, 10, 64); err != nil {
			return &Error{Source: source, Row: row, Field: field.Name, Reason: fmt.Sprintf("整数格式非法: %s", value)}
		}
		if strings.HasPrefix(field.ProtoType, "u") || strings.HasPrefix(field.ProtoType, "fixed") {
			parsed, _ := strconv.ParseInt(value, 10, 64)
			if parsed < 0 {
				return &Error{Source: source, Row: row, Field: field.Name, Reason: fmt.Sprintf("无符号整数不能为负: %s", value)}
			}
		}
	case "float", "double":
		if _, err := strconv.ParseFloat(value, 64); err != nil {
			return &Error{Source: source, Row: row, Field: field.Name, Reason: fmt.Sprintf("浮点数格式非法: %s", value)}
		}
	case "bool":
		if value != "true" && value != "false" {
			return &Error{Source: source, Row: row, Field: field.Name, Reason: fmt.Sprintf("布尔值必须为 true/false: %s", value)}
		}
	case "string":
		trimmed := strings.TrimSpace(value)
		if (strings.HasPrefix(trimmed, "[") && strings.HasSuffix(trimmed, "]")) ||
			(strings.HasPrefix(trimmed, "{") && strings.HasSuffix(trimmed, "}")) {
			if !json.Valid([]byte(trimmed)) {
				return &Error{Source: source, Row: row, Field: field.Name, Reason: fmt.Sprintf("JSON 格式非法: %s", value)}
			}
		}
	}
	return nil
}

func isIndexField(name, protoType string) bool {
	if protoType != "string" {
		return false
	}
	name = strings.ToLower(name)
	return name == "index" || strings.HasSuffix(name, "_index") || strings.HasSuffix(name, "_key")
}

func filepathBase(path string) string {
	parts := strings.Split(path, "/")
	return parts[len(parts)-1]
}
