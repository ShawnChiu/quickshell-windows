# Quickshell for Windows

An **unofficial** port of [Quickshell](https://quickshell.outfoxxed.me) to Windows 11.

This is a downstream fork that tracks upstream and keeps the Windows support as a small set of
commits on the `windows` branch. `master` mirrors upstream.

The branch is never force pushed, so `git pull` keeps a clone up to date. Each upstream sync
starts with a `chore: start merging-rebase onto upstream ...` merge commit, and the commits after
the latest one are the current patches.

- Upstream: https://git.outfoxxed.me/quickshell/quickshell
  (mirror: https://github.com/quickshell-mirror/quickshell)
- Upstream's own readme: [README.md](../README.md)

> [!IMPORTANT]
> Please report Windows-specific problems here, not upstream. Upstream does not support Windows.

## Status

Early work in progress.

Tested:
- Loading QML shells, hot reload
- `PanelWindow` placement and reserving screen space through the Win32 AppBar API
- `Process`, `IpcHandler`. Stopping a process (`running = false`) terminates it, as `signal()`
  does: console programs never see the polite close Qt sends. A running process is also ended
  when quickshell exits or crashes; what it starts is not.
- `DesktopEntries` lists the Start menu's shortcuts (`.lnk`, the user's over everyone's, one per
  name). `execute()` opens the shortcut through Explorer, and `startupClass` is the target's
  file name without `.exe`, so `heuristicLookup` matches window managers' app ids. Store (UWP)
  apps have no shortcut there and are not listed.
- A `PanelWindow` that becomes `focusable` while shown takes the foreground, as `Exclusive`
  keyboard focus does on Wayland, and hands it back to the previous window when it stops.
- `SocketServer` and `Socket` over named pipes (a `path` without `\\.\pipe\` is a pipe name)
- `qs ipc`, `qs list`, `qs kill`, `qs -d` (daemonize)

Implemented but not yet verified:
- Multiple monitors and mixed DPI
- `FloatingWindow`, `PopupWindow`, `FileView`

Windows specific (`import Quickshell.Windows`):
- `IdleInhibitor`: keeps the display on and the system awake while `enabled`.
- `IdleMonitor`: `isIdle` after `timeout` seconds without keyboard or mouse input.
- `Hwnd`: `activate`, `close` and `processId` for a window by handle, as window managers such as
  komorebi report them.
- `BlurBehind.region`: blurs what is behind part of a window, and stays blurred while the window
  is unfocused, unlike Mica and Acrylic. `radius` and `saturation` default to Acrylic's. Like
  Windows' own blur it turns solid while Transparency effects is off or battery or energy saver
  is on.
- `PowerManager`: power supply, energy saver, batteries (`Battery`, capacities in mWh) and the
  power mode (Settings > System > Power), which can also be set.

Upstream services backed by Windows:
- `Quickshell.Services.UPower` reports `PowerManager`'s batteries as laptop batteries
  (`displayDevice` is all of them combined), and `PowerProfiles` maps to the power mode: Best
  power efficiency, Balanced and Best performance. Peripheral batteries are not listed, and there
  are no profile holds or degradation reasons. Tested on a desktop only (no battery).

Built but inert (they talk to DBus, and Windows has no session bus): `Quickshell.Bluetooth`,
`Quickshell.Services.Mpris`, `SystemTray` and `Notifications` load and stay empty, the
same as on Linux with the service not running. Each logs one "Could not connect to DBus" warning
when first used.

Not available on Windows (disabled at build time):
- Wayland, Hyprland, X11 and i3 integrations. `Quickshell.Wayland` can still be imported so
  shared configs load: `BackgroundEffect.blurRegion` sets `BlurBehind.region`, while
  `WlrLayershell`, `WlrLayer`, `WlrKeyboardFocus`, `IdleInhibitor` and `IdleMonitor` exist but
  do nothing.
- Services: PipeWire, PAM, Polkit, greetd
- Network
- Crash reporter, qmlls tooling support

Known limitations:
- `quickshell.exe` is a console program, so starting it directly opens a console window.
  Use `qs -d` to run it in the background.
- Reloading briefly removes and re-adds a panel's reserved space.
- While another program running as administrator manages the taskbar (seen with yasb and
  komorebi), Explorer rejects AppBar registration from non-elevated processes. Panels are still
  placed but do not reserve space unless quickshell runs as administrator too.

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

Configs are searched for in `$XDG_CONFIG_HOME\quickshell` (if set), then
`%USERPROFILE%\.config\quickshell`, then `%APPDATA%\quickshell`. The first one containing a config
wins, the same way `~/.config/quickshell` works on Linux. See the upstream
[documentation](https://quickshell.outfoxxed.me/docs/) for writing configs.

## License

Like upstream, licensed under the GNU LGPL 3. See [LICENSE](../LICENSE).
