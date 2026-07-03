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

function Normalize-TextureMaterial {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Material
  )

  $normalized = $Material.Replace("\", "/").Trim()
  while ($normalized.StartsWith("/")) {
    $normalized = $normalized.Substring(1)
  }
  if ($normalized.StartsWith("textures/", [System.StringComparison]::OrdinalIgnoreCase)) {
    $normalized = $normalized.Substring("textures/".Length)
  }
  return $normalized
}

function Get-MapTextureMaterials {
  param(
    [Parameter(Mandatory = $true)]
    [string]$MapSource
  )

  $materials = New-Object "System.Collections.Generic.HashSet[string]" ([System.StringComparer]::OrdinalIgnoreCase)
  $facePattern = '^\s*\([^)]*\)\s*\([^)]*\)\s*\([^)]*\)\s+([^\s\}]+)'
  Get-ChildItem -Path $MapSource -File |
    Where-Object { $_.Extension -in ".map" } |
    ForEach-Object {
      Select-String -Path $_.FullName -Pattern $facePattern | ForEach-Object {
        $material = Normalize-TextureMaterial $_.Matches[0].Groups[1].Value
        if (-not [string]::IsNullOrWhiteSpace($material)) {
          [void]$materials.Add($material)
        }
      }
    }

  return $materials | Sort-Object
}

