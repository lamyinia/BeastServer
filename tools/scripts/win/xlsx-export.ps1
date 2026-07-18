#Requires -Version 5.1
<#
.SYNOPSIS
  Export bizconfig/static-xlsx to scheme proto + server/client pb (Windows).

.EXAMPLE
  # From BeastServer-project root
  .\tools\scripts\win\xlsx-export.ps1
#>

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = (Resolve-Path (Join-Path $ScriptDir "..\..\..")).Path

$OutRoot = Join-Path $RepoRoot "beastserver\build\bizconfig"
$RawDir = Join-Path $RepoRoot "bizconfig\static-xlsx"
$SchemaDir = Join-Path $RepoRoot "bizconfig\scheme"
$ExportBin = Join-Path $RepoRoot "tools\biz_export\biz_export.exe"
$ExportBinSh = Join-Path $RepoRoot "tools\biz_export\biz_export"

New-Item -ItemType Directory -Force -Path (Join-Path $OutRoot "server") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $OutRoot "client") | Out-Null

if (-not (Test-Path $ExportBin)) {
    if (-not (Test-Path $ExportBinSh)) {
        Write-Error "biz_export not built. Run: cd tools/biz_export; go build -o biz_export.exe ."
    }
    $ExportBin = $ExportBinSh
}

& $ExportBin `
    --raw $RawDir `
    --schema $SchemaDir `
    --server (Join-Path $OutRoot "server") `
    --client (Join-Path $OutRoot "client") `
    --manifest (Join-Path $OutRoot "manifest.json")

if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Write-Host "Export complete -> $OutRoot"
