# Installs the build into a per-user prefix and copies Qt's runtime beside it,
# so `qs` runs from PATH and at logon without a build environment. Run after
# building (see .github/README.md); this script does not build.
#
#   scripts\install-windows.ps1 [-BuildDir <dir>] [-Prefix <dir>] [-NoPath]
#
# cmake --install lays out <Prefix>\bin\{quickshell.exe,qs.exe}. The shell's
# own QML is linked into the executable, so windeployqt cannot see its imports;
# a stub stands in for them, or the Qt Quick modules the shell needs are left
# behind and every window it makes fails to create.

[CmdletBinding()]
param(
    [string]$BuildDir = (Join-Path $PSScriptRoot '..\build'),
    [string]$Prefix = (Join-Path $env:LOCALAPPDATA 'Programs\quickshell'),
    [switch]$NoPath
)

$ErrorActionPreference = 'Stop'

# A native tool's stderr is not a failure (windeployqt warns about the missing
# DirectX shader compiler, which this Qt does not ship and the shell does not
# need). Check the exit code instead of letting PowerShell throw.
function Invoke-Tool([scriptblock]$Command) {
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { & $Command } finally { $ErrorActionPreference = $previous }
}

$BuildDir = (Resolve-Path -LiteralPath $BuildDir).Path

# windeployqt comes from the Qt the build was configured against, which is not
# necessarily the one on PATH.
$cache = Join-Path $BuildDir 'CMakeCache.txt'
if (-not (Test-Path -LiteralPath $cache)) {
    throw "$cache not found -- configure and build the project first (see .github/README.md)."
}

$qtPrefix = (Select-String -Path $cache -Pattern '^CMAKE_PREFIX_PATH:\w+=(.+)$').Matches.Groups[1].Value.Trim()
$windeployqt = Join-Path $qtPrefix 'bin\windeployqt.exe'
if (-not (Test-Path -LiteralPath $windeployqt)) {
    throw "windeployqt not found at $windeployqt (CMAKE_PREFIX_PATH was $qtPrefix)."
}

Write-Host "Installing into $Prefix" -ForegroundColor Cyan
Invoke-Tool { cmake --install $BuildDir --prefix $Prefix }
if ($LASTEXITCODE -ne 0) { throw "cmake --install failed with $LASTEXITCODE." }

# Every module the shell's own QML imports, so windeployqt resolves them.
$stub = Join-Path ([IO.Path]::GetTempPath()) ('qs-qmlstub-' + [guid]::NewGuid())
New-Item -ItemType Directory -Path $stub | Out-Null
try {
    Set-Content -LiteralPath (Join-Path $stub 'stub.qml') -Value @'
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Fusion
import QtQuick.Layouts
import QtQuick.Shapes
import QtQuick.Effects
import QtQuick.Window
Item {}
'@
    Invoke-Tool {
        & $windeployqt --qmldir $stub --release --no-translations --no-system-d3d-compiler `
            --no-opengl-sw (Join-Path $Prefix 'bin\quickshell.exe')
    }
    if ($LASTEXITCODE -ne 0) { throw "windeployqt failed with $LASTEXITCODE." }
}
finally {
    Remove-Item -Recurse -Force $stub -ErrorAction SilentlyContinue
}

$binDir = Join-Path $Prefix 'bin'
if (-not $NoPath) {
    $parts = @([Environment]::GetEnvironmentVariable('Path', 'User') -split ';' | Where-Object { $_ })
    if ($parts -notcontains $binDir) {
        [Environment]::SetEnvironmentVariable('Path', ($parts + $binDir) -join ';', 'User')
        Write-Host "Added $binDir to the user PATH (takes effect in new shells)." -ForegroundColor Green
    }
}

Write-Host "Installed: $binDir\qs.exe" -ForegroundColor Green
