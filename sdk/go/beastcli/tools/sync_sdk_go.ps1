#Requires -Version 5.1
<#
.SYNOPSIS
  sdk/go proto -> Go pb.go 同步脚本。

.DESCRIPTION
  读 register_sdk_proto.txt，调 protoc 生成 .pb.go + _grpc.pb.go 到 ../proto/。
  Go module 名必须为 beastserver-project/sdk/go（go.mod 第一行）。

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
  .\sync_sdk_go.ps1
#>

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
# $SdkGoRoot 指向 beastcli/ 根（脚本在 beastcli/tools/ 下）。
# beastcli 是唯一 Go module，proto + tools + internal 都在它下面。
$SdkGoRoot = (Resolve-Path (Join-Path $ScriptDir "..")).Path
# 从 sdk/go/beastcli/ 上溯 3 级到 repo root：beastcli → go → sdk → repo
$RepoRoot = (Resolve-Path (Join-Path $SdkGoRoot "..\..\..")).Path
$ProtoInclude = Join-Path $RepoRoot "bizconfig\protocol"
$RegisterPath = Join-Path $ScriptDir "register_sdk_proto.txt"
$GenRoot = Join-Path $SdkGoRoot "proto"
$GoModule = "beastserver-project/sdk/go/beastcli"

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

        # 解析重定向语法：src [=> dst]
        # 例如：game/example/sdk_event/sdk_event.proto => biz/sdk_event/
        # 没重定向：按 .proto 相对路径生成（platform/envelope.proto -> proto/platform/）
        # 有重定向：生成到 proto/<dst>/，M 选项用 $GoModule/proto/<dst>
        $src = $trimmed
        $dst = ""
        if ($trimmed -match '^(.+?)\s*=>\s*(.+)$') {
            $src = $matches[1].Trim()
            $dst = $matches[2].Trim().Trim("/").Trim("\").Replace("\", "/")
        }

        $relProto = $src.Trim("/").Trim("\").Replace("/", "\")
        if ($relProto.Length -eq 0) { continue }

        $absProto = Join-Path $ProtoInclude $relProto
        if (-not (Test-Path $absProto)) {
            throw "Registered proto not found: $absProto"
        }

        # DstPkg 决定 .pb.go 物理位置 + M 选项的 go_package：
        # - 没重定向：用 .proto 文件目录部分（platform/envelope.proto -> platform）
        # - 有重定向：用 dst（biz/sdk_event）
        $dstPkg = $dst
        if ($dstPkg -eq "") {
            $relFwd = $relProto -replace "\\", "/"
            $dirPart = Split-Path $relFwd -Parent
            $dstPkg = $dirPart
        }

        $null = $entries.Add([PSCustomObject]@{
            RelPath = $relProto
            AbsPath = $absProto
            DstPkg  = $dstPkg
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
    # 规则：取 entry.DstPkg（重定向后的目标包路径），前面加 "$GoModule/proto/"。
    # 例如 platform/envelope.proto -> beastserver-project/sdk/go/beastcli/proto/platform
    # 例如 game/example/sdk_event/sdk_event.proto (=> biz/sdk_event/) -> beastserver-project/sdk/go/beastcli/proto/biz/sdk_event
    $mOpts = New-Object System.Collections.ArrayList
    foreach ($entry in $entries) {
        $relFwd = $entry.RelPath -replace "\\", "/"
        $dstPkg = $entry.DstPkg
        if ($dstPkg -eq "") {
            $goPkg = "$GoModule/proto"
        } else {
            $goPkg = "$GoModule/proto/$dstPkg"
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

    # 对有重定向的 entry，把 .pb.go 文件从原路径 Move 到目标路径。
    # paths=source_relative 按 .proto 相对路径生成，与 DstPkg 不一致时需要重定向。
    # 例如 sdk_event.proto 生成到 proto/game/example/sdk_event/sdk_event.pb.go，
    # 但 DstPkg=biz/sdk_event，需要 Move 到 proto/biz/sdk_event/sdk_event.pb.go。
    foreach ($entry in $entries) {
        $relFwd = $entry.RelPath -replace "\\", "/"
        $dstPkg = $entry.DstPkg

        $protoBase = [System.IO.Path]::GetFileNameWithoutExtension($relFwd)
        $srcDir = Split-Path $relFwd -Parent

        # 原生成路径（按 .proto 相对路径）
        if ($srcDir -eq "") {
            $srcGenPath = Join-Path $GenRoot "$protoBase.pb.go"
        } else {
            $srcGenPath = Join-Path $GenRoot (Join-Path $srcDir.Replace("/", "\") "$protoBase.pb.go")
        }

        # srcDir 与 dstPkg 一致就不动（platform/envelope.proto -> DstPkg=platform，无需 Move）
        $srcDirFwd = $srcDir -replace "\\", "/"
        if ($srcDirFwd -eq $dstPkg) {
            continue
        }
        if (-not (Test-Path $srcGenPath)) {
            continue  # 没生成（可能 .proto 没 message，protoc 不输出 .pb.go）
        }

        # 目标路径
        $dstDir = if ($dstPkg -eq "") { $GenRoot } else { Join-Path $GenRoot ($dstPkg.Replace("/", "\")) }
        if (-not (Test-Path $dstDir)) {
            New-Item -ItemType Directory -Path $dstDir -Force | Out-Null
        }
        $dstGenPath = Join-Path $dstDir "$protoBase.pb.go"

        Move-Item -Force $srcGenPath $dstGenPath
        Write-Host "Redirected: $relFwd -> proto/$dstPkg/$protoBase.pb.go"

        # grpc 文件也要移（如果有）
        $srcGrpcPath = $srcGenPath.Replace(".pb.go", "_grpc.pb.go")
        if (Test-Path $srcGrpcPath) {
            $dstGrpcPath = $dstGenPath.Replace(".pb.go", "_grpc.pb.go")
            Move-Item -Force $srcGrpcPath $dstGrpcPath
        }
    }

    # 清理空目录（重定向后留下的空目录树）
    Get-ChildItem -Path $GenRoot -Recurse -Directory |
        Sort-Object -Property FullName -Descending |
        Where-Object { (Get-ChildItem $_.FullName -Force | Measure-Object).Count -eq 0 } |
        ForEach-Object { Remove-Item -Recurse -Force $_.FullName }

    Write-Host ""
    Write-Host "Generated files:"
    Get-ChildItem -Path $GenRoot -Filter "*.pb.go" -Recurse -File | ForEach-Object {
        Write-Host ("  " + $_.FullName.Substring($SdkGoRoot.Length + 1))
    }
}

Invoke-Protoc
Write-Host ""
Write-Host "Done."
