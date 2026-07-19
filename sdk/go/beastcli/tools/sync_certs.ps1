#Requires -Version 5.1
<#
.SYNOPSIS
  Sync beastserver CA cert to sdk/go/beastcli/certificate/.

.DESCRIPTION
  Copies beastserver/config/certs/ca/ca_cert.pem to sdk/go/beastcli/certificate/,
  so SDK unit/e2e tests can load the CA trust anchor via a relative path
  without walking up to the repo root.

  Only the CA cert is synced (client verifies server).
  Server cert/key are NOT synced (private keys stay in beastserver/;
  server-side mTLS verify_client is currently disabled).

.EXAMPLE
  .\sync_certs.ps1
#>

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
# $SdkGoRoot points to beastcli/ root (script lives in beastcli/tools/).
$SdkGoRoot = (Resolve-Path (Join-Path $ScriptDir "..")).Path
# Walk up 3 levels from sdk/go/beastcli/ to repo root: beastcli -> go -> sdk -> repo.
$RepoRoot = (Resolve-Path (Join-Path $SdkGoRoot "..\..\..")).Path

$SrcCaCert = Join-Path $RepoRoot "beastserver\config\certs\ca\ca_cert.pem"
$DstDir    = Join-Path $SdkGoRoot "certificate"
$DstCaCert = Join-Path $DstDir "ca_cert.pem"

if (-not (Test-Path $SrcCaCert)) {
    $msg = "CA cert not found: $SrcCaCert`n" +
           "Prerequisite missing. Generate CA cert under beastserver/config/certs/ca/ first."
    throw $msg
}

if (-not (Test-Path $DstDir)) {
    New-Item -ItemType Directory -Path $DstDir -Force | Out-Null
    Write-Host "Created: $DstDir"
}

Copy-Item -Force $SrcCaCert $DstCaCert
Write-Host "Synced: ca_cert.pem"
Write-Host "  src: $SrcCaCert"
Write-Host "  dst: $DstCaCert"
Write-Host ""
Write-Host "Done."
