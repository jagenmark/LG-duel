param(
  [Parameter(Position = 0)]
  [ValidateSet('start', 'stop', 'status')]
  [string]$Action = 'status',
  [int]$ServerPort = 27960,
  [int]$ControlPort = 27961,
  [ValidateSet('gpu', 'fallback')]
  [string]$Renderer = 'gpu',
  [switch]$Benchmark
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repoRoot 'build/default'
$stateDir = Join-Path $repoRoot 'build/dev-control'
$statePath = Join-Path $stateDir 'processes.json'
$serverExe = Join-Path $buildDir 'lg_duel_server.exe'
$clientExe = Join-Path $buildDir 'lg_duel_client.exe'
$controlScript = Join-Path $PSScriptRoot 'lg_control.py'

function Get-ExactProcess([int]$ProcessId, [string]$ExpectedPath) {
  if ($ProcessId -le 0) { return $null }
  $process = Get-Process -Id $ProcessId -ErrorAction SilentlyContinue
  if ($null -eq $process) { return $null }
  try { $actualPath = [IO.Path]::GetFullPath($process.Path) } catch { return $null }
  if ($actualPath -ne [IO.Path]::GetFullPath($ExpectedPath)) { return $null }
  return $process
}

function Find-ExactProcess([string]$ExpectedPath) {
  $leaf = [IO.Path]::GetFileNameWithoutExtension($ExpectedPath)
  foreach ($process in (Get-Process -Name $leaf -ErrorAction SilentlyContinue)) {
    try {
      if ([IO.Path]::GetFullPath($process.Path) -eq [IO.Path]::GetFullPath($ExpectedPath)) {
        return $process
      }
    } catch { }
  }
  return $null
}

function Read-State {
  if (-not (Test-Path -LiteralPath $statePath)) { return $null }
  try { return Get-Content -Raw -LiteralPath $statePath | ConvertFrom-Json } catch { return $null }
}

if ($Action -eq 'status') {
  $state = Read-State
  if ($null -eq $state) {
    Write-Output 'No LG Duel development processes are recorded.'
  } else {
    $server = Get-ExactProcess ([int]$state.server.pid) $serverExe
    $client = Get-ExactProcess ([int]$state.client.pid) $clientExe
    Write-Output ("Server: {0} (pid {1}, owned {2})" -f $(if ($server) {'running'} else {'stopped'}), $state.server.pid, $state.server.owned)
    Write-Output ("Client: {0} (pid {1}, owned {2})" -f $(if ($client) {'running'} else {'stopped'}), $state.client.pid, $state.client.owned)
  }
  python $controlScript --port $ControlPort --timeout 2 status
  exit $LASTEXITCODE
}

if ($Action -eq 'stop') {
  $state = Read-State
  if ($null -eq $state) {
    Write-Output 'Nothing to stop; no process ownership file exists.'
    exit 0
  }
  foreach ($entry in @(
    @{ Name = 'client'; Data = $state.client; Path = $clientExe },
    @{ Name = 'server'; Data = $state.server; Path = $serverExe }
  )) {
    $process = Get-ExactProcess ([int]$entry.Data.pid) $entry.Path
    if ($null -eq $process) {
      Write-Output ("{0} is already stopped or its PID belongs to another executable." -f $entry.Name)
    } elseif (-not [bool]$entry.Data.owned) {
      Write-Output ("Leaving attached {0} pid {1} running (not owned by this wrapper)." -f $entry.Name, $process.Id)
    } else {
      Stop-Process -Id $process.Id
      $process.WaitForExit(5000) | Out-Null
      Write-Output ("Stopped owned {0} pid {1}." -f $entry.Name, $process.Id)
    }
  }
  Remove-Item -LiteralPath $statePath -Force
  exit 0
}

if (-not (Test-Path -LiteralPath $serverExe) -or -not (Test-Path -LiteralPath $clientExe)) {
  throw "LG Duel executables are unavailable. Run: cmake --preset default; cmake --build --preset default"
}
New-Item -ItemType Directory -Force -Path $stateDir | Out-Null

$existingStatus = & python $controlScript --port $ControlPort --timeout 2 --json status 2>$null
if ($LASTEXITCODE -eq 0) {
  if ($Benchmark) {
    $parsedStatus = $existingStatus | ConvertFrom-Json
    if (-not [bool]$parsedStatus.benchmark_enabled) {
      throw "A development-control client is already running without --benchmark. Stop the owned development client before starting a benchmark."
    }
  }
  Write-Output "A development-control client already answers on port $ControlPort; no duplicate was launched."
  Write-Output $existingStatus
  exit 0
}

$server = Find-ExactProcess $serverExe
$serverOwned = $false
if ($null -eq $server) {
  $server = Start-Process -FilePath $serverExe -ArgumentList @($ServerPort) `
    -WorkingDirectory $buildDir -WindowStyle Hidden -PassThru `
    -RedirectStandardOutput (Join-Path $stateDir 'server.stdout.log') `
    -RedirectStandardError (Join-Path $stateDir 'server.stderr.log')
  $serverOwned = $true
  Write-Output "Started server pid $($server.Id)."
} else {
  Write-Output "Attached to existing repository server pid $($server.Id)."
}

$oldBackend = $env:LG_DUEL_RENDER_BACKEND
$env:LG_DUEL_RENDER_BACKEND = $(if ($Renderer -eq 'gpu') { 'gpu' } else { 'fallback' })
try {
  $clientArguments = @('127.0.0.1', $ServerPort, '--dev-control', '--control-port', $ControlPort)
  if ($Benchmark) { $clientArguments += '--benchmark' }
  $client = Start-Process -FilePath $clientExe `
    -ArgumentList $clientArguments `
    -WorkingDirectory $buildDir -WindowStyle Hidden -PassThru `
    -RedirectStandardOutput (Join-Path $stateDir 'client.stdout.log') `
    -RedirectStandardError (Join-Path $stateDir 'client.stderr.log')
} finally {
  $env:LG_DUEL_RENDER_BACKEND = $oldBackend
}
Write-Output "Started development client pid $($client.Id)."

@{
  server = @{ pid = $server.Id; owned = $serverOwned; path = $serverExe }
  client = @{ pid = $client.Id; owned = $true; path = $clientExe }
  server_port = $ServerPort
  control_port = $ControlPort
  benchmark = [bool]$Benchmark
  started_at = (Get-Date).ToUniversalTime().ToString('o')
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $statePath -Encoding UTF8

$deadline = [DateTime]::UtcNow.AddSeconds(20)
do {
  Start-Sleep -Milliseconds 250
  $status = & python $controlScript --port $ControlPort --timeout 2 --json status 2>$null
  if ($LASTEXITCODE -eq 0) {
    Write-Output 'Development control is ready.'
    Write-Output $status
    exit 0
  }
  if ($client.HasExited) {
    throw "LG Duel client exited during startup. See build/dev-control/client.stderr.log"
  }
} while ([DateTime]::UtcNow -lt $deadline)

throw "Development control did not become ready within 20 seconds. See build/dev-control logs."
