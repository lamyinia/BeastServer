#Requires -Version 5.1
<#
.SYNOPSIS
  从 bizconfig 生成 sdk/godot/demo/generated/（routes + message 编解码）。

.DESCRIPTION
  读 sdk/godot/demo/register_protocol.txt，扫描对应 proto 目录，输出到 demo/generated/（扁平目录）。

.EXAMPLE
  .\sync_demo_generated.ps1
#>
$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$SdkRoot = Split-Path -Parent $ScriptDir
$RepoRoot = Split-Path -Parent $SdkRoot
$DemoRoot = Join-Path $SdkRoot "godot\demo"
$RegisterPath = Join-Path $DemoRoot "register_protocol.txt"
$ProtoInclude = Join-Path $RepoRoot "bizconfig\protocol"
$GameProtoRoot = Join-Path $ProtoInclude "game"
$GenRoutesScript = Join-Path $ScriptDir "gen_routes_from_proto.py"
$GenMessagesScript = Join-Path $ScriptDir "gen_messages_from_proto.py"
$GeneratedRoot = Join-Path $DemoRoot "generated"
$WireCodecPreload = "res://beast_sdk/impl/codec/wire_codec.gd"
$LoadPrefix = "res://demo/generated/"

function Get-PythonCommand {
    foreach ($cmd in @("python", "python3")) {
        if (Get-Command $cmd -ErrorAction SilentlyContinue) {
            return $cmd
        }
    }
    throw "python not found in PATH"
}

function Get-RegisterEntries([string]$Path) {
    if (-not (Test-Path $Path)) {
        throw "Register file not found: $Path"
    }

    $entries = @()
    Get-Content $Path | ForEach-Object {
        $line = $_.Trim()
        if ($line.Length -eq 0 -or $line.StartsWith("#")) {
            return
        }
        $gameSubdir = $line.Trim().Trim("/").Trim("\")
        if ($gameSubdir.Length -gt 0) {
            $entries += $gameSubdir
        }
    }

    if ($entries.Count -eq 0) {
        throw "No entries in register file: $Path"
    }
    return $entries
}

function Get-RoutesClassName([string]$ProtoStem) {
    $parts = $ProtoStem -split "_"
    $title = ($parts | ForEach-Object {
        if ($_.Length -eq 0) { return "" }
        $_.Substring(0, 1).ToUpper() + $_.Substring(1)
    }) -join ""
    return "${title}Routes"
}

if (-not (Test-Path $GeneratedRoot)) {
    New-Item -ItemType Directory -Path $GeneratedRoot -Force | Out-Null
}

$python = Get-PythonCommand
foreach ($gameSubdir in (Get-RegisterEntries $RegisterPath)) {
    $inputDir = Join-Path $GameProtoRoot $gameSubdir
    if (-not (Test-Path $inputDir)) {
        throw "Registered proto dir not found: $inputDir"
    }

    $protoFiles = Get-ChildItem -Path $inputDir -Filter "*.proto" -Recurse -File | Sort-Object FullName
    if ($protoFiles.Count -eq 0) {
        Write-Warning "no .proto in $gameSubdir"
    }

    foreach ($protoFile in $protoFiles) {
        $routesOut = Join-Path $GeneratedRoot ($protoFile.BaseName + "_routes.gd")
        $className = Get-RoutesClassName $protoFile.BaseName

        & $python $GenRoutesScript `
            --proto $protoFile.FullName `
            --out $routesOut `
            --class-name $className `
            --skip-if-no-routes
        if ($LASTEXITCODE -ne 0) {
            throw "gen_routes_from_proto failed: $($protoFile.FullName)"
        }
        if (Test-Path $routesOut) {
            Write-Host "generated routes -> $routesOut"
        }

        & $python $GenMessagesScript `
            --proto $protoFile.FullName `
            --out-dir $GeneratedRoot `
            --wire-codec-preload $WireCodecPreload `
            --load-prefix $LoadPrefix `
            --proto-include $ProtoInclude
        if ($LASTEXITCODE -ne 0) {
            throw "gen_messages_from_proto failed: $($protoFile.FullName)"
        }
    }
}

Write-Host "Done."
