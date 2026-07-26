$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repoRoot "build/default"
$clientPath = Join-Path $buildDir "lg_duel_client.exe"
$sdlPath = Join-Path $buildDir "SDL3.dll"

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
  Write-Host "Configuring this worktree with the pinned SDL3 source..."
  Invoke-CheckedCommand cmake @("--preset", "default", "--fresh")

  Write-Host "Building the SDL client..."
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
