param(
  [string]$HostAddress = "192.168.1.194",
  [string]$Name = "Muse Radio-3BD8",
  [int]$Volume = 55,
  [int]$Frequency = 880,
  [int]$Duration = 8,
  [string]$File = "",
  [switch]$ScanOnly
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
$VenvDir = Join-Path $RepoRoot ".venv-airplay-test"
$PythonExe = Join-Path $VenvDir "Scripts\python.exe"
$AtvRemote = Join-Path $VenvDir "Scripts\atvremote.exe"

if (!(Test-Path -LiteralPath $PythonExe)) {
  python -m venv $VenvDir
}

& $PythonExe -m pip install --disable-pip-version-check --quiet "pyatv==0.17.0"

Write-Host "Scanning RAOP on $HostAddress..."
& $AtvRemote --scan-hosts $HostAddress --scan-protocols raop scan

if ($ScanOnly) {
  exit 0
}

if ($File) {
  $ResolvedFile = (Resolve-Path -LiteralPath $File).Path
  Write-Host "Streaming $ResolvedFile to $Name..."
  & $AtvRemote --scan-hosts $HostAddress --scan-protocols raop -n $Name "set_volume=$Volume" "stream_file=$ResolvedFile"
  exit $LASTEXITCODE
}

$Ffmpeg = "C:\ffmpeg\bin\ffmpeg.exe"
if (!(Test-Path -LiteralPath $Ffmpeg)) {
  $FfmpegCommand = Get-Command ffmpeg -ErrorAction SilentlyContinue
  if (!$FfmpegCommand) {
    throw "ffmpeg not found. Install ffmpeg or put it at C:\ffmpeg\bin\ffmpeg.exe."
  }
  $Ffmpeg = $FfmpegCommand.Source
}

Write-Host "Streaming ${Frequency}Hz sine for ${Duration}s to $Name..."
$TempSine = Join-Path $env:TEMP ("muse-airplay-sine-{0}.wav" -f ([guid]::NewGuid().ToString("N")))
try {
  & $Ffmpeg -hide_banner -loglevel error -y -f lavfi -i "sine=frequency=${Frequency}:duration=${Duration}" $TempSine
  & $AtvRemote --scan-hosts $HostAddress --scan-protocols raop -n $Name "set_volume=$Volume" "stream_file=$TempSine"
  $ExitCode = $LASTEXITCODE
} finally {
  if (Test-Path -LiteralPath $TempSine) {
    Remove-Item -LiteralPath $TempSine -Force
  }
}

exit $ExitCode
