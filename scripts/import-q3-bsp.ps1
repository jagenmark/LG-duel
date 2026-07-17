[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$SourceBsp,
  [string]$SourceAas = "",
  [string]$CandidateMap = "",
  [string]$ReportDirectory = "",
  [string]$WorkDirectory = "",
  [switch]$AllowOverLimit
)

$ErrorActionPreference = "Stop"

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$sourcePath = [IO.Path]::GetFullPath($SourceBsp)
$stem = [IO.Path]::GetFileNameWithoutExtension($sourcePath)
if ([string]::IsNullOrWhiteSpace($WorkDirectory)) {
  $WorkDirectory = Join-Path (Split-Path -Parent $sourcePath) "work"
}
$workPath = [IO.Path]::GetFullPath($WorkDirectory)
$rawMapPath = Join-Path $workPath "$stem.raw.map"
$logPath = Join-Path $workPath "$stem.q3map2.log"

if ([string]::IsNullOrWhiteSpace($CandidateMap)) {
  $CandidateMap = Join-Path $repositoryRoot "maps\$stem`_import.map"
}
$candidatePath = [IO.Path]::GetFullPath($CandidateMap)
if ([string]::IsNullOrWhiteSpace($ReportDirectory)) {
  $ReportDirectory = Join-Path $repositoryRoot "reports\q3"
}
$reportPath = [IO.Path]::GetFullPath($ReportDirectory)
$jsonReportPath = Join-Path $reportPath "$stem-import.json"
$markdownReportPath = Join-Path $reportPath "$stem-import.md"

$python = (Get-Command python -ErrorAction Stop).Source
$generationPath = Join-Path $workPath (".generation-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $generationPath | Out-Null
$conversionExitCode = 0
try {
  # A complete generation is staged before any prior successful artifact is
  # replaced. The raw map is always regenerated to prevent same-name BSP reuse.
  & (Join-Path $PSScriptRoot "decompile-q3-bsp.ps1") `
    -InputPath $sourcePath `
    -OutputDirectory $generationPath | Out-Host

  $stagedRawMapPath = Join-Path $generationPath "$stem.raw.map"
  $stagedLogPath = Join-Path $generationPath "$stem.q3map2.log"
  $stagedCandidatePath = Join-Path $generationPath ([IO.Path]::GetFileName($candidatePath))
  $stagedJsonReportPath = Join-Path $generationPath ([IO.Path]::GetFileName($jsonReportPath))
  $stagedMarkdownReportPath = Join-Path $generationPath ([IO.Path]::GetFileName($markdownReportPath))
  $arguments = @(
    (Join-Path $PSScriptRoot "import_q3_map.py"),
    "--source-bsp", $sourcePath,
    "--raw-map", $stagedRawMapPath,
    "--output-map", $stagedCandidatePath,
    "--json-report", $stagedJsonReportPath,
    "--markdown-report", $stagedMarkdownReportPath
  )
  if (-not [string]::IsNullOrWhiteSpace($SourceAas)) {
    $arguments += @("--source-aas", [IO.Path]::GetFullPath($SourceAas))
  }
  if ($AllowOverLimit) {
    $arguments += "--allow-over-limit"
  }

  & $python @arguments
  $conversionExitCode = $LASTEXITCODE
  if ($conversionExitCode -ne 0 -and $conversionExitCode -ne 2) {
    throw "Q3 conversion failed with exit code $conversionExitCode; previous artifacts were left unchanged"
  }

  New-Item -ItemType Directory -Force -Path (Split-Path -Parent $candidatePath) | Out-Null
  New-Item -ItemType Directory -Force -Path $reportPath | Out-Null
  New-Item -ItemType Directory -Force -Path $workPath | Out-Null
  # Reports carry both raw and candidate hashes. Publish them last so any
  # interrupted replacement remains detectably inconsistent with the old report.
  Move-Item -LiteralPath $stagedCandidatePath -Destination $candidatePath -Force
  Move-Item -LiteralPath $stagedRawMapPath -Destination $rawMapPath -Force
  Move-Item -LiteralPath $stagedLogPath -Destination $logPath -Force
  Move-Item -LiteralPath $stagedJsonReportPath -Destination $jsonReportPath -Force
  Move-Item -LiteralPath $stagedMarkdownReportPath -Destination $markdownReportPath -Force
} finally {
  if (Test-Path -LiteralPath $generationPath) {
    Remove-Item -LiteralPath $generationPath -Recurse -Force
  }
}

if ($conversionExitCode -eq 2) {
  throw "Q3 conversion exceeded LG capacities; complete marked artifacts were published at $markdownReportPath"
}

[PSCustomObject]@{
  SourceBsp = $sourcePath
  RawDecompile = $rawMapPath
  CandidateMap = $candidatePath
  JsonReport = $jsonReportPath
  MarkdownReport = $markdownReportPath
}
