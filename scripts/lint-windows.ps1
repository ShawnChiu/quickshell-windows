#Requires -Version 7
# Checks the tree as the Windows CI job does: clang-format over every source
# file and clang-tidy over every file the Windows build compiles, with the LLVM
# version in .github/llvm-version. Run from a build environment (see
# .github/README.md) after configuring the lint build once:
#
#   cmake --preset windows-lint -DCMAKE_PREFIX_PATH=C:/Qt/<version>/msvc2022_64
#   scripts\lint-windows.ps1 [-NoBuild]
#
# clang-tidy findings listed in .github/tidy-baseline.txt are accepted. Any
# other finding fails, and so does a baseline entry that no longer occurs.

[CmdletBinding()]
param(
    [switch]$NoBuild
)

$ErrorActionPreference = 'Stop'

function Invoke-Tool([scriptblock]$Command) {
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { & $Command } finally { $ErrorActionPreference = $previous }
}

$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$buildDir = Join-Path $root 'build\lint'
$baselinePath = Join-Path $root '.github\tidy-baseline.txt'
$version = (Get-Content -LiteralPath (Join-Path $root '.github\llvm-version') -Raw).Trim()

foreach ($tool in 'clang-format', 'clang-tidy') {
    $output = (Invoke-Tool { & $tool --version 2>&1 }) -join "`n"
    if ($output -notmatch "version $([regex]::Escape($version))(\s|$)") {
        throw "$tool on PATH is not LLVM ${version}:`n$output"
    }
}

Push-Location $root
try {
    $failed = $false

    # .clang-format wants LF, which a checkout with core.autocrlf does not have,
    # so files go through stdin with their line endings normalized.
    $sources = git ls-files -- 'src/*.cpp' 'src/*.hpp'
    $unformatted = $sources | ForEach-Object -ThrottleLimit ([Environment]::ProcessorCount) -Parallel {
        $text = [IO.File]::ReadAllText((Join-Path $using:root $_)).Replace("`r`n", "`n")
        $info = [Diagnostics.ProcessStartInfo]::new('clang-format')
        $info.ArgumentList.Add("--assume-filename=$_")
        $info.RedirectStandardInput = $true
        $info.RedirectStandardOutput = $true
        $info.StandardInputEncoding = [Text.UTF8Encoding]::new($false)
        $info.StandardOutputEncoding = [Text.UTF8Encoding]::new($false)
        $info.WorkingDirectory = $using:root

        $process = [Diagnostics.Process]::Start($info)
        $process.StandardInput.Write($text)
        $process.StandardInput.Close()
        $formatted = $process.StandardOutput.ReadToEnd()
        $process.WaitForExit()

        if ($process.ExitCode -ne 0 -or $formatted -ne $text) { $_ }
    } | Sort-Object

    if ($unformatted) {
        $failed = $true
        Write-Host "Not formatted (run clang-format -i on them):"
        $unformatted | ForEach-Object { Write-Host "  $_" }
    }

    if (-not $NoBuild) {
        Invoke-Tool { cmake --build $buildDir }
        if ($LASTEXITCODE -ne 0) { throw "Building $buildDir failed." }
    }

    $database = Join-Path $buildDir 'compile_commands.json'
    if (-not (Test-Path -LiteralPath $database)) {
        throw "$database not found -- configure with cmake --preset windows-lint first."
    }

    $srcDir = Join-Path $root 'src\'
    $files = (Get-Content -LiteralPath $database -Raw | ConvertFrom-Json).file |
        ForEach-Object { [IO.Path]::GetFullPath($_) } |
        Where-Object { $_.StartsWith($srcDir, [StringComparison]::OrdinalIgnoreCase) } |
        Sort-Object -Unique

    $runs = $files | ForEach-Object -ThrottleLimit ([Environment]::ProcessorCount) -Parallel {
        $output = & clang-tidy -p $using:buildDir --quiet $_ 2>&1 | ForEach-Object { "$_" }
        [pscustomobject]@{ File = $_; ExitCode = $LASTEXITCODE; Output = $output }
    }

    $pattern = '^(?<path>(?:[A-Za-z]:)?[^:]+):(?<line>\d+):\d+: (?:warning|error): (?<message>.+) \[(?<check>[^\],]+)[^\]]*\]$'
    # Keyed by location, as headers are reported once per file including them.
    $findings = @{}
    foreach ($run in $runs) {
        $matched = $false
        foreach ($line in $run.Output) {
            if ($line -notmatch $pattern) { continue }
            $matched = $true
            $path = [IO.Path]::GetFullPath($Matches.path)
            if (-not $path.StartsWith($srcDir, [StringComparison]::OrdinalIgnoreCase)) { continue }

            $relative = $path.Substring($root.Length + 1).Replace('\', '/')
            $location = "${relative}:$($Matches.line): [$($Matches.check)] $($Matches.message)"
            $findings[$location] = "${relative}: [$($Matches.check)] $($Matches.message)"
        }

        if ($run.ExitCode -ne 0 -and -not $matched) {
            $failed = $true
            Write-Host "clang-tidy failed on $($run.File):"
            $run.Output | Select-Object -Last 20 | ForEach-Object { Write-Host "  $_" }
        }
    }

    # Baseline entries leave out line numbers so they survive upstream syncs.
    # An entry listed n times accepts n findings.
    $accepted = [System.Collections.Generic.List[string]]::new()
    if (Test-Path -LiteralPath $baselinePath) {
        Get-Content -LiteralPath $baselinePath |
            Where-Object { $_.Trim() -and -not $_.StartsWith('#') } |
            ForEach-Object { $accepted.Add($_) }
    }

    $new = foreach ($location in $findings.Keys | Sort-Object) {
        if (-not $accepted.Remove($findings[$location])) { $location }
    }
    $stale = $accepted

    if ($new) {
        $failed = $true
        Write-Host "clang-tidy findings:"
        $new | ForEach-Object { Write-Host "  $_" }
    }

    if ($stale) {
        $failed = $true
        Write-Host "Baseline entries that no longer occur (remove them from .github/tidy-baseline.txt):"
        $stale | ForEach-Object { Write-Host "  $_" }
    }

    if ($failed) { exit 1 }
    Write-Host "Lint passed: $($sources.Count) files formatted, $($files.Count) files tidy."
} finally {
    Pop-Location
}
