package main

import (
	"fmt"
	"strconv"

	"github.com/xuri/excelize/v2"
)

func main() {
	const xlsxPath = "../../bizconfig/static-xlsx/moba/pixel_moba/unit.xlsx"

	f, err := excelize.OpenFile(xlsxPath)
	if err != nil {
		panic(err)
	}
	defer f.Close()

	const sheet = "Unit"

	// Column AK=37 (is_ranged), AL=38 (attack_projectile_speed)
	// 现有 36 字段(A-AJ),追加 2 列
	headers := []struct {
		col, name, ptype, constraint, visibility, comment string
	}{
		{"AK", "is_ranged", "bool", "notnull", "!s!c", "是否远程攻击"},
		{"AL", "attack_projectile_speed", "float", "", "!s!c", "远程平A弹道速度"},
	}

	for _, h := range headers {
		f.SetCellValue(sheet, h.col+"1", h.name)
		f.SetCellValue(sheet, h.col+"2", h.ptype)
		f.SetCellValue(sheet, h.col+"3", h.constraint)
		f.SetCellValue(sheet, h.col+"4", h.visibility)
		f.SetCellValue(sheet, h.col+"5", h.comment)
	}

	// 数据行 6-15 (10 行:3 hero + 1 minion + 1 tower + 5 monster)
	// is_ranged: 仅 hero 1002(法师 row7) / 1003(弓箭手 row8) 为 true,其余 false
	// attack_projectile_speed: 法师=600, 弓箭手=800, 其余=0
	type rowData struct {
		row           int
		isRanged      string
		projSpeed     float64
	}
	rows := []rowData{
		{6, "false", 0},   // hero 1001 战士
		{7, "true", 600},  // hero 1002 法师
		{8, "true", 800},  // hero 1003 弓箭手
		{9, "false", 0},   // minion 2001 近战兵
		{10, "false", 0},  // tower 3001 一塔
		{11, "false", 0},  // monster 4001 增益怪
		{12, "false", 0},  // monster 4002 狼
		{13, "false", 0},  // monster 4003 鸟
		{14, "false", 0},  // monster 4004 石头人
		{15, "false", 0},  // monster 4005 boss
	}

	for _, r := range rows {
		// bool 用字符串写入(避免 excelize 把 Go bool 写成大写 "TRUE")
		f.SetCellValue(sheet, "AK"+strconv.Itoa(r.row), r.isRanged)
		// float 直接写数值
		f.SetCellValue(sheet, "AL"+strconv.Itoa(r.row), r.projSpeed)
	}

	if err := f.Save(); err != nil {
		panic(err)
	}

	fmt.Println("unit.xlsx updated: added is_ranged(AK) + attack_projectile_speed(AL)")
	fmt.Println("  hero 1002 法师: is_ranged=true, speed=600")
	fmt.Println("  hero 1003 弓箭手: is_ranged=true, speed=800")
	fmt.Println("  其余: is_ranged=false, speed=0")
}
