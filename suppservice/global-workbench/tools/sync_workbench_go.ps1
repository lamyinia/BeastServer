#Requires -Version 5.1
<#
.SYNOPSIS
  global-workbench proto -> Go pb.go 同步脚本。

.DESCRIPTION
  读 register_workbench_proto.txt，调 protoc 生成 .pb.go + _grpc.pb.go 到 ../proto/。
  Go module 名必须为 global-workbench（go.mod 第一行）。

  前置依赖：
    1. protoc 已装并加入 PATH（protoc --version 可跑）
    2. protoc-gen-go 已装到 GOPATH/bin 并加入 PATH
    3. protoc-gen-go-grpc 已装到 GOPATH/bin 并加入 PATH
    4. Go 1.25+（推荐 D:\golang-sdk\go1.25.1）

  安装命令（首次）：
    $env:GOROOT = "D:\golang-sdk\go1.25.1"
    $env:PATH = "D:\golang-sdk\go1.25.1\bin;D:\golang-sdk\go-path\bin;$env:PATH"
    $env:GOPROXY = "https://goproxy.cn,direct"
    $env:GOSUMDB = "off"
    go install google.golang.org/protobuf/cmd/protoc-gen-go@latest
    go install google.golang.org/grpc/cmd/protoc-gen-go-grpc@latest

.EXAMPLE
  .\sync_workbench_go.ps1
#>

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$WorkbenchRoot = (Resolve-Path (Join-Path $ScriptDir "..")).Path
$RepoRoot = (Resolve-Path (Join-Path $WorkbenchRoot "..\..")).Path
$ProtoInclude = Join-Path $RepoRoot "bizconfig\protocol"
$RegisterPath = Join-Path $ScriptDir "register_workbench_proto.txt"
$GenRoot = Join-Path $WorkbenchRoot "proto"
$GoModule = "global-workbench"

function Assert-Tool([string]$Name, [string]$VersionArgs = "--version") {
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw @"
$Name not found in PATH.

前置依赖未满足。请在 PowerShell 里执行：

  `$env:GOROOT = "D:\golang-sdk\go1.25.1"
  `$env:PATH = "D:\golang-sdk\go1.25.1\bin;D:\golang-sdk\go-path\bin;`$env:PATH"
  `$env:GOPROXY = "https://goproxy.cn,direct"
  `$env:GOSUMDB = "off"
  go install google.golang.org/protobuf/cmd/protoc-gen-go@latest
  go install google.golang.org/grpc/cmd/protoc-gen-go-grpc@latest
"@
    }
}

function Get-RegisterEntries {
    if (-not (Test-Path $RegisterPath)) {
        throw "Register file not found: $RegisterPath"
    }

    # 用 ArrayList 而不是 @()，避免 PowerShell 5.1 把 $entries 当成单个 PSObject
    # 导致 += 调用 op_Addition 失败的问题。
    $entries = New-Object System.Collections.ArrayList
    foreach ($line in (Get-Content $RegisterPath)) {
        $trimmed = $line.Trim()
        if ($trimmed.Length -eq 0 -or $trimmed.StartsWith("#")) { continue }

        $relProto = $trimmed.Trim("/").Trim("\").Replace("/", "\")
        if ($relProto.Length -eq 0) { continue }

        $absProto = Join-Path $ProtoInclude $relProto
        if (-not (Test-Path $absProto)) {
            throw "Registered proto not found: $absProto"
        }

        $null = $entries.Add([PSCustomObject]@{
            RelPath = $relProto
            AbsPath = $absProto
        })
    }

    if ($entries.Count -eq 0) {
        throw "No entries in register file: $RegisterPath"
    }

    return ,$entries
}

function Invoke-Protoc {
    Assert-Tool "protoc"
    Assert-Tool "protoc-gen-go"
    Assert-Tool "protoc-gen-go-grpc"

    if (-not (Test-Path $ProtoInclude)) {
        throw "Proto include root not found: $ProtoInclude"
    }

    if (-not (Test-Path $GenRoot)) {
        New-Item -ItemType Directory -Path $GenRoot -Force | Out-Null
    }

    $entries = Get-RegisterEntries

    # protoc 接受多个 .proto 文件，一次性生成
    $protoPaths = $entries | ForEach-Object { $_.RelPath }
    # 转成数组，供 splat
    $protoArgs = @($protoPaths)

    # 为每个 proto 生成 M<path>=<go_package> 选项。
    # protoc-gen-go 必须知道每个 proto 的 Go import path，否则报错。
    # proto 源文件是跨语言共享的（C++/Godot/Go），不能加 go_package 选项，只能用 M 参数。
    # 规则：取 proto 相对路径的目录部分，前面加 "$GoModule/proto/"。
    # 例如 platform/envelope.proto -> global-workbench/proto/platform
    $mOpts = New-Object System.Collections.ArrayList
    foreach ($rel in $protoArgs) {
        $relFwd = $rel -replace "\\", "/"
        $dirPart = Split-Path $relFwd -Parent
        if ($dirPart -eq "") {
            $goPkg = "$GoModule/proto"
        } else {
            $goPkg = "$GoModule/proto/$dirPart"
        }
        $null = $mOpts.Add("M${relFwd}=${goPkg}")
    }
    $goOpts = @(
        "paths=source_relative"
    ) + $mOpts.ToArray()
    $goGrpcOpts = @(
        "paths=source_relative"
    ) + $mOpts.ToArray()

    Write-Host "protoc generating -> $GenRoot"
    $protoArgs | ForEach-Object { Write-Host "  - $_" }

    Push-Location $ProtoInclude
    try {
        # 注意：proto_path 必须是 proto 文件路径的"精确前缀"。
        # proto 文件路径用相对形式（platform\envelope.proto），proto_path 必须用 "." 才能匹配。
        # 不要用绝对路径 $ProtoInclude，否则前缀匹配失败。
        #
        # paths=source_relative：让 .pb.go 按 .proto 的相对路径生成（不加 module 前缀）。
        # M 选项：明确每个 proto 的 Go import path（避免依赖 proto 源里的 go_package 选项）。
        & protoc `
            --proto_path=. `
            --go_out=$GenRoot `
            --go_opt=$($goOpts -join ",") `
            --go-grpc_out=$GenRoot `
            --go-grpc_opt=$($goGrpcOpts -join ",") `
            @protoArgs

        if ($LASTEXITCODE -ne 0) {
            throw "protoc failed (exit=$LASTEXITCODE)"
        }
    } finally {
        Pop-Location
    }

    Write-Host ""
    Write-Host "Generated files:"
    Get-ChildItem -Path $GenRoot -Filter "*.pb.go" -Recurse -File | ForEach-Object {
        Write-Host ("  " + $_.FullName.Substring($WorkbenchRoot.Length + 1))
    }
}

Invoke-Protoc
Write-Host ""
Write-Host "Done."
