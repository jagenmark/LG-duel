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
  $fallbackBuildDirs = @(
    (Join-Path $repoRoot "build"),
    (Join-Path (Split-Path -Parent $repoRoot) "build")
  )
  foreach ($buildDir in $fallbackBuildDirs) {
    if (-not (Test-Path $buildDir)) {
      continue
    }
    $sdlLicense = Get-ChildItem -Path $buildDir -Filter "LICENSE.txt" -File -Recurse |
      Where-Object { $_.FullName -match "[\\/]sdl3-src[\\/]" } |
      Select-Object -First 1
    if ($sdlLicense) {
      break
    }
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
$audioSource = Join-Path (Split-Path -Parent $client) "assets/audio"
if (-not (Test-Path $audioSource)) {
  throw "The runtime audio directory was not found beside lg_duel_client.exe."
}
New-Item -Path (Join-Path $outputPath "assets") -ItemType Directory | Out-Null
Copy-Item $audioSource (Join-Path $outputPath "assets/audio") -Recurse
Copy-Item $sdl.FullName (Join-Path $outputPath "SDL3.dll")
Copy-Item $sdlLicense.FullName (Join-Path $outputPath "SDL3-LICENSE.txt")
Copy-Item (Join-Path $repoRoot "config") (Join-Path $outputPath "config") -Recurse
$mapSource = Join-Path $repoRoot "maps"
if (-not (Test-Path $mapSource)) {
  throw "The runtime map directory was not found in the repository."
}
Copy-Item $mapSource (Join-Path $outputPath "maps") -Recurse
$textureSource = Join-Path $repoRoot "textures"
if (-not (Test-Path $textureSource)) {
  throw "The runtime texture directory was not found in the repository."
}
Copy-Item $textureSource (Join-Path $outputPath "textures") -Recurse
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
  "config/gameplay.cfg",
  "maps/thunderstruck.lgmap",
  "textures/License.txt",
  "textures/512x512/Stone/Stone_01-512x512.png",
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

$requiredMapFiles = Get-ChildItem -Path $mapSource -File |
  Where-Object { $_.Extension -in ".lgmap", ".map" }
if ($requiredMapFiles.Count -eq 0) {
  throw "Package validation failed: no runtime map files were found."
}
foreach ($file in $requiredMapFiles) {
  $packagedMapFile = Join-Path $outputPath "maps/$($file.Name)"
  if (-not (Test-Path $packagedMapFile)) {
    throw "Package validation failed: maps/$($file.Name) is missing."
  }
}

$requiredAudioFiles = Get-ChildItem -Path $audioSource -Filter "*.wav" -File
if ($requiredAudioFiles.Count -eq 0) {
  throw "Package validation failed: no runtime audio WAV files were found."
}
foreach ($file in $requiredAudioFiles) {
  $packagedAudioFile = Join-Path $outputPath "assets/audio/$($file.Name)"
  if (-not (Test-Path $packagedAudioFile)) {
    throw "Package validation failed: assets/audio/$($file.Name) is missing."
  }
}

Write-Host "Windows playtest package created at $outputPath"
Get-ChildItem $outputPath | Select-Object Name, Length
