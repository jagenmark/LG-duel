$ErrorActionPreference = 'Stop'
$Arguments = @($args)
$repoRoot = Split-Path -Parent $PSScriptRoot
$pythonScript = Join-Path $PSScriptRoot 'lg_control.py'

if ($Arguments.Count -gt 0 -and ($Arguments[0] -eq 'start' -or $Arguments[0] -eq 'stop')) {
  & (Join-Path $PSScriptRoot 'lg-dev.ps1') @Arguments
  exit $LASTEXITCODE
}

python $pythonScript @Arguments
exit $LASTEXITCODE