function Resolve-TextureMaterialPath {
  param(
    [Parameter(Mandatory = $true)]
    [string]$TextureSource,

    [Parameter(Mandatory = $true)]
    [string]$Material
  )

  $normalized = Normalize-TextureMaterial $Material
  if ([string]::IsNullOrWhiteSpace($normalized)) {
    return $null
  }

  $relative = $normalized
  if ([System.IO.Path]::GetExtension($relative) -eq "") {
    $relative = "$relative.png"
  }
  $candidate = $TextureSource
  foreach ($part in ($relative -split "/")) {
    if ([string]::IsNullOrWhiteSpace($part)) {
      continue
    }
    $candidate = Join-Path $candidate $part
  }

  $textureRoot = [System.IO.Path]::GetFullPath($TextureSource)
  $candidatePath = [System.IO.Path]::GetFullPath($candidate)
  $rootWithSeparator = $textureRoot.TrimEnd([System.IO.Path]::DirectorySeparatorChar, [System.IO.Path]::AltDirectorySeparatorChar) + [System.IO.Path]::DirectorySeparatorChar
  if (-not $candidatePath.StartsWith($rootWithSeparator, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Texture material '$Material' resolves outside the texture directory."
  }
  if (-not (Test-Path $candidatePath -PathType Leaf)) {
    return $null
  }

  return Get-Item $candidatePath
}

function Get-TextureRelativePath {
  param(
    [Parameter(Mandatory = $true)]
    [string]$TextureSource,

    [Parameter(Mandatory = $true)]
    [string]$TextureFile
  )

  $textureRoot = [System.IO.Path]::GetFullPath($TextureSource).TrimEnd([System.IO.Path]::DirectorySeparatorChar, [System.IO.Path]::AltDirectorySeparatorChar)
  $texturePath = [System.IO.Path]::GetFullPath($TextureFile)
  $rootWithSeparator = $textureRoot + [System.IO.Path]::DirectorySeparatorChar
  if (-not $texturePath.StartsWith($rootWithSeparator, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Texture file '$TextureFile' is outside the texture directory."
  }

  return $texturePath.Substring($rootWithSeparator.Length)
}

function Copy-UsedMapTextures {
  param(
    [Parameter(Mandatory = $true)]
    [string]$MapSource,

    [Parameter(Mandatory = $true)]
    [string]$TextureSource,

    [Parameter(Mandatory = $true)]
    [string]$OutputPath
  )

  $textureOutput = Join-Path $OutputPath "textures"
  New-Item -Path $textureOutput -ItemType Directory | Out-Null
  
  $missingMaterials = New-Object System.Collections.Generic.List[string]
  $copiedTextures = New-Object "System.Collections.Generic.HashSet[string]" ([System.StringComparer]::OrdinalIgnoreCase)
  $materials = Get-MapTextureMaterials $MapSource
  foreach ($material in $materials) {
    $textureFile = Resolve-TextureMaterialPath $TextureSource $material
    if ($null -eq $textureFile) {
      $missingMaterials.Add($material)
      continue
    }

    $relativePath = Get-TextureRelativePath $TextureSource $textureFile.FullName
    if (-not $copiedTextures.Add($relativePath)) {
      continue
    }

    $destination = Join-Path $textureOutput $relativePath
    $destinationDirectory = Split-Path -Parent $destination
    if (-not (Test-Path $destinationDirectory)) {
      New-Item -Path $destinationDirectory -ItemType Directory | Out-Null
    }
    Copy-Item $textureFile.FullName $destination
  }

  if ($missingMaterials.Count -gt 0) {
    throw "Package validation failed: missing texture files for map materials: $($missingMaterials -join ', ')"
  }

  return $copiedTextures.Count
}

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
$modelSource = Join-Path (Split-Path -Parent $client) "assets/models"
if (-not (Test-Path $modelSource)) {
  throw "The runtime model directory was not found beside lg_duel_client.exe."
}
Copy-Item $modelSource (Join-Path $outputPath "assets/models") -Recurse
$fontSource = Join-Path (Split-Path -Parent $client) "assets/fonts"
if (Test-Path $fontSource) {
  Copy-Item $fontSource (Join-Path $outputPath "assets/fonts") -Recurse
}
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
$copiedTextureCount = Copy-UsedMapTextures $mapSource $textureSource $outputPath
Copy-Item (Join-Path $repoRoot "package/windows/Play LG Duel.bat") $outputPath
Copy-Item (Join-Path $repoRoot "package/windows/Host LG Duel Server.bat") $outputPath
Copy-Item (Join-Path $repoRoot "package/windows/README.txt") $outputPath
Set-Content -Path (Join-Path $outputPath "server-address.txt") -Value "${ServerHost}:${ServerPort}" -Encoding ASCII

$requiredFiles = @(
  "lg_duel_client.exe",
  "lg_duel_server.exe",
  "shaders/color2d.vert.spv",
  "shaders/color2d.frag.spv",
  "shaders/world3d.vert.spv",
  "shaders/world3d.frag.spv",
  "shaders/outline_mask.frag.spv",
  "shaders/outline_clear.vert.spv",
  "shaders/outline_clear.frag.spv",
  "shaders/outline_dilate.frag.spv",
  "shaders/outline_composite.vert.spv",
  "shaders/outline_composite.frag.spv",
  "SDL3.dll",
  "SDL3-LICENSE.txt",
  "config/balance.cfg",
  "config/server_cvars.cfg",
  "config/default_client.cfg",
  "config/sound_mixer.cfg",
  "config/README.md",
  "assets/fonts/bahnschrift.ttf",
  "assets/models/lg_duelist_male_v2/art/exports/lg_duelist_male.glb",
  "maps/eyetoeye.map",
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

$requiredFontFiles = Get-ChildItem -Path (Join-Path $outputPath "assets/fonts") -File |
  Where-Object { $_.Extension -in ".ttf", ".otf", ".TTF", ".OTF" }
if ($requiredFontFiles.Count -eq 0) {
  throw "Package validation failed: no runtime UI font files were found."
}

$requiredMapFiles = Get-ChildItem -Path $mapSource -File |
  Where-Object { $_.Extension -in ".map" }
if ($requiredMapFiles.Count -eq 0) {
  throw "Package validation failed: no runtime map files were found."
}
foreach ($file in $requiredMapFiles) {
  $packagedMapFile = Join-Path $outputPath "maps/$($file.Name)"
  if (-not (Test-Path $packagedMapFile)) {
    throw "Package validation failed: maps/$($file.Name) is missing."
  }
}

$requiredTextureMaterials = Get-MapTextureMaterials $mapSource
if ($requiredTextureMaterials.Count -gt 0 -and $copiedTextureCount -eq 0) {
  throw "Package validation failed: map materials were found but no textures were copied."
}
foreach ($material in $requiredTextureMaterials) {
  $textureFile = Resolve-TextureMaterialPath $textureSource $material
  if ($null -eq $textureFile) {
    throw "Package validation failed: missing texture file for map material $material."
  }
  $packagedTextureFile = Join-Path (Join-Path $outputPath "textures") (Get-TextureRelativePath $textureSource $textureFile.FullName)
  if (-not (Test-Path $packagedTextureFile)) {
    throw "Package validation failed: textures/$(Get-TextureRelativePath $textureSource $textureFile.FullName) is missing."
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

$requiredModelFiles = Get-ChildItem -Path $modelSource -Recurse -File |
  Where-Object { $_.Extension -in ".glb", ".gltf", ".bin" }
if ($requiredModelFiles.Count -eq 0) {
  throw "Package validation failed: no runtime glTF/GLB model files were found."
}
foreach ($file in $requiredModelFiles) {
  $modelRoot = $modelSource.TrimEnd("\", "/")
  $relativeModelPath = $file.FullName.Substring($modelRoot.Length).TrimStart("\", "/").Replace("\", "/")
  $packagedModelFile =
    Join-Path (Join-Path $outputPath "assets/models") $relativeModelPath
  if (-not (Test-Path $packagedModelFile)) {
    throw "Package validation failed: assets/models/$relativeModelPath is missing."
  }
}

Write-Host "Windows playtest package created at $outputPath"
Write-Host "Copied $copiedTextureCount map texture(s)."
Get-ChildItem $outputPath | Select-Object Name, Length
