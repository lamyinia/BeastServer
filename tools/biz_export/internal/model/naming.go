package model

import (
	"path/filepath"
	"strings"
	"unicode"
)

func fileStemWithoutExt(name string) string {
	return strings.TrimSuffix(name, filepath.Ext(name))
}

func ToCamelCase(value string) string {
	return toCamelCase(value)
}

func toCamelCase(value string) string {
	parts := strings.FieldsFunc(value, func(r rune) bool {
		return r == '_' || r == '-' || r == ' '
	})
	for i, part := range parts {
		if part == "" {
			continue
		}
		runes := []rune(part)
		runes[0] = unicode.ToUpper(runes[0])
		parts[i] = string(runes)
	}
	return strings.Join(parts, "")
}

func toSnakeCase(value string) string {
	var builder strings.Builder
	for i, ch := range value {
		if ch >= 'A' && ch <= 'Z' {
			if i > 0 {
				builder.WriteByte('_')
			}
			builder.WriteRune(ch + ('a' - 'A'))
			continue
		}
		if ch == '-' {
			builder.WriteByte('_')
			continue
		}
		builder.WriteRune(ch)
	}
	return builder.String()
}

func PackageNameFromLogical(logicalName string) string {
	parts := strings.Split(logicalName, "/")
	var pkgParts []string
	for _, part := range parts {
		part = strings.TrimSpace(part)
		if part == "" {
			continue
		}
		pkgParts = append(pkgParts, strings.ReplaceAll(part, "-", "_"))
	}
	if len(pkgParts) == 0 {
		return "beast.biz"
	}
	return "beast.biz." + strings.Join(pkgParts, ".")
}
