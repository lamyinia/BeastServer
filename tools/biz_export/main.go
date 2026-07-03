package main

import (
	"crypto/sha256"
	"encoding/hex"
	"flag"
	"fmt"
	"log"
	"os"
	"path/filepath"

	"github.com/beastserver/biz_export/internal/excel"
	"github.com/beastserver/biz_export/internal/export"
	"github.com/beastserver/biz_export/internal/manifest"
	"github.com/beastserver/biz_export/internal/model"
	bizproto "github.com/beastserver/biz_export/internal/proto"
	"github.com/beastserver/biz_export/internal/validate"
)

func main() {
	rawDir := flag.String("raw", "", "Excel source root (bizconfig/static-xlsx)")
	schemaDir := flag.String("schema", "", "Table scheme output root (bizconfig/scheme)")
	serverDir := flag.String("server", "", "Server pb output directory")
	clientDir := flag.String("client", "", "Client pb output directory")
	manifestPath := flag.String("manifest", "", "Manifest json output path")
	flag.Parse()

	if *rawDir == "" || *schemaDir == "" || *serverDir == "" || *clientDir == "" || *manifestPath == "" {
		fmt.Fprintln(os.Stderr, "usage: biz_export --raw <dir> --schema <dir> --server <dir> --client <dir> --manifest <file>")
		flag.PrintDefaults()
		os.Exit(2)
	}

	xlsxFiles, err := excel.ScanExcelFiles(*rawDir)
	if err != nil {
		log.Fatalf("scan excel files: %v", err)
	}
	if len(xlsxFiles) == 0 {
		log.Fatalf("no .xlsx files under %s", *rawDir)
	}

	fmt.Printf("biz_export: found %d excel file(s)\n", len(xlsxFiles))

	var outputs []model.TableOutput
	hasError := false

	for _, xlsxPath := range xlsxFiles {
		sheets, err := excel.ProcessWorkbook(*rawDir, xlsxPath)
		if err != nil {
			log.Printf("[ERROR] %s: %v", xlsxPath, err)
			hasError = true
			continue
		}

		for _, sheet := range sheets {
			if errs := validate.Sheet(sheet); len(errs) > 0 {
				hasError = true
				fmt.Printf("\n[FAIL] %s validation errors (%d):\n", sheet.LogicalName, len(errs))
				for _, item := range errs {
					fmt.Printf("  - %s\n", item.String())
				}
				continue
			}

			protoPath, err := bizproto.GenerateSchema(sheet, *schemaDir)
			if err != nil {
				log.Printf("[ERROR] generate proto %s: %v", sheet.LogicalName, err)
				hasError = true
				continue
			}
			fmt.Printf("[OK] %s -> proto %s (rows=%d, s_fields=%d, c_fields=%d)\n",
				sheet.LogicalName,
				protoPath,
				len(sheet.DataRows),
				len(sheet.ServerFields()),
				len(sheet.ClientFields()),
			)

			fds, err := bizproto.CompileDescriptorSet(protoPath)
			if err != nil {
				log.Printf("[ERROR] compile proto %s: %v", protoPath, err)
				hasError = true
				continue
			}

			serverRel, rowCount, err := export.WriteTable(sheet, fds, export.SideServer, *serverDir)
			if err != nil {
				log.Printf("[ERROR] export server pb %s: %v", sheet.LogicalName, err)
				hasError = true
				continue
			}
			serverAbs := filepath.Join(*serverDir, filepath.FromSlash(serverRel))
			serverHash, err := fileSHA256(serverAbs)
			if err != nil {
				log.Printf("[ERROR] hash server pb %s: %v", serverAbs, err)
				hasError = true
				continue
			}
			fmt.Printf("  -> server %s (%d bytes, rows=%d)\n", serverRel, fileSize(serverAbs), rowCount)

			// 纯服务端表(无 !c 字段)跳过 client pb 生成,不视为错误
			var clientRel, clientHash string
			if len(sheet.ClientFields()) == 0 {
				fmt.Printf("  -> (skip client pb: server-only table)\n")
			} else {
				var err2 error
				clientRel, _, err2 = export.WriteTable(sheet, fds, export.SideClient, *clientDir)
				if err2 != nil {
					log.Printf("[ERROR] export client pb %s: %v", sheet.LogicalName, err2)
					hasError = true
					continue
				}
				clientAbs := filepath.Join(*clientDir, filepath.FromSlash(clientRel))
				clientHash, err2 = fileSHA256(clientAbs)
				if err2 != nil {
					log.Printf("[ERROR] hash client pb %s: %v", clientAbs, err2)
					hasError = true
					continue
				}
				fmt.Printf("  -> client %s (%d bytes)\n", clientRel, fileSize(clientAbs))
			}

			outputs = append(outputs, model.TableOutput{
				LogicalName:  sheet.LogicalName,
				ServerFile:   serverRel,
				ClientFile:   clientRel,
				ServerSchema: bizproto.ServerSchemaName(sheet),
				ClientSchema: bizproto.ClientSchemaName(sheet),
				RowCount:     rowCount,
				ServerSHA256: serverHash,
				ClientSHA256: clientHash,
			})
		}
	}

	if hasError {
		fmt.Println("\n========== export failed ==========")
		os.Exit(1)
	}

	if err := manifest.Write(*manifestPath, outputs); err != nil {
		log.Fatalf("write manifest: %v", err)
	}

	fmt.Printf("\n========== export complete ==========\n")
	fmt.Printf("tables=%d manifest=%s\n", len(outputs), *manifestPath)
}

func fileSHA256(path string) (string, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return "", err
	}
	sum := sha256.Sum256(data)
	return hex.EncodeToString(sum[:]), nil
}

func fileSize(path string) int64 {
	info, err := os.Stat(path)
	if err != nil {
		return 0
	}
	return info.Size()
}
