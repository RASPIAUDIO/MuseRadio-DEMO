param(
  [Parameter(Mandatory = $true)]
  [string]$File,
  [string]$HostAddress = "192.168.1.194",
  [string]$Name = "Muse Radio-3BD8",
  [int]$Volume = 45,
  [switch]$NoScan
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
$VenvDir = Join-Path $RepoRoot ".venv-airplay-test"
$PythonExe = Join-Path $VenvDir "Scripts\python.exe"
$AtvRemote = Join-Path $VenvDir "Scripts\atvremote.exe"
$ResolvedFile = (Resolve-Path -LiteralPath $File).Path

if (!(Test-Path -LiteralPath $PythonExe)) {
  python -m venv $VenvDir
}

& $PythonExe -m pip install --disable-pip-version-check --quiet "pyatv==0.17.0"

if (!$NoScan) {
  Write-Host "Scanning RAOP on $HostAddress..."
  & $AtvRemote --scan-hosts $HostAddress --scan-protocols raop scan
}

Write-Host "Streaming $ResolvedFile to $Name..."
& $AtvRemote --scan-hosts $HostAddress --scan-protocols raop -n $Name "set_volume=$Volume" "stream_file=$ResolvedFile"
exit $LASTEXITCODE
