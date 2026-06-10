param(
  [Parameter(Mandatory = $true)]
  [string]$BuildDir,

  [Parameter(Mandatory = $true)]
  [string]$OutputDir,

  [string]$Configuration = "Release",
  [string]$ServerHost = "213.66.106.51",
  [ValidateRange(1, 65535)]
  [int]$ServerPort = 27960
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$resolvedBuildDir = (Resolve-Path $BuildDir).Path
$outputPath = [System.IO.Path]::GetFullPath($OutputDir)

$clientCandidates = @(
  (Join-Path $resolvedBuildDir "$Configuration/lg_duel_client.exe"),
  (Join-Path $resolvedBuildDir "lg_duel_client.exe")
)
$client = $clientCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $client) {
  throw "lg_duel_client.exe was not found in $resolvedBuildDir for $Configuration."
}

$sdlCandidates = Get-ChildItem -Path $resolvedBuildDir -Filter "SDL3.dll" -File -Recurse |
  Where-Object {
    $_.FullName -match "[\\/](Release|bin)[\\/]" -or
    $_.DirectoryName -eq (Split-Path -Parent $client)
  }
$sdl = $sdlCandidates | Select-Object -First 1
if (-not $sdl) {
  throw "SDL3.dll was not found. The package would not run on a clean Windows PC."
}

$sdlLicense = Get-ChildItem -Path $resolvedBuildDir -Filter "LICENSE.txt" -File -Recurse |
  Where-Object { $_.FullName -match "[\\/]sdl3-src[\\/]" } |
  Select-Object -First 1
if (-not $sdlLicense) {
  throw "The SDL3 license file was not found in the fetched source tree."
}

if (Test-Path $outputPath) {
  Remove-Item -Path $outputPath -Recurse -Force
}
New-Item -Path $outputPath -ItemType Directory | Out-Null

Copy-Item $client (Join-Path $outputPath "lg_duel_client.exe")
Copy-Item $sdl.FullName (Join-Path $outputPath "SDL3.dll")
Copy-Item $sdlLicense.FullName (Join-Path $outputPath "SDL3-LICENSE.txt")
Copy-Item (Join-Path $repoRoot "package/windows/Play LG Duel.bat") $outputPath
Copy-Item (Join-Path $repoRoot "package/windows/README.txt") $outputPath
Copy-Item (Join-Path $repoRoot "PLAYTEST_GUIDE.html") $outputPath
Set-Content -Path (Join-Path $outputPath "server-address.txt") -Value "${ServerHost}:${ServerPort}" -Encoding ASCII

$requiredFiles = @(
  "lg_duel_client.exe",
  "SDL3.dll",
  "SDL3-LICENSE.txt",
  "Play LG Duel.bat",
  "README.txt",
  "PLAYTEST_GUIDE.html",
  "server-address.txt"
)
foreach ($file in $requiredFiles) {
  if (-not (Test-Path (Join-Path $outputPath $file))) {
    throw "Package validation failed: $file is missing."
  }
}

Write-Host "Windows playtest package created at $outputPath"
Get-ChildItem $outputPath | Select-Object Name, Length
