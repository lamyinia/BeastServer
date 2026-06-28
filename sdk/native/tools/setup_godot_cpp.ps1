param(
    [string]$Tag = "godot-4.3-stable",
    [string]$Root = (Join-Path (Join-Path (Join-Path $PSScriptRoot "..") "thirdparty") "godot-cpp")
)

$ErrorActionPreference = "Stop"

if (Test-Path (Join-Path $Root ".git")) {
    Write-Host "godot-cpp already exists at $Root"
    exit 0
}

New-Item -ItemType Directory -Force -Path (Split-Path $Root) | Out-Null

Write-Host "Cloning godot-cpp ($Tag) into $Root ..."
git clone --branch $Tag --depth 1 https://github.com/godotengine/godot-cpp.git $Root
if ($LASTEXITCODE -ne 0) {
    Write-Error "git clone failed. Check network or clone manually into: $Root"
}

Write-Host "Done. Build with:"
Write-Host "  sdk/native/tools/build_godot_extension.ps1 -Config Release"
