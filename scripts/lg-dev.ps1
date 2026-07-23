param(
  [Parameter(Position = 0)]
  [ValidateSet('start', 'stop', 'restart', 'status')]
  [string]$Action = 'status',
  [int]$ServerPort = 27960,
  [int]$ControlPort = 27961,
  [ValidateSet('gpu', 'fallback')]
  [string]$Renderer = 'gpu',
  [switch]$ExternalServer,
  [switch]$Benchmark
)

$ErrorActionPreference = 'Stop'
$launcher = Join-Path $PSScriptRoot 'lg_launch.py'
$python = @(Get-Command python.exe -CommandType Application -ErrorAction Stop)[0].Source
$arguments = @($launcher, '--json', $Action)

if ($Action -eq 'start') {
  $arguments += @(
    '--server-port', $ServerPort,
    '--control-port', $ControlPort,
    '--renderer', $Renderer
  )
  if ($Renderer -eq 'fallback') { $arguments += '--allow-fallback' }
  if ($ExternalServer) { $arguments += '--external-server' }
  if ($Benchmark) { $arguments += '--benchmark' }
} elseif ($Action -eq 'restart') {
  $arguments += @('--renderer', $Renderer)
  if ($Renderer -eq 'fallback') { $arguments += '--allow-fallback' }
} elseif ($Action -eq 'status') {
  $arguments += @('--control-port', $ControlPort)
}

& $python @arguments
exit $LASTEXITCODE
