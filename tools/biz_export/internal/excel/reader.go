package excel

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"github.com/beastserver/biz_export/internal/model"
	"github.com/xuri/excelize/v2"
)

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
		if err != nil || len(rows) < 3 {
			continue
		}

		fields := parseHeaders(rows[0], rows[1])
		if len(fields) == 0 {
			continue
		}

		logicalName, msgName, protoRelDir := model.BuildLogicalName(rawRoot, xlsxPath, sheetName)
		var dataRows [][]string
		for i := 3; i < len(rows); i++ {
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

func parseHeaders(row1, row2 []string) []model.FieldInfo {
	var fields []model.FieldInfo
	for i, cell1 := range row1 {
		cell1 = strings.TrimSpace(cell1)
		if cell1 == "" {
			continue
		}

		start := strings.Index(cell1, "(")
		end := strings.Index(cell1, ")")
		if start == -1 || end == -1 || end <= start {
			continue
		}

		name := strings.TrimSpace(cell1[:start])
		protoType := strings.TrimSpace(cell1[start+1 : end])
		if name == "" || protoType == "" {
			continue
		}

		cell2 := ""
		if i < len(row2) {
			cell2 = strings.TrimSpace(row2[i])
		}

		constraint, visibility := splitConstraintVisibility(cell2)
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

func splitConstraintVisibility(cell2 string) (string, string) {
	parts := strings.SplitN(cell2, "/", 2)
	constraint := strings.TrimSpace(parts[0])
	visibility := ""
	if len(parts) == 2 {
		visibility = strings.TrimSpace(parts[1])
	}
	return constraint, visibility
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
