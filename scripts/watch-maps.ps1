param(
  [string]$Source = "",
  [string]$Destination = "",
  [int]$IntervalMilliseconds = 500
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($Source)) {
  $Source = Join-Path $repoRoot "maps"
}
if ([string]::IsNullOrWhiteSpace($Destination)) {
  $Destination = Join-Path $repoRoot "build/default/maps"
}

$sourcePath = [System.IO.Path]::GetFullPath($Source)
$destinationPath = [System.IO.Path]::GetFullPath($Destination)
$extensions = @(".map", ".lgmap")
$knownFiles = @{}

function Copy-MapFile {
  param([System.IO.FileInfo]$File)

  if (-not $extensions.Contains($File.Extension.ToLowerInvariant())) {
    return
  }

  New-Item -ItemType Directory -Force -Path $destinationPath | Out-Null
  $target = Join-Path $destinationPath $File.Name

  for ($attempt = 1; $attempt -le 10; ++$attempt) {
    try {
      Copy-Item -LiteralPath $File.FullName -Destination $target -Force
      Write-Host ("[{0}] synced {1}" -f (Get-Date -Format "HH:mm:ss"), $File.Name)
      return
    } catch {
      if ($attempt -eq 10) {
        Write-Warning ("Could not sync {0}: {1}" -f $File.Name, $_.Exception.Message)
        return
      }
      Start-Sleep -Milliseconds 100
    }
  }
}

function Sync-ChangedMaps {
  Get-ChildItem -LiteralPath $sourcePath -File |
    Where-Object { $extensions.Contains($_.Extension.ToLowerInvariant()) } |
    ForEach-Object {
      $signature = "{0}|{1}" -f $_.LastWriteTimeUtc.Ticks, $_.Length
      if (-not $knownFiles.ContainsKey($_.FullName) -or $knownFiles[$_.FullName] -ne $signature) {
        $knownFiles[$_.FullName] = $signature
        Copy-MapFile $_
      }
    }
}

if (-not (Test-Path -LiteralPath $sourcePath -PathType Container)) {
  throw "Map source directory does not exist: $sourcePath"
}

Write-Host "Watching maps for LG Duel"
Write-Host "  source:      $sourcePath"
Write-Host "  destination: $destinationPath"
Write-Host "Press Ctrl+C to stop."

while ($true) {
  Sync-ChangedMaps
  Start-Sleep -Milliseconds $IntervalMilliseconds
}
