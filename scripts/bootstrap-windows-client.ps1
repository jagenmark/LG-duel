param(
  [switch]$RepairIfNeeded
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repoRoot "build/default"
$clientPath = Join-Path $buildDir "lg_duel_client.exe"
$sdlPath = Join-Path $buildDir "SDL3.dll"
$cachePath = Join-Path $buildDir "CMakeCache.txt"
$ninjaPath = Join-Path $buildDir "build.ninja"

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

Push-Location $repoRoot
try {
  $needsRepair = -not $RepairIfNeeded
  if ($RepairIfNeeded) {
    $needsRepair = -not (
      (Test-Path -LiteralPath $ninjaPath -PathType Leaf) -and
      (Test-Path -LiteralPath $cachePath -PathType Leaf) -and
      (Test-Path -LiteralPath $clientPath -PathType Leaf) -and
      (Test-Path -LiteralPath $sdlPath -PathType Leaf) -and
      ((Get-Content -LiteralPath $cachePath -Raw) -match "CMAKE_GENERATOR:INTERNAL=Ninja") -and
      ((Get-Content -LiteralPath $cachePath -Raw) -match "LG_DUEL_REQUIRE_SDL3:BOOL=ON") -and
      ((Get-Content -LiteralPath $cachePath -Raw) -match "LG_DUEL_FETCH_SDL3:BOOL=ON")
    )
  }
  if ($needsRepair) {
    Write-Host "Repairing this worktree with the pinned SDL3 source..."
    Invoke-CheckedCommand cmake @("--preset", "default", "--fresh")
  } else {
    Write-Host "Using the existing SDL3 build..."
  }

  Write-Host "Building the SDL client incrementally..."
  Invoke-CheckedCommand cmake @(
    "--build", "--preset", "default",
    "--target", "lg_duel_client",
    "--parallel"
  )

  if (-not (Test-Path -LiteralPath $clientPath -PathType Leaf)) {
    throw "The client was not found at $clientPath."
  }
  if (-not (Test-Path -LiteralPath $sdlPath -PathType Leaf)) {
    throw "SDL3.dll was not copied beside the client at $sdlPath."
  }

  Write-Host "Client ready: $clientPath"
  Write-Host "SDL runtime ready: $sdlPath"
}
finally {
  Pop-Location
}
