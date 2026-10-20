# Rebases the windows patch stack onto an upstream release the way Git for
# Windows does ("merging-rebase"), so the branch only ever moves forward and is
# pushed without --force.
#
#   scripts\sync-upstream.ps1 [-Fetch] [-Onto <ref>]
#   scripts\sync-upstream.ps1 -Finish
#
# Each sync puts a marker merge at the bottom of the new stack: its first
# parent is the release, its second parent is the previous windows tip, and its
# tree is the release's (like `-s ours`). Everything since the latest marker is
# the current patch stack (`git log <marker>..windows`). Between syncs, old
# patches are amended with `git commit --fixup=<sha>` or `--fixup=reword:<sha>`;
# the rebase squashes those in and drops patches that end up empty.
#
# -Onto defaults to the newest vX.Y.Z tag. -Fetch fetches upstream and
# fast-forwards master to upstream/master first. If the rebase stops on a
# conflict, resolve it, run `git rebase --continue`, then run this script again
# with -Finish. It never pushes; it prints the push command once the result
# checks out.

[CmdletBinding(DefaultParameterSetName = 'Sync')]
param(
    [Parameter(ParameterSetName = 'Sync')]
    [switch]$Fetch,
    [Parameter(ParameterSetName = 'Sync')]
    [string]$Onto,
    [Parameter(ParameterSetName = 'Finish')]
    [switch]$Finish
)

$ErrorActionPreference = 'Stop'

$branch = 'windows'
$markerPrefix = 'chore: start merging-rebase onto upstream'

# Runs git, throwing on a non-zero exit code. Git writes progress to stderr,
# which is not a failure.
function Invoke-Git {
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { $output = & git @args } finally { $ErrorActionPreference = $previous }
    if ($LASTEXITCODE -ne 0) { throw "git $args failed with $LASTEXITCODE." }
    $output
}

# Runs git for its exit code only.
function Test-Git {
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { & git @args 2>&1 | Out-Null } finally { $ErrorActionPreference = $previous }
    $LASTEXITCODE -eq 0
}

function Test-RebaseInProgress {
    foreach ($dir in 'rebase-merge', 'rebase-apply') {
        if (Test-Path -LiteralPath (Invoke-Git rev-parse --git-path $dir)) { return $true }
    }
    $false
}

# The last command a stopped rebase ran, or the next one it will run.
function Get-RebaseCommand([string]$File, [switch]$Last) {
    $path = Invoke-Git rev-parse --git-path "rebase-merge/$File"
    if (-not (Test-Path -LiteralPath $path)) { return '' }
    $lines = @(Get-Content -LiteralPath $path | Where-Object { $_ -match '^\s*[^#\s]' })
    if (-not $lines) { return '' }
    $line = if ($Last) { $lines[-1] } else { $lines[0] }
    ($line.Trim() -split '\s+')[0]
}

# Continues a stopped rebase until it completes. --empty=drop does not cover a
# patch whose fixups cancel it out (as of git 2.40 the rebase stops with "would
# make it empty"), so such a patch is dropped here, as git's message suggests.
# Returns false if the rebase stopped for something a person must resolve.
function Complete-Rebase {
    while (Test-RebaseInProgress) {
        if (Invoke-Git diff --name-only --diff-filter=U) { return $false }

        $emptied = (Get-RebaseCommand done -Last) -in 'f', 'fixup', 's', 'squash' -and
            (Get-RebaseCommand git-rebase-todo) -notin 'f', 'fixup', 's', 'squash' -and
            (Test-Git diff --cached --quiet 'HEAD^')
        if ($emptied) {
            Write-Host "Dropping $(Invoke-Git log -1 --format='%h %s' HEAD), its fixups cancel it out" -ForegroundColor Cyan
            Invoke-Git reset -q --soft 'HEAD^'
        }

        $before = "$(Invoke-Git rev-parse HEAD) $(Get-RebaseCommand git-rebase-todo)"
        $previous = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        try { & git rebase --continue } finally { $ErrorActionPreference = $previous }
        if ((Test-RebaseInProgress) -and $LASTEXITCODE -ne 0 -and
            "$(Invoke-Git rev-parse HEAD) $(Get-RebaseCommand git-rebase-todo)" -eq $before) {
            return $false
        }
    }
    $true
}

$stateFile = Invoke-Git rev-parse --git-path sync-upstream.json

