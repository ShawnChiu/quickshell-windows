# Quickshell for Windows

An **unofficial** port of [Quickshell](https://quickshell.outfoxxed.me) to Windows 11.

This is a downstream fork that tracks upstream and keeps the Windows support as a small set of
commits on the `windows` branch. `master` mirrors upstream.

Upstream syncs use a merging rebase, as Git for Windows does: the patches are rebased onto the
new `master` on top of a merge commit (`chore: start merging-rebase onto upstream ...`) whose
second parent is the previous `windows` tip. History only moves forward, so `git pull` updates
a clone. To see the current patches, run `git log <latest marker>..windows`, where
`git log --first-parent --merges -1 windows` finds the latest marker.

- Upstream: https://git.outfoxxed.me/quickshell/quickshell
  (mirror: https://github.com/quickshell-mirror/quickshell)
- Upstream's own readme: [README.md](../README.md)

> [!IMPORTANT]
> Please report Windows-specific problems here, not upstream. Upstream does not support Windows.

## Status

Work in progress, used daily as a bar on Windows 11. Configs written for Linux load: panels
reserve screen space like the taskbar, `Process`, IPC and `DesktopEntries` work, UPower and
Mpris are backed by Windows, and the Wayland and DBus modules can be imported but do nothing.
`import Quickshell.Windows` adds Windows only types for idle, window handles, blur, power, audio
and media.

- [PORTING.md](PORTING.md): what works, what differs from Linux, and what is not available.
- [src/windows/module.md](../src/windows/module.md) and the headers in
  [src/windows](../src/windows): how to use `Quickshell.Windows`.

## Building

Requirements: Visual Studio 2022 Build Tools (Desktop C++ workload), LLVM 19+ for `clang-cl`,
CMake, Ninja, and Qt 6.8+ for `msvc2022_64` with the Qt Shader Tools module. Only `clang-cl` is
supported, `cl` is rejected at configure time. LLVM comes from `winget install LLVM.LLVM` or the
"C++ Clang tools for Windows" component in the Visual Studio Installer. Qt can be installed with
[aqtinstall](https://github.com/miurahr/aqtinstall):

```powershell
winget install miurahr.aqtinstall
aqt install-qt windows desktop 6.10.3 win64_msvc2022_64 -m qtshadertools -O C:\Qt
```

From an "x64 Native Tools" prompt with LLVM's `bin` on `PATH`:

```powershell
cmake --preset windows -DCMAKE_PREFIX_PATH=C:/Qt/6.10.3/msvc2022_64
cmake --build build
```

Qt's `bin` directory has to be on `PATH` to run the result from the build directory.

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
wins, the same way `~/.config/quickshell` works on Linux. `~/.config` comes first because that is
where chezmoi and tools like komorebi and yasb keep their configs on Windows. See the upstream
[documentation](https://quickshell.outfoxxed.me/docs/) for writing configs.

## License

Like upstream, licensed under the GNU LGPL 3. See [LICENSE](../LICENSE).
