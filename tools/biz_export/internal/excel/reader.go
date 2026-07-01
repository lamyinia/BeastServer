package excel

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"github.com/beastserver/biz_export/internal/model"
	"github.com/xuri/excelize/v2"
)

const HeaderRows = 5

func ScanExcelFiles(root string) ([]string, error) {
	var files []string
	err := filepath.Walk(root, func(path string, info os.FileInfo, walkErr error) error {
		if walkErr != nil {
			return walkErr
		}
		if info.IsDir() {
			return nil
		}
		name := info.Name()
		if strings.HasPrefix(name, "~$") {
			return nil
		}
		if strings.EqualFold(filepath.Ext(name), ".xlsx") {
			files = append(files, path)
		}
		return nil
	})
	return files, err
}

func ProcessWorkbook(rawRoot, xlsxPath string) ([]model.SheetResult, error) {
	book, err := excelize.OpenFile(xlsxPath)
	if err != nil {
		return nil, fmt.Errorf("open xlsx: %w", err)
	}
	defer book.Close()

	var results []model.SheetResult
	for _, sheetName := range book.GetSheetList() {
		rows, err := book.GetRows(sheetName)
		if err != nil || len(rows) < HeaderRows {
			continue
		}

		fields := parseHeaders(rows[0], rows[1], rows[2], rows[3])
		if len(fields) == 0 {
			continue
		}

		logicalName, msgName, protoRelDir := model.BuildLogicalName(rawRoot, xlsxPath, sheetName)
		var dataRows [][]string
		for i := HeaderRows; i < len(rows); i++ {
			if isEmptyRow(rows[i]) {
				continue
			}
			dataRows = append(dataRows, rows[i])
		}

		results = append(results, model.SheetResult{
			LogicalName: logicalName,
			SheetName:   sheetName,
			MsgName:     msgName,
			PackageName: model.PackageNameFromLogical(logicalName),
			ProtoRelDir: protoRelDir,
			Fields:      fields,
			DataRows:    dataRows,
			SourceXLSX:  xlsxPath,
		})
	}

	if len(results) == 0 {
		return nil, fmt.Errorf("no exportable sheets in %s", xlsxPath)
	}
	return results, nil
}

func parseHeaders(nameRow, typeRow, constraintRow, visibilityRow []string) []model.FieldInfo {
	var fields []model.FieldInfo
	for i, nameCell := range nameRow {
		name := strings.TrimSpace(nameCell)
		if name == "" {
			continue
		}

		protoType := ""
		if i < len(typeRow) {
			protoType = strings.TrimSpace(typeRow[i])
		}
		if protoType == "" {
			continue
		}

		constraint := ""
		if i < len(constraintRow) {
			constraint = strings.TrimSpace(constraintRow[i])
		}

		visibility := ""
		if i < len(visibilityRow) {
			visibility = strings.TrimSpace(visibilityRow[i])
		}

		visibleS, visibleC, skip := parseVisibility(visibility)
		if skip || (!visibleS && !visibleC) {
			continue
		}

		fields = append(fields, model.FieldInfo{
			Name:       name,
			ProtoType:  protoType,
			Constraint: constraint,
			VisibleS:   visibleS,
			VisibleC:   visibleC,
			ColIndex:   i,
		})
	}
	return fields
}

func parseVisibility(visibility string) (visibleS, visibleC, skip bool) {
	visibleS = true
	visibleC = true
	if visibility == "" {
		return visibleS, visibleC, false
	}
	if !strings.Contains(visibility, "!") {
		return visibleS, visibleC, false
	}

	visibleS = strings.Contains(visibility, "!s")
	visibleC = strings.Contains(visibility, "!c")
	adminOnly := strings.Contains(visibility, "!a")
	if adminOnly && !visibleS && !visibleC {
		return false, false, true
	}
	return visibleS, visibleC, false
}

func isEmptyRow(row []string) bool {
	for _, cell := range row {
		if strings.TrimSpace(cell) != "" {
			return false
		}
	}
	return true
}

func CellValue(row []string, colIndex int) string {
	if colIndex < len(row) {
		return row[colIndex]
	}
	return ""
}
