param(
  [Parameter(ValueFromRemainingArguments = $true)]
  [string[]]$Arguments
)

$ErrorActionPreference = 'Stop'
$scriptPath = Join-Path $PSScriptRoot 'lg_benchmark.py'
& python $scriptPath @Arguments
exit $LASTEXITCODE
