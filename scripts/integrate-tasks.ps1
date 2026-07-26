param(
  [Parameter(Mandatory = $true)]
  [string]$ManifestPath,
  [switch]$DryRun
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2.0

function Invoke-Git {
  param(
    [Parameter(Mandatory = $true)]
    [string]$WorkingDirectory,
    [Parameter(Mandatory = $true)]
    [string[]]$Arguments,
    [switch]$AllowFailure
  )

  $oldErrorActionPreference = $ErrorActionPreference
  $ErrorActionPreference = "Continue"
  try {
    $output = @(& git -C $WorkingDirectory @Arguments 2>&1)
    $exitCode = $LASTEXITCODE
  } finally {
    $ErrorActionPreference = $oldErrorActionPreference
  }
  if ($exitCode -ne 0 -and -not $AllowFailure) {
    $detail = ($output | ForEach-Object { "$_" }) -join [Environment]::NewLine
    throw "git $($Arguments -join ' ') failed with exit code $exitCode.`n$detail"
  }
  return [pscustomobject]@{
    ExitCode = $exitCode
    Output = @($output | ForEach-Object { "$_" })
  }
}

function Resolve-RepositoryPath {
  param(
    [Parameter(Mandatory = $true)]
    [string]$RepositoryRoot,
    [Parameter(Mandatory = $true)]
    [string]$Path
  )

  if ([IO.Path]::IsPathRooted($Path)) {
    return [IO.Path]::GetFullPath($Path)
  }
  return [IO.Path]::GetFullPath((Join-Path $RepositoryRoot $Path))
}

function Test-SameOrChildPath {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Path,
    [Parameter(Mandatory = $true)]
    [string]$Parent
  )

  $fullPath = [IO.Path]::GetFullPath($Path).TrimEnd("\", "/")
  $fullParent = [IO.Path]::GetFullPath($Parent).TrimEnd("\", "/")
  if ($fullPath.Equals($fullParent, [StringComparison]::OrdinalIgnoreCase)) {
    return $true
  }
  return $fullPath.StartsWith(
    $fullParent + [IO.Path]::DirectorySeparatorChar,
    [StringComparison]::OrdinalIgnoreCase
  )
}

function Get-GitCommonDirectory {
  param(
    [Parameter(Mandatory = $true)]
    [string]$WorkingDirectory
  )

  $value = (Invoke-Git $WorkingDirectory @("rev-parse", "--path-format=absolute", "--git-common-dir")).Output[-1].Trim()
  return [IO.Path]::GetFullPath($value).TrimEnd("\", "/")
}

function Get-PropertyValue {
  param(
    [Parameter(Mandatory = $true)]
    [object]$Object,
    [Parameter(Mandatory = $true)]
    [string]$Name,
    $Default = $null
  )

  $property = $Object.PSObject.Properties[$Name]
  if ($null -eq $property) {
    return $Default
  }
  return $property.Value
}

function Resolve-Commit {
  param(
    [Parameter(Mandatory = $true)]
    [string]$RepositoryRoot,
    [Parameter(Mandatory = $true)]
    [string]$Ref
  )

  $result = Invoke-Git $RepositoryRoot @("rev-parse", "--verify", "$Ref`^{commit}")
  return $result.Output[-1].Trim()
}

function Assert-NonMergeCommit {
  param(
    [Parameter(Mandatory = $true)]
    [string]$RepositoryRoot,
    [Parameter(Mandatory = $true)]
    [string]$Commit
  )

  $line = (Invoke-Git $RepositoryRoot @("rev-list", "--parents", "-n", "1", $Commit)).Output[-1]
  if (($line -split "\s+").Count -gt 2) {
    throw "Merge commit $Commit is not supported. List its non-merge commits instead."
  }
}

function Test-PickRecorded {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Worktree,
    [Parameter(Mandatory = $true)]
    [string]$Commit
  )

  $ancestor = Invoke-Git $Worktree @("merge-base", "--is-ancestor", $Commit, "HEAD") -AllowFailure
  if ($ancestor.ExitCode -eq 0) {
    return $true
  }
  $marker = "(cherry picked from commit $Commit)"
  $log = (Invoke-Git $Worktree @("log", "--format=%B", "--fixed-strings", "--grep=$marker", "-n", "1")).Output
  return $log.Count -gt 0
}

function Write-IntegrationReport {
  param(
    [Parameter(Mandatory = $true)]
    [string]$Path,
    [Parameter(Mandatory = $true)]
    [object]$Lines,
    [Parameter(Mandatory = $true)]
    [string]$Worktree,
    [Parameter(Mandatory = $true)]
    [string]$RepositoryPath
  )

  $tracked = Invoke-Git $Worktree @("ls-files", "--error-unmatch", "--", $RepositoryPath) -AllowFailure
  if ($tracked.ExitCode -eq 0) {
    throw "Refusing to overwrite tracked report path: $RepositoryPath"
  }
  $parent = Split-Path -Parent $Path
  New-Item -ItemType Directory -Force -Path $parent | Out-Null
  $Lines | Set-Content -LiteralPath $Path -Encoding utf8
}

$scriptRoot = Split-Path -Parent $PSScriptRoot
$repositoryRoot = (Invoke-Git $scriptRoot @("rev-parse", "--show-toplevel")).Output[-1].Trim()
$manifestFullPath = if ([IO.Path]::IsPathRooted($ManifestPath)) {
  [IO.Path]::GetFullPath($ManifestPath)
} else {
  [IO.Path]::GetFullPath((Join-Path (Get-Location).Path $ManifestPath))
}
if (-not (Test-Path -LiteralPath $manifestFullPath -PathType Leaf)) {
  throw "Manifest not found: $manifestFullPath"
}

$manifest = Get-Content -Raw -LiteralPath $manifestFullPath | ConvertFrom-Json
if ((Get-PropertyValue $manifest "version") -ne 1) {
  throw "Manifest version must be 1."
}

$feature = [string](Get-PropertyValue $manifest "feature")
$baseRef = [string](Get-PropertyValue $manifest "base")
$integrationBranch = [string](Get-PropertyValue $manifest "integrationBranch")
$worktreeValue = [string](Get-PropertyValue $manifest "worktreePath")
$reportValue = [string](Get-PropertyValue $manifest "reportPath" "reports/integration/latest.generated.md")
if ([string]::IsNullOrWhiteSpace($feature) -or
    [string]::IsNullOrWhiteSpace($baseRef) -or
    [string]::IsNullOrWhiteSpace($integrationBranch) -or
    [string]::IsNullOrWhiteSpace($worktreeValue)) {
  throw "Manifest feature, base, integrationBranch, and worktreePath are required."
}
if ($feature -notmatch "^[a-z0-9][a-z0-9-]*$") {
  throw "Manifest feature must use lower-case letters, numbers, and hyphens."
}
if ($integrationBranch -eq $baseRef) {
  throw "integrationBranch must differ from base."
}
if (-not $integrationBranch.StartsWith("integration/", [StringComparison]::Ordinal)) {
  throw "integrationBranch must start with 'integration/'."
}

$baseCommit = Resolve-Commit $repositoryRoot $baseRef
$worktreePath = Resolve-RepositoryPath $repositoryRoot $worktreeValue
$reportUnixPath = $reportValue.Replace("\", "/")
if ([IO.Path]::IsPathRooted($reportValue) -or
    $reportUnixPath -notlike "reports/integration/*.generated.md" -or
    $reportUnixPath.Split("/") -contains "..") {
  throw "reportPath must be a relative reports/integration/*.generated.md path."
}
if (Test-SameOrChildPath $worktreePath $repositoryRoot) {
  throw "worktreePath must be outside the source worktree: $repositoryRoot"
}
$groups = @(Get-PropertyValue $manifest "groups" @())
if ($groups.Count -eq 0) {
  throw "Manifest groups must contain at least one group."
}

$resolvedGroups = New-Object System.Collections.Generic.List[object]
foreach ($group in $groups) {
  $groupName = [string](Get-PropertyValue $group "name")
  if ([string]::IsNullOrWhiteSpace($groupName)) {
    throw "Each group needs a name."
  }
  $commits = New-Object System.Collections.Generic.List[string]
  foreach ($item in @(Get-PropertyValue $group "items" @())) {
    $itemType = [string](Get-PropertyValue $item "type")
    $itemRef = [string](Get-PropertyValue $item "ref")
    if ([string]::IsNullOrWhiteSpace($itemRef)) {
      throw "Group '$groupName' has an item without a ref."
    }
    if ($itemType -eq "commit") {
      $commit = Resolve-Commit $repositoryRoot $itemRef
      Assert-NonMergeCommit $repositoryRoot $commit
      $commits.Add($commit)
    } elseif ($itemType -eq "branch") {
      $fromRef = [string](Get-PropertyValue $item "from" $baseRef)
      $fromCommit = Resolve-Commit $repositoryRoot $fromRef
      $tipCommit = Resolve-Commit $repositoryRoot $itemRef
      $range = "$fromCommit..$tipCommit"
      $rangeResult = Invoke-Git $repositoryRoot @("rev-list", "--reverse", "--topo-order", $range)
      foreach ($commit in $rangeResult.Output) {
        if (-not [string]::IsNullOrWhiteSpace($commit)) {
          $commit = $commit.Trim()
          Assert-NonMergeCommit $repositoryRoot $commit
          $commits.Add($commit)
        }
      }
    } else {
      throw "Group '$groupName' item '$itemRef' has type '$itemType'; use commit or branch."
    }
  }
  $resolvedGroups.Add([pscustomobject]@{
    Name = $groupName
    Commits = $commits
    Validation = @(Get-PropertyValue $group "validation" @())
  })
}

foreach ($commit in @($baseCommit) + @($resolvedGroups | ForEach-Object { $_.Commits })) {
  $trackedInCommit = (Invoke-Git $repositoryRoot @("ls-tree", "-r", "--name-only", $commit, "--", $reportUnixPath)).Output
  if ($trackedInCommit.Count -gt 0) {
    throw "Commit $commit tracks reserved report path '$reportUnixPath'. Choose another reportPath or omit that commit."
  }
}

if ($DryRun) {
  Write-Host "Dry run: no branch or worktree changes will be made."
  Write-Host "Feature: $feature"
  Write-Host "Base: $baseRef ($baseCommit)"
  Write-Host "Integration branch: $integrationBranch"
  Write-Host "Worktree: $worktreePath"
  foreach ($group in $resolvedGroups) {
    Write-Host "Group: $($group.Name)"
    foreach ($commit in $group.Commits) {
      Write-Host "  $commit"
    }
    foreach ($check in $group.Validation) {
      $arguments = @((Get-PropertyValue $check "arguments" @()))
      Write-Host "  check: $([string](Get-PropertyValue $check 'command')) $($arguments -join ' ')"
    }
  }
  exit 0
}

$branchExists = (Invoke-Git $repositoryRoot @("show-ref", "--verify", "--quiet", "refs/heads/$integrationBranch") -AllowFailure).ExitCode -eq 0
if ($branchExists) {
  $basedOnChosenBase = Invoke-Git $repositoryRoot @("merge-base", "--is-ancestor", $baseCommit, $integrationBranch) -AllowFailure
  if ($basedOnChosenBase.ExitCode -ne 0) {
    throw "Existing branch '$integrationBranch' is not based on $baseRef. Use a new integration branch."
  }
}

$listedWorktrees = (Invoke-Git $repositoryRoot @("worktree", "list", "--porcelain")).Output
foreach ($line in $listedWorktrees) {
  if ($line.StartsWith("worktree ")) {
    $listedPath = $line.Substring("worktree ".Length)
    if ((Test-SameOrChildPath $worktreePath $listedPath) -and
        -not [IO.Path]::GetFullPath($worktreePath).Equals(
          [IO.Path]::GetFullPath($listedPath),
          [StringComparison]::OrdinalIgnoreCase
        )) {
      throw "worktreePath must not be inside another worktree: $listedPath"
    }
  }
}

if (Test-Path -LiteralPath $worktreePath) {
  if (-not (Test-Path -LiteralPath $worktreePath -PathType Container)) {
    throw "Worktree path exists but is not a directory: $worktreePath"
  }
  $actualRoot = (Invoke-Git $worktreePath @("rev-parse", "--show-toplevel")).Output[-1].Trim()
  if ([IO.Path]::GetFullPath($actualRoot) -ne [IO.Path]::GetFullPath($worktreePath)) {
    throw "Path is not the root of a Git worktree: $worktreePath"
  }
  $sourceCommonDirectory = Get-GitCommonDirectory $repositoryRoot
  $targetCommonDirectory = Get-GitCommonDirectory $worktreePath
  if (-not $sourceCommonDirectory.Equals($targetCommonDirectory, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Worktree belongs to another repository: $worktreePath"
  }
  $actualBranch = (Invoke-Git $worktreePath @("symbolic-ref", "--quiet", "--short", "HEAD")).Output[-1].Trim()
  if ($actualBranch -ne $integrationBranch) {
    throw "Worktree '$worktreePath' uses '$actualBranch', not '$integrationBranch'."
  }
} else {
  $parentPath = Split-Path -Parent $worktreePath
  New-Item -ItemType Directory -Force -Path $parentPath | Out-Null
  if ($branchExists) {
    Invoke-Git $repositoryRoot @("worktree", "add", $worktreePath, $integrationBranch) | Out-Null
  } else {
    Invoke-Git $repositoryRoot @("worktree", "add", "-b", $integrationBranch, $worktreePath, $baseCommit) | Out-Null
    $branchExists = $true
  }
}

$status = (Invoke-Git $worktreePath @("status", "--porcelain", "--untracked-files=all")).Output
if ($status.Count -gt 0) {
  throw "Integration worktree is not clean: $worktreePath`n$($status -join [Environment]::NewLine)"
}

$reportPath = Resolve-RepositoryPath $worktreePath $reportValue
$trackedReport = Invoke-Git $worktreePath @("ls-files", "--error-unmatch", "--", $reportUnixPath) -AllowFailure
if ($trackedReport.ExitCode -eq 0) {
  throw "reportPath must not name a tracked file: $reportUnixPath"
}
$lockPath = Join-Path (Get-GitCommonDirectory $worktreePath) "lg-integration-helper.runlock"
try {
  $runLock = [IO.File]::Open($lockPath, [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
} catch {
  throw "Another integration run holds the worktree lock: $lockPath"
}

$reportLines = New-Object System.Collections.Generic.List[string]
$reportLines.Add("# Integration report")
$reportLines.Add("")
$reportLines.Add("- Result: running")
$reportLines.Add("- Started: $([DateTime]::UtcNow.ToString('u')) UTC")
$reportLines.Add("- Feature: ``$feature``")
$reportLines.Add("- Base: ``$baseRef`` (``$baseCommit``)")
$reportLines.Add("- Branch: ``$integrationBranch``")
$reportLines.Add("- Worktree: ``$worktreePath``")
$reportLines.Add("")

try {
  foreach ($group in $resolvedGroups) {
    $reportLines.Add("## $($group.Name)")
    $reportLines.Add("")
    foreach ($commit in $group.Commits) {
      if (Test-PickRecorded $worktreePath $commit) {
        Write-Host "Skipping recorded commit $commit"
        $reportLines.Add("- Skipped recorded commit ``$commit``")
        continue
      }
      Write-Host "Cherry-picking $commit"
      $pick = Invoke-Git $worktreePath @(
        "-c", "rerere.enabled=false",
        "-c", "rerere.autoupdate=false",
        "cherry-pick", "-x", $commit
      ) -AllowFailure
      if ($pick.ExitCode -ne 0) {
        $unmerged = (Invoke-Git $worktreePath @("diff", "--name-only", "--diff-filter=U")).Output
        $isConflict = $unmerged.Count -gt 0
        $resultName = if ($isConflict) { "conflict" } else { "cherry-pick failed" }
        $reportLines[2] = "- Result: $resultName"
        $reportLines.Add("- Cherry-pick stopped at ``$commit``; the helper did not resolve files.")
        $reportLines.Add("")
        $reportLines.Add("Recovery: run ``git status`` in the worktree, then resolve and use ``git cherry-pick --continue``; or use ``git cherry-pick --abort``.")
        Write-IntegrationReport $reportPath $reportLines $worktreePath $reportUnixPath
        [Console]::Error.WriteLine("Cherry-pick stopped at $commit. The helper did not resolve files. Worktree: $worktreePath. Run 'git status', then resolve and run 'git cherry-pick --continue', or run 'git cherry-pick --abort'. Report: $reportPath")
        exit 2
      }
      $reportLines.Add("- Picked ``$commit``")
    }

    foreach ($check in $group.Validation) {
      $command = [string](Get-PropertyValue $check "command")
      $arguments = @((Get-PropertyValue $check "arguments" @()))
      if ([string]::IsNullOrWhiteSpace($command)) {
        throw "Group '$($group.Name)' has a validation entry without a command."
      }
      $display = "$command $($arguments -join ' ')".Trim()
      if ($null -eq (Get-Command $command -ErrorAction SilentlyContinue)) {
        Write-Warning "Skipping unavailable validation command: $command"
        $reportLines.Add("- Check skipped; command not found: ``$display``")
        continue
      }
      Write-Host "Running: $display"
      Push-Location $worktreePath
      $oldErrorActionPreference = $ErrorActionPreference
      $ErrorActionPreference = "Continue"
      try {
        & $command @arguments
        $checkExitCode = $LASTEXITCODE
      } finally {
        $ErrorActionPreference = $oldErrorActionPreference
        Pop-Location
      }
      if ($checkExitCode -ne 0) {
        $reportLines[2] = "- Result: validation failed"
        $reportLines.Add("- Check failed ($checkExitCode): ``$display``")
        Write-IntegrationReport $reportPath $reportLines $worktreePath $reportUnixPath
        [Console]::Error.WriteLine("Validation failed in '$($group.Name)': $display. Report: $reportPath")
        exit 3
      }
      $reportLines.Add("- Check passed: ``$display``")
    }
    $reportLines.Add("")
  }

  $reportLines[2] = "- Result: success"
  $reportLines.Add("- Finished: $([DateTime]::UtcNow.ToString('u')) UTC")
  Write-IntegrationReport $reportPath $reportLines $worktreePath $reportUnixPath
  Write-Host "Integration complete. Report: $reportPath"
} finally {
  $runLock.Dispose()
}
