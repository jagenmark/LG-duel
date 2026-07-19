param(
  [switch]$Remove
)

$ErrorActionPreference = 'Stop'
$serverPath = (Resolve-Path (Join-Path $PSScriptRoot 'lg_mcp_server.py')).Path
$python = @(Get-Command python.exe -CommandType Application -ErrorAction Stop)[0]
$codex = @(Get-Command codex.exe -CommandType Application -ErrorAction Stop)[0]
$pythonPath = [IO.Path]::GetFullPath($python.Source)
$codexHome = if ([string]::IsNullOrWhiteSpace($env:CODEX_HOME)) {
  Join-Path $env:USERPROFILE '.codex'
} else {
  [IO.Path]::GetFullPath($env:CODEX_HOME)
}
$configPath = Join-Path $codexHome 'config.toml'
$registrationScope = if ([string]::IsNullOrWhiteSpace($env:CODEX_HOME)) {
  'current Windows user profile'
} else {
  'CODEX_HOME override'
}

Write-Output ("Registration host: {0}\{1}" -f $env:COMPUTERNAME, [Environment]::UserName)
Write-Output ("Registration scope: {0}" -f $registrationScope)
Write-Output ("User profile: {0}" -f [IO.Path]::GetFullPath($env:USERPROFILE))
Write-Output ("Codex config: {0}" -f $configPath)
Write-Output ("Codex executable: {0}" -f [IO.Path]::GetFullPath($codex.Source))
Write-Output ("Python executable: {0}" -f $pythonPath)
Write-Output ("MCP server: {0}" -f $serverPath)

if ($Remove) {
  & $codex.Source mcp remove lg-duel
  exit $LASTEXITCODE
}

& $codex.Source mcp add lg-duel -- $pythonPath $serverPath
if ($LASTEXITCODE -ne 0) {
  throw "Codex MCP registration failed for host '$env:COMPUTERNAME' and config '$configPath'."
}

$registered = & $codex.Source mcp list 2>&1
if ($LASTEXITCODE -ne 0) {
  throw "Codex MCP registration was written, but 'codex mcp list' failed for '$configPath': $registered"
}
Write-Output $registered
if (($registered | Out-String) -notmatch '(?m)^\s*lg-duel(?:\s|$)') {
  throw "Codex MCP registration verification failed: lg-duel is absent from 'codex mcp list' for '$configPath'."
}

Write-Output 'Registered and verified local MCP server "lg-duel". Restart Codex or start a new task to load its tools.'
