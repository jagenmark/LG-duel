param(
  [string]$ServerHost = "20.238.17.134",
  [ValidateRange(1, 65535)]
  [int]$ServerPort = 27960,
  [string]$BuildDir = "build/windows",
  [string]$OutputDir = "dist/LG-Duel-Windows-x64"
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($ServerHost)) {
  throw "ServerHost must be a non-empty IP address or hostname."
}

$repoRoot = Split-Path -Parent $PSScriptRoot

function Get-AbsoluteRepoPath {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Path
  )

  if ([System.IO.Path]::IsPathRooted($Path)) {
    return [System.IO.Path]::GetFullPath($Path)
  }
  return [System.IO.Path]::GetFullPath((Join-Path $repoRoot $Path))
}

function Invoke-CheckedCommand {
  param(
    [Parameter(Mandatory = $true)]
    [string]$FilePath,
    [Parameter(Mandatory = $true)]
    [string[]]$ArgumentList
  )

  & $FilePath @ArgumentList
  if ($LASTEXITCODE -ne 0) {
    throw "$FilePath failed with exit code $LASTEXITCODE."
  }
}

$buildPath = Get-AbsoluteRepoPath $BuildDir
$outputPath = Get-AbsoluteRepoPath $OutputDir
$zipPath = "$outputPath.zip"

Push-Location $repoRoot
try {
  Write-Host "Configuring the Windows Release build..."
  Invoke-CheckedCommand cmake @(
    "-S", ".",
    "-B", $buildPath,
    "-A", "x64",
    "-DCMAKE_BUILD_TYPE=Release",
    "-DBUILD_TESTING=OFF",
    "-DLG_DUEL_REQUIRE_SDL3=ON",
    "-DLG_DUEL_FETCH_SDL3=ON",
    "-DLG_DUEL_SDL3_GIT_TAG=8e37db5e797b6167f3a00d697d816a684bd259c7",
    "-DCMAKE_DISABLE_FIND_PACKAGE_SDL3=TRUE",
    "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded"
  )

  Write-Host "Building the client and server..."
  Invoke-CheckedCommand cmake @(
    "--build", $buildPath,
    "--config", "Release",
    "--target", "lg_duel_client", "lg_duel_server",
    "--parallel"
  )

  Write-Host "Creating and validating the playtest package..."
  & (Join-Path $PSScriptRoot "package-windows.ps1") `
    -BuildDir $buildPath `
    -OutputDir $outputPath `
    -Configuration Release `
    -ServerHost $ServerHost `
    -ServerPort $ServerPort

  if (Test-Path $zipPath -PathType Leaf) {
    Remove-Item -LiteralPath $zipPath -Force
  }
  Compress-Archive -LiteralPath $outputPath -DestinationPath $zipPath

  Write-Host "Windows playtest ZIP created at $zipPath"
}
finally {
  Pop-Location
}
