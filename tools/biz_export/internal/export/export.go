package export

import (
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"

	"github.com/beastserver/biz_export/internal/excel"
	"github.com/beastserver/biz_export/internal/model"
	bizproto "github.com/beastserver/biz_export/internal/proto"
	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/reflect/protodesc"
	"google.golang.org/protobuf/reflect/protoreflect"
	"google.golang.org/protobuf/reflect/protoregistry"
	"google.golang.org/protobuf/types/descriptorpb"
	"google.golang.org/protobuf/types/dynamicpb"
)

type Side int

const (
	SideServer Side = iota
	SideClient
)

func WriteTable(
	sheet model.SheetResult,
	fds *descriptorpb.FileDescriptorSet,
	side Side,
	outputDir string,
) (string, int, error) {
	fields := sheet.ServerFields()
	configSuffix := "ServerConfig"
	rowSuffix := "RowServer"
	fileSuffix := "_s.pb"
	if side == SideClient {
		fields = sheet.ClientFields()
		configSuffix = "ClientConfig"
		rowSuffix = "RowClient"
		fileSuffix = "_c.pb"
	}
	if len(fields) == 0 {
		return "", 0, fmt.Errorf("no exportable fields for side=%d table=%s", side, sheet.LogicalName)
	}

	files, err := buildRegistry(fds)
	if err != nil {
		return "", 0, err
	}

	configName := protoreflect.FullName(sheet.PackageName + "." + sheet.MsgName + configSuffix)
	rowName := protoreflect.FullName(sheet.PackageName + "." + sheet.MsgName + rowSuffix)

	configDesc, err := files.FindDescriptorByName(configName)
	if err != nil {
		return "", 0, fmt.Errorf("find config message %s: %w", configName, err)
	}
	rowDesc, err := files.FindDescriptorByName(rowName)
	if err != nil {
		return "", 0, fmt.Errorf("find row message %s: %w", rowName, err)
	}

	configMsgDesc := configDesc.(protoreflect.MessageDescriptor)
	rowMsgDesc := rowDesc.(protoreflect.MessageDescriptor)
	configMsg := dynamicpb.NewMessage(configMsgDesc)
	rowsField := configMsgDesc.Fields().ByName("rows")
	if rowsField == nil {
		return "", 0, fmt.Errorf("config message missing rows field")
	}

	rowCount := 0
	for _, dataRow := range sheet.DataRows {
		rowMsg := dynamicpb.NewMessage(rowMsgDesc)
		hasValue := false
		for _, field := range fields {
			value := strings.TrimSpace(excel.CellValue(dataRow, field.ColIndex))
			if value == "" {
				continue
			}
			fieldDesc := rowMsgDesc.Fields().ByName(protoreflect.Name(field.Name))
			if fieldDesc == nil {
				return "", 0, fmt.Errorf("field %s missing in %s", field.Name, rowName)
			}
			if err := setFieldValue(rowMsg, fieldDesc, field.ProtoType, value); err != nil {
				return "", 0, fmt.Errorf("set field %s: %w", field.Name, err)
			}
			hasValue = true
		}
		if !hasValue {
			continue
		}
		list := configMsg.Mutable(rowsField).List()
		list.Append(protoreflect.ValueOfMessage(rowMsg))
		rowCount++
	}

	payload, err := proto.Marshal(configMsg.Interface())
	if err != nil {
		return "", 0, fmt.Errorf("marshal config: %w", err)
	}

	relDir := filepath.Dir(sheet.LogicalName)
	baseName := bizproto.OutputBaseName(sheet.LogicalName) + fileSuffix
	targetDir := outputDir
	if relDir != "." && relDir != "" {
		targetDir = filepath.Join(outputDir, relDir)
	}
	if err := os.MkdirAll(targetDir, 0o755); err != nil {
		return "", 0, fmt.Errorf("mkdir output dir: %w", err)
	}

	targetPath := filepath.Join(targetDir, baseName)
	if err := os.WriteFile(targetPath, payload, 0o644); err != nil {
		return "", 0, fmt.Errorf("write pb: %w", err)
	}

	relPath := filepath.ToSlash(filepath.Join(relDir, baseName))
	if relDir == "." || relDir == "" {
		relPath = baseName
	}
	return relPath, rowCount, nil
}

func buildRegistry(fds *descriptorpb.FileDescriptorSet) (*protoregistry.Files, error) {
	files := &protoregistry.Files{}
	for _, fd := range fds.GetFile() {
		file, err := protodesc.NewFile(fd, files)
		if err != nil {
			return nil, fmt.Errorf("create file descriptor: %w", err)
		}
		if err := files.RegisterFile(file); err != nil {
			if !strings.Contains(err.Error(), "already registered") {
				return nil, fmt.Errorf("register file: %w", err)
			}
		}
	}
	return files, nil
}

func setFieldValue(
	msg protoreflect.Message,
	fieldDesc protoreflect.FieldDescriptor,
	protoType string,
	value string,
) error {
	switch protoType {
	case "uint32", "fixed32":
		parsed, err := strconv.ParseUint(value, 10, 32)
		if err != nil {
			return err
		}
		msg.Set(fieldDesc, protoreflect.ValueOfUint32(uint32(parsed)))
	case "uint64", "fixed64":
		parsed, err := strconv.ParseUint(value, 10, 64)
		if err != nil {
			return err
		}
		msg.Set(fieldDesc, protoreflect.ValueOfUint64(parsed))
	case "int32", "sint32", "sfixed32":
		parsed, err := strconv.ParseInt(value, 10, 32)
		if err != nil {
			return err
		}
		msg.Set(fieldDesc, protoreflect.ValueOfInt32(int32(parsed)))
	case "int64", "sint64", "sfixed64":
		parsed, err := strconv.ParseInt(value, 10, 64)
		if err != nil {
			return err
		}
		msg.Set(fieldDesc, protoreflect.ValueOfInt64(parsed))
	case "float":
		parsed, err := strconv.ParseFloat(value, 32)
		if err != nil {
			return err
		}
		msg.Set(fieldDesc, protoreflect.ValueOfFloat32(float32(parsed)))
	case "double":
		parsed, err := strconv.ParseFloat(value, 64)
		if err != nil {
			return err
		}
		msg.Set(fieldDesc, protoreflect.ValueOfFloat64(parsed))
	case "bool":
		msg.Set(fieldDesc, protoreflect.ValueOfBool(value == "true"))
	case "string":
		msg.Set(fieldDesc, protoreflect.ValueOfString(value))
	default:
		return fmt.Errorf("unsupported proto type: %s", protoType)
	}
	return nil
}
