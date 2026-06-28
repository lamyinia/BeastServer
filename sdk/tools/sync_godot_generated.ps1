#Requires -Version 5.1
<#
.SYNOPSIS
  同步 sdk/godot 全部 generated：beast_sdk platform + demo consumer。

.EXAMPLE
  .\sync_godot_generated.ps1
#>
$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

& (Join-Path $ScriptDir "gen_proto_godot.ps1")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& (Join-Path $ScriptDir "sync_demo_generated.ps1")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "sync_godot_generated: all done."
