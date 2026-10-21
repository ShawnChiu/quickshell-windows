# Quickshell for Windows

An **unofficial** port of [Quickshell](https://quickshell.outfoxxed.me) to Windows 11.

This is a downstream fork that keeps the Windows support as a small set of commits on the
`windows` branch, on top of the latest upstream release. `master` mirrors upstream. Releases of
this fork are tagged `vX.Y.Z-win.N`, for upstream `vX.Y.Z`.

The branch is never force pushed, so `git pull` keeps a clone up to date. Each upstream sync
starts with a `chore: start merging-rebase onto upstream ...` merge commit, and the commits after
the latest one are the current patches.

- Upstream: https://git.outfoxxed.me/quickshell/quickshell
  (mirror: https://github.com/quickshell-mirror/quickshell)
- Upstream's own readme: [README.md](../README.md)

> [!IMPORTANT]
> Please report Windows-specific problems here, not upstream. Upstream does not support Windows.
> See [CONTRIBUTING.md](CONTRIBUTING.md) for what to include and how to contribute.

## Status

Work in progress, used daily as a bar on Windows 11. Configs written for Linux load: panels
reserve screen space like the taskbar, `Process`, IPC and `DesktopEntries` work, UPower, Mpris
and SystemTray are backed by Windows, and the Wayland and other DBus modules can be imported but
do nothing.
`import Quickshell.Windows` adds Windows-only types for idle, window handles, blur, power, audio,
media, the system tray and notifications.

- [PORTING.md](PORTING.md): what works, what differs from Linux, and what is not available.
- [src/windows/module.md](../src/windows/module.md) and the headers in
  [src/windows](../src/windows): how to use `Quickshell.Windows`.

## Building

Install the tools below, and Qt 6.8 or newer for MSVC 2022 64-bit with Qt Shader Tools from the
[Qt Online Installer](https://www.qt.io/download-qt-installer-oss). LLVM must be 19 or newer.

```powershell
winget install Kitware.CMake Ninja-build.Ninja LLVM.LLVM
winget install Microsoft.VisualStudio.2022.BuildTools --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

Clone to a short path such as `C:\src\quickshell`, because some build paths get close to the
260-character path limit. Every new terminal needs the 64-bit MSVC environment with LLVM first
on `PATH` before building:

```powershell
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
& "$vs\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
$env:PATH = "$env:ProgramFiles\LLVM\bin;$env:PATH"
```

Then configure once and build:

```powershell
cmake --preset windows -DCMAKE_PREFIX_PATH=C:/Qt/<version>/msvc2022_64
cmake --build build
```

To run `build\qs.exe` in place, put Qt's `bin` on `PATH`, or use the install script below.

## Installing

```powershell
scripts\install-windows.ps1
```

Installs the build into `%LOCALAPPDATA%\Programs\quickshell`, copies Qt's runtime beside it and
adds its `bin` to the user `PATH`, so `qs` runs without a build environment. `-Prefix` changes
the location and `-NoPath` leaves `PATH` alone. It does not build.

## Running

```powershell
qs -d
```

`quickshell.exe` is a console program, so starting it without `-d` opens a console window.

Configs are searched for in `$XDG_CONFIG_HOME\quickshell` (if set), then
`%USERPROFILE%\.config\quickshell`, then `%APPDATA%\quickshell`. The first one containing a config
wins, the same way `~/.config/quickshell` works on Linux. See the upstream
[documentation](https://quickshell.outfoxxed.me/docs/) for writing configs.

## License

Like upstream, licensed under the GNU LGPL 3. See [LICENSE](../LICENSE).
