[CmdletBinding()]
param(
  [string]$Destination = "",
  [switch]$Force
)

$ErrorActionPreference = "Stop"

$releaseTag = "20260114"
$archiveName = "netradiant-custom-$releaseTag-windows-x86_64.zip"
$archiveUrl = "https://github.com/Garux/netradiant-custom/releases/download/$releaseTag/$archiveName"
$archiveSize = 43618125L
$archiveSha256 = "25c2e14e2b0bd7a9897b2f943c8821458873c8713973f9c3d68d49f26fe79e35"
$q3map2Version = "2.5.17n-git-68ecbed"
$q3map2Sha256 = "c38475d0e691dcf6e2ca2c69145e6c3dfff953192bb99145e0b578271d77c88b"
$distributionSha256 = "1f294972a77b32bdbf978b4e368c0f343c066ccc79236092591549d19b40c77d"

function Get-DistributionFingerprint([string]$Root, [string]$ExcludedManifest = "") {
  $files = @(
    Get-ChildItem -LiteralPath $Root -Recurse -File |
      Where-Object { [string]::IsNullOrEmpty($ExcludedManifest) -or $_.FullName -ne $ExcludedManifest } |
      ForEach-Object {
        [PSCustomObject]@{
          relative_path = $_.FullName.Substring($Root.Length + 1).Replace('\', '/')
          sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        }
      } |
      Sort-Object relative_path
  )
  $canonical = [string]::Join("", @(
    $files | ForEach-Object { "$($_.relative_path)`t$($_.sha256)`n" }
  ))
  $algorithm = [Security.Cryptography.SHA256]::Create()
  try {
    $digest = $algorithm.ComputeHash([Text.Encoding]::UTF8.GetBytes($canonical))
  } finally {
    $algorithm.Dispose()
  }
  return [PSCustomObject]@{
    files = $files
    sha256 = -join ($digest | ForEach-Object { $_.ToString("x2") })
  }
}

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$managedToolRoot = [IO.Path]::GetFullPath((Join-Path $repositoryRoot "build\tools\q3map2"))
if ([string]::IsNullOrWhiteSpace($Destination)) {
  $Destination = Join-Path $managedToolRoot $releaseTag
}
$destinationPath = [IO.Path]::GetFullPath($Destination)
$q3map2Path = Join-Path $destinationPath "q3map2.exe"
$installManifestPath = Join-Path $destinationPath "lg-duel-q3map2-install.json"

if (-not $destinationPath.StartsWith(
    $managedToolRoot + [IO.Path]::DirectorySeparatorChar,
    [StringComparison]::OrdinalIgnoreCase
  )) {
  throw "q3map2 installations are restricted to the managed tool cache under $managedToolRoot"
}

if ((Test-Path -LiteralPath $q3map2Path) -and -not $Force) {
  if (-not (Test-Path -LiteralPath $installManifestPath -PathType Leaf)) {
    throw "existing q3map2 installation has no verification manifest; rerun with -Force"
  }
  $installManifest = Get-Content -Raw -LiteralPath $installManifestPath | ConvertFrom-Json
  $existingSha256 = (Get-FileHash -LiteralPath $q3map2Path -Algorithm SHA256).Hash.ToLowerInvariant()
  $existingBanner = (& $q3map2Path -help 2>&1 | Out-String)
  $existingDistribution = Get-DistributionFingerprint $destinationPath $installManifestPath
  $distributionVerified =
    $existingDistribution.sha256 -eq $distributionSha256 -and
    $installManifest.distribution_sha256 -eq $distributionSha256
  if ($distributionVerified -and
      $installManifest.release -eq $releaseTag -and
      $installManifest.archive_sha256 -eq $archiveSha256 -and
      $installManifest.q3map2_sha256 -eq $q3map2Sha256 -and
      $existingSha256 -eq $q3map2Sha256 -and
      $existingBanner -match [regex]::Escape($q3map2Version)) {
    Write-Output $q3map2Path
    exit 0
  }
  throw "existing q3map2 installation does not match the pinned verified manifest; rerun with -Force"
}
if ((Test-Path -LiteralPath $destinationPath) -and -not $Force) {
  throw "destination already exists but is not a verified installation; rerun with -Force: $destinationPath"
}

$downloadRoot = Join-Path $repositoryRoot "build\downloads"
New-Item -ItemType Directory -Force -Path $downloadRoot | Out-Null
$archivePath = Join-Path $downloadRoot $archiveName

if (-not (Test-Path -LiteralPath $archivePath)) {
  $partialPath = "$archivePath.partial"
  Remove-Item -LiteralPath $partialPath -Force -ErrorAction SilentlyContinue
  Invoke-WebRequest -Uri $archiveUrl -OutFile $partialPath
  Move-Item -LiteralPath $partialPath -Destination $archivePath -Force
}

$actualSha256 = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
if ((Get-Item -LiteralPath $archivePath).Length -ne $archiveSize) {
  throw "q3map2 archive size mismatch: expected $archiveSize bytes"
}
if ($actualSha256 -ne $archiveSha256) {
  throw "q3map2 archive checksum mismatch: expected $archiveSha256, got $actualSha256"
}

$stagingPath = Join-Path $managedToolRoot (".staging-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $stagingPath | Out-Null
try {
  Expand-Archive -LiteralPath $archivePath -DestinationPath $stagingPath -Force
  $stagedExecutable = Join-Path $stagingPath "q3map2.exe"
  if (-not (Test-Path -LiteralPath $stagedExecutable)) {
    throw "q3map2.exe was not present at the root of $archiveName"
  }

  $banner = (& $stagedExecutable -help 2>&1 | Out-String)
  if ($banner -notmatch [regex]::Escape($q3map2Version)) {
    throw "q3map2 banner did not contain pinned version '$q3map2Version'"
  }
  $stagedSha256 = (Get-FileHash -LiteralPath $stagedExecutable -Algorithm SHA256).Hash.ToLowerInvariant()
  if ($stagedSha256 -ne $q3map2Sha256) {
    throw "q3map2 executable checksum mismatch: expected $q3map2Sha256, got $stagedSha256"
  }

  $stagedDistribution = Get-DistributionFingerprint $stagingPath
  if ($stagedDistribution.sha256 -ne $distributionSha256) {
    throw "q3map2 extracted distribution mismatch: expected $distributionSha256, got $($stagedDistribution.sha256)"
  }
  @{
    release = $releaseTag
    version = $q3map2Version
    archive_sha256 = $archiveSha256
    q3map2_sha256 = $q3map2Sha256
    distribution_sha256 = $distributionSha256
    files = $stagedDistribution.files
  } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $stagingPath "lg-duel-q3map2-install.json") -Encoding utf8

  if ($Force -and (Test-Path -LiteralPath $destinationPath)) {
    Remove-Item -LiteralPath $destinationPath -Recurse -Force
  }
  Move-Item -LiteralPath $stagingPath -Destination $destinationPath
} finally {
  Remove-Item -LiteralPath $stagingPath -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Output $q3map2Path
