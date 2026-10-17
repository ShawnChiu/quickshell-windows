# Quickshell for Windows

An **unofficial** port of [Quickshell](https://quickshell.outfoxxed.me) to Windows 11.

This is a downstream fork that tracks upstream and keeps the Windows support as a small set of
commits on the `windows` branch, rebased onto each upstream update. `master` mirrors upstream.

- Upstream: https://git.outfoxxed.me/quickshell/quickshell
  (mirror: https://github.com/quickshell-mirror/quickshell)
- Upstream's own readme: [README.md](../README.md)

> [!IMPORTANT]
> Please report Windows-specific problems here, not upstream. Upstream does not support Windows.

## Status

Early work in progress.

Tested:
- Loading QML shells, hot reload
- `PanelWindow` placement
- `Process`, `IpcHandler`
- `qs ipc`, `qs list`, `qs kill`, `qs -d` (daemonize)

Implemented but not yet verified:
- `PanelWindow` reserving screen space through the Win32 AppBar API
- `FloatingWindow`, `PopupWindow`, `FileView`, `Socket`

Not available on Windows (disabled at build time):
- Wayland, Hyprland, X11 and i3 integrations
- Services: PipeWire, MPRIS, UPower, system tray, notifications, PAM, Polkit, greetd
- Bluetooth, Network
- Crash reporter, qmlls tooling support
- `DesktopEntries` (always empty)

Known limitations:
- `quickshell.exe` is a console program, so starting it directly opens a console window.
  Use `qs -d` to run it in the background.
- Reloading briefly removes and re-adds a panel's reserved space.

## Building

Requirements: Visual Studio 2022 (Desktop C++ workload), CMake, Ninja, and Qt 6.8+ for
`msvc2022_64` with the Qt Shader Tools module. Qt can be installed with
[aqtinstall](https://github.com/miurahr/aqtinstall):

```powershell
winget install miurahr.aqtinstall
aqt install-qt windows desktop 6.10.3 win64_msvc2022_64 -m qtshadertools -O C:\Qt
```

From an "x64 Native Tools" prompt:

```powershell
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo `
	-DCMAKE_PREFIX_PATH=C:/Qt/6.10.3/msvc2022_64 -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build build
```

Qt's `bin` directory has to be on `PATH` to run the result.

## Configuration

Configs are found in `%APPDATA%\quickshell` (or `$XDG_CONFIG_HOME/quickshell` if set), the same way
`~/.config/quickshell` works on Linux. See the upstream
[documentation](https://quickshell.outfoxxed.me/docs/) for writing configs.

## License

Like upstream, licensed under the GNU LGPL 3. See [LICENSE](../LICENSE).
