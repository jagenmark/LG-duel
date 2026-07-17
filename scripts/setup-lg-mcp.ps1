param(
  [switch]$Remove
)

$ErrorActionPreference = 'Stop'
$serverPath = Join-Path $PSScriptRoot 'lg_mcp_server.py'

if ($Remove) {
  codex mcp remove lg-duel
  exit $LASTEXITCODE
}

codex mcp add lg-duel -- python $serverPath
if ($LASTEXITCODE -ne 0) {
  throw 'Codex MCP registration failed. Run the printed codex mcp add command from a Codex CLI-enabled terminal.'
}

Write-Output 'Registered local MCP server "lg-duel". Restart Codex or start a new task to load its tools.'
