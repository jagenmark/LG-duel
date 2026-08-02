$ErrorActionPreference = "Stop"

$expected = "https://github.com/jagenmark/LG-duel.git"
$current = git remote get-url origin 2>$null

if ($LASTEXITCODE -eq 0) {
    if ($current.Trim() -ne $expected) {
        git remote set-url origin $expected
    }
} else {
    git remote add origin $expected
}

Write-Output "Codex worktree origin: $(git remote get-url origin)"
