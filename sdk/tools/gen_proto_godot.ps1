#Requires -Version 5.1
<#
.SYNOPSIS
  Beast SDK platform proto 生成（Windows）：routes + message 编解码。

.DESCRIPTION
  读 proto_manifest.json，对 platform proto 生成：
  - auth_routes.gd（gen_routes_from_proto.py）
  - envelope/auth message（gen_messages_from_proto.py，protoc descriptor）
#>
$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$SdkRoot = Split-Path -Parent $ScriptDir
$RepoRoot = Split-Path -Parent $SdkRoot
$ProtoRoot = Join-Path $RepoRoot "bizconfig\protocol"
$Manifest = Join-Path $ScriptDir "proto_manifest.json"
$OutDir = Join-Path $SdkRoot "godot\beast_sdk\generated"
$GenRoutes = Join-Path $ScriptDir "gen_routes_from_proto.py"
$GenMessages = Join-Path $ScriptDir "gen_messages_from_proto.py"
$WireCodecPreload = "res://beast_sdk/impl/codec/wire_codec.gd"
$LoadPrefix = "res://beast_sdk/generated/"

if (-not (Test-Path $ProtoRoot)) {
    Write-Error "Proto root not found: $ProtoRoot"
}

if (-not (Test-Path $OutDir)) {
    New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
}

$python = $null
foreach ($cmd in @("python", "python3")) {
    if (Get-Command $cmd -ErrorAction SilentlyContinue) {
        $python = $cmd
        break
    }
}
if (-not $python) {
    Write-Error "python not found in PATH"
}

Write-Host "Proto root : $ProtoRoot"
Write-Host "Output dir : $OutDir"
Write-Host ""

$manifest = Get-Content $Manifest -Raw | ConvertFrom-Json
$generatedMessageProtos = @{}

foreach ($entry in $manifest.v1_client_tcp_routes) {
    $protoPath = Join-Path $ProtoRoot ($entry.proto -replace "/", "\")
    if (-not (Test-Path $protoPath)) {
        Write-Warning "[MISSING routes] $($entry.proto)"
        continue
    }

    $routesOut = Join-Path $OutDir $entry.out
    & $python $GenRoutes `
        --proto $protoPath `
        --out $routesOut `
        --class-name $entry.class_name
    if ($LASTEXITCODE -ne 0) {
        Write-Error "gen_routes_from_proto failed: $protoPath"
    }
    Write-Host "[routes] $($entry.out)"
}

foreach ($entry in $manifest.v1_client_tcp_messages) {
    $protoPath = Join-Path $ProtoRoot ($entry.proto -replace "/", "\")
    if (-not (Test-Path $protoPath)) {
        Write-Warning "[MISSING messages] $($entry.proto)"
        continue
    }
    Write-Host "[OK] $($entry.proto)"

    if (-not $generatedMessageProtos.ContainsKey($protoPath)) {
        & $python $GenMessages `
            --proto $protoPath `
            --out-dir $OutDir `
            --wire-codec-preload $WireCodecPreload `
            --load-prefix $LoadPrefix `
            --proto-include $ProtoRoot
        if ($LASTEXITCODE -ne 0) {
            Write-Error "gen_messages_from_proto failed: $protoPath"
        }
        $generatedMessageProtos[$protoPath] = $true
    }
}

Write-Host "Done."