if (-not $Finish) {
    if (Test-RebaseInProgress) {
        throw 'A rebase is in progress. Finish it, or run the script with -Finish.'
    }
    if ((Invoke-Git symbolic-ref --short -q HEAD) -ne $branch) {
        throw "Check out $branch first."
    }
    if (Invoke-Git status --porcelain --untracked-files=no) {
        throw 'The working tree has uncommitted changes.'
    }

    if ($Fetch) {
        Write-Host 'Fetching upstream and origin' -ForegroundColor Cyan
        Invoke-Git fetch --tags upstream
        Invoke-Git fetch origin
        if (-not (Test-Git merge-base --is-ancestor master upstream/master)) {
            throw 'master has diverged from upstream/master; it must only mirror upstream.'
        }
        Invoke-Git update-ref refs/heads/master upstream/master
    }

    if (-not $Onto) {
        $Onto = Invoke-Git tag --list 'v*' --sort=-v:refname |
            Where-Object { $_ -match '^v\d+\.\d+\.\d+$' } |
            Select-Object -First 1
        if (-not $Onto) { throw 'No vX.Y.Z tag found; fetch upstream or pass -Onto.' }
    }

    if (-not (Test-Git merge-base --is-ancestor "origin/$branch" $branch)) {
        throw "$branch does not contain origin/$branch; pull it before syncing."
    }

    $oldTip = Invoke-Git rev-parse $branch
    $target = Invoke-Git rev-parse --verify "$Onto^{commit}"

    # The previous sync's marker on the first-parent history, or the fork
    # point before the first sync.
    $base = Invoke-Git log --first-parent --merges --format='%H %s' $branch |
        Where-Object { $_.Substring(41).StartsWith($markerPrefix) } |
        Select-Object -First 1
    if ($base) { $base = $base.Substring(0, 40) }
    else { $base = Invoke-Git merge-base $branch $target }

    $tag = "backup/$branch-$(Get-Date -Format 'yyyy-MM-dd')"
    for ($i = 2; (Test-Git rev-parse -q --verify "refs/tags/$tag") -and
        (Invoke-Git rev-parse "$tag^{commit}") -ne $oldTip; $i++) {
        $tag = "backup/$branch-$(Get-Date -Format 'yyyy-MM-dd')-$i"
    }
    Invoke-Git tag -f $tag $oldTip | Out-Null
    Write-Host "Tagged the current tip as $tag" -ForegroundColor Cyan

    $marker = Invoke-Git commit-tree "$target^{tree}" -p $target -p $oldTip -m "$markerPrefix $Onto"

    @{ oldTip = $oldTip; base = $base; marker = $marker; fetched = [bool]$Fetch } | ConvertTo-Json |
        Set-Content -LiteralPath $stateFile

    Write-Host "Rebasing $branch onto $Onto" -ForegroundColor Cyan
    $env:GIT_SEQUENCE_EDITOR = ':'
    try {
        $previous = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        try { & git rebase -i --autosquash --empty=drop --onto $marker $base $branch }
        finally { $ErrorActionPreference = $previous }
    }
    finally {
        Remove-Item Env:GIT_SEQUENCE_EDITOR
    }
}

if (-not (Test-Path -LiteralPath $stateFile)) {
    throw "$stateFile not found; run the script without -Finish first."
}
$state = Get-Content -LiteralPath $stateFile -Raw | ConvertFrom-Json

if (-not (Complete-Rebase)) {
    Write-Host 'The rebase stopped. Resolve the conflicts, run `git rebase --continue`, then run' -ForegroundColor Yellow
    Write-Host '  scripts\sync-upstream.ps1 -Finish' -ForegroundColor Yellow
    exit 1
}
if ((Invoke-Git symbolic-ref --short -q HEAD) -ne $branch) {
    throw "The rebase did not finish on $branch."
}

if (-not (Test-Git merge-base --is-ancestor $state.marker $branch)) {
    throw "$branch is not based on the marker $($state.marker); the rebase did not complete."
}
if (-not (Test-Git merge-base --is-ancestor $state.oldTip $branch)) {
    throw "The old tip $($state.oldTip) is not an ancestor of $branch; a push would need --force."
}

Write-Host 'Patch stack, before and after:' -ForegroundColor Cyan
Invoke-Git --no-pager range-diff "$($state.base)..$($state.oldTip)" "$($state.marker)..$branch" | Out-Host

Remove-Item -LiteralPath $stateFile
Write-Host 'Done. Check the result, build it, then push without --force:' -ForegroundColor Green
Write-Host "  git push origin $branch"
if ($state.fetched) { Write-Host '  git push origin master' }
