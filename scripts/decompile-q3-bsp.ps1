[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$InputPath,
  [string]$OutputDirectory = "",
  [switch]$Force
)

$ErrorActionPreference = "Stop"

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$sourcePath = [IO.Path]::GetFullPath($InputPath)
if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
  throw "BSP source does not exist: $sourcePath"
}
if ([IO.Path]::GetExtension($sourcePath) -ne ".bsp") {
  throw "expected a .bsp source: $sourcePath"
}

$header = [byte[]]::new(8)
$stream = [IO.File]::OpenRead($sourcePath)
try {
  if ($stream.Read($header, 0, $header.Length) -ne $header.Length) {
    throw "BSP source is shorter than its eight-byte header"
  }
} finally {
  $stream.Dispose()
}

$magic = [Text.Encoding]::ASCII.GetString($header, 0, 4)
$version = [BitConverter]::ToInt32($header, 4)
if ($magic -ne "IBSP") {
  throw "unsupported BSP magic '$magic'; expected IBSP"
}
$game = switch ($version) {
  46 { "quake3" }
  47 { "quakelive" }
  default { throw "unsupported IBSP version $version; expected Quake 3 v46 or Quake Live v47" }
}

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
  $OutputDirectory = Join-Path (Split-Path -Parent $sourcePath) "work"
}
$outputPath = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $outputPath | Out-Null
$stem = [IO.Path]::GetFileNameWithoutExtension($sourcePath)
$rawMapPath = Join-Path $outputPath "$stem.raw.map"
$logPath = Join-Path $outputPath "$stem.q3map2.log"
if ((Test-Path -LiteralPath $rawMapPath) -and -not $Force) {
  throw "raw decompile already exists; use -Force to replace it: $rawMapPath"
}

$q3map2Path = (& (Join-Path $PSScriptRoot "setup-q3map2.ps1") | Select-Object -Last 1)
$stagingRoot = Join-Path ([IO.Path]::GetTempPath()) ("lg-duel-q3map2-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $stagingRoot | Out-Null
try {
  $stagedBspPath = Join-Path $stagingRoot "$stem.bsp"
  Copy-Item -LiteralPath $sourcePath -Destination $stagedBspPath
  $toolOutput = (& $q3map2Path -game $game -convert -format map $stagedBspPath 2>&1 | Out-String)
  $toolOutput | Set-Content -LiteralPath $logPath -Encoding utf8
  if ($LASTEXITCODE -ne 0) {
    throw "q3map2 failed with exit code $LASTEXITCODE; see $logPath"
  }
  $convertedPath = Join-Path $stagingRoot "$stem`_converted.map"
  if (-not (Test-Path -LiteralPath $convertedPath -PathType Leaf)) {
    throw "q3map2 succeeded but did not create $convertedPath"
  }
  Copy-Item -LiteralPath $convertedPath -Destination $rawMapPath -Force:$Force
} finally {
  if ((Test-Path -LiteralPath $stagingRoot) -and
      $stagingRoot.StartsWith([IO.Path]::GetTempPath(), [StringComparison]::OrdinalIgnoreCase)) {
    Remove-Item -LiteralPath $stagingRoot -Recurse -Force
  }
}

[PSCustomObject]@{
  Source = $sourcePath
  SourceSha256 = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash.ToLowerInvariant()
  BspVersion = $version
  Q3Map2Game = $game
  RawMap = $rawMapPath
  RawMapSha256 = (Get-FileHash -LiteralPath $rawMapPath -Algorithm SHA256).Hash.ToLowerInvariant()
  Log = $logPath
}
