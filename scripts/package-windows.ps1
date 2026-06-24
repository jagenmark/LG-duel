param(
  [Parameter(Mandatory = $true)]
  [string]$BuildDir,

  [Parameter(Mandatory = $true)]
  [string]$OutputDir,

  [string]$Configuration = "Release",
  [Parameter(Mandatory = $true)]
  [string]$ServerHost,
  [ValidateRange(1, 65535)]
  [int]$ServerPort = 27960
)

$ErrorActionPreference = "Stop"
if ([string]::IsNullOrWhiteSpace($ServerHost)) {
  throw "ServerHost must be a non-empty IP address or hostname."
}

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

$serverCandidates = @(
  (Join-Path $resolvedBuildDir "$Configuration/lg_duel_server.exe"),
  (Join-Path $resolvedBuildDir "lg_duel_server.exe")
)
$server = $serverCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $server) {
  throw "lg_duel_server.exe was not found in $resolvedBuildDir for $Configuration."
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
  $parentBuildDir = Join-Path (Split-Path -Parent $repoRoot) "build"
  if (Test-Path $parentBuildDir) {
    $sdlLicense = Get-ChildItem -Path $parentBuildDir -Filter "LICENSE.txt" -File -Recurse |
      Where-Object { $_.FullName -match "[\\/]sdl3-src[\\/]" } |
      Select-Object -First 1
  }
}
if (-not $sdlLicense) {
  throw "The SDL3 license file was not found in the fetched source tree."
}

if (Test-Path $outputPath) {
  Remove-Item -Path $outputPath -Recurse -Force
}
New-Item -Path $outputPath -ItemType Directory | Out-Null

Copy-Item $client (Join-Path $outputPath "lg_duel_client.exe")
Copy-Item $server (Join-Path $outputPath "lg_duel_server.exe")
$shaderSource = Join-Path (Split-Path -Parent $client) "shaders"
if (-not (Test-Path $shaderSource)) {
  throw "The compiled shader directory was not found beside lg_duel_client.exe."
}
Copy-Item $shaderSource (Join-Path $outputPath "shaders") -Recurse
Copy-Item $sdl.FullName (Join-Path $outputPath "SDL3.dll")
Copy-Item $sdlLicense.FullName (Join-Path $outputPath "SDL3-LICENSE.txt")
Copy-Item (Join-Path $repoRoot "package/windows/Play LG Duel.bat") $outputPath
Copy-Item (Join-Path $repoRoot "package/windows/Host LG Duel Server.bat") $outputPath
Copy-Item (Join-Path $repoRoot "package/windows/README.txt") $outputPath
Copy-Item (Join-Path $repoRoot "docs/PLAYTEST_GUIDE.html") $outputPath
Set-Content -Path (Join-Path $outputPath "server-address.txt") -Value "${ServerHost}:${ServerPort}" -Encoding ASCII

$requiredFiles = @(
  "lg_duel_client.exe",
  "lg_duel_server.exe",
  "shaders/color2d.vert.spv",
  "shaders/color2d.frag.spv",
  "shaders/world3d.vert.spv",
  "shaders/world3d.frag.spv",
  "SDL3.dll",
  "SDL3-LICENSE.txt",
  "Play LG Duel.bat",
  "Host LG Duel Server.bat",
  "README.txt",
  "server-address.txt"
)
foreach ($file in $requiredFiles) {
  if (-not (Test-Path (Join-Path $outputPath $file))) {
    throw "Package validation failed: $file is missing."
  }
}

Write-Host "Windows playtest package created at $outputPath"
Get-ChildItem $outputPath | Select-Object Name, Length
