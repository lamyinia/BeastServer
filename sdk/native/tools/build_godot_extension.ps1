param(
    [ValidateSet("Debug", "Release", "Both")]
    [string]$Config = "Both",
    [string]$GodotCppTag = "godot-4.3-stable"
)

$ErrorActionPreference = "Stop"
$NativeRoot = Resolve-Path (Join-Path $PSScriptRoot "..")

& (Join-Path $PSScriptRoot "setup_godot_cpp.ps1") -Tag $GodotCppTag
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$BuildDir = Join-Path $NativeRoot "build-godot"
cmake -S $NativeRoot -B $BuildDir -DBEAST_CLIENT_BUILD_GODOT=ON -DBEAST_CLIENT_BUILD_C_API=OFF -DBEAST_CLIENT_BUILD_TESTS=OFF
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$configs = @()
if ($Config -eq "Both") {
    $configs = @("Debug", "Release")
} else {
    $configs = @($Config)
}

foreach ($cfg in $configs) {
    Write-Host "Building GDExtension ($cfg) ..."
    cmake --build $BuildDir --config $cfg
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

Write-Host "GDExtension binaries copied to sdk/godot/beast_sdk_native/bin/"
