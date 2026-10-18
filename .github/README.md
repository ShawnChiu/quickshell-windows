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
- `DesktopEntries` lists the Start menu's apps, as its All apps list does (the shell's Apps
  folder): programs, Store (UWP) apps, File Explorer and Settings, and URL shortcuts such as
  Steam's games, one per name. Names are in the display language; a shortcut's file name, when
  it differs, is a keyword. `execute()` runs the app through Explorer, and `startupClass` is the
  target's file name without `.exe`, so `heuristicLookup` matches window managers' app ids. The
  list is rescanned when the Start menu's folders change, not when a Store app is installed.
- A window's `mask` only limits input, as on Wayland: what the window draws outside it still
  shows (`QWindow::setMask` would clip it on Windows), and clicks there reach whatever is below.
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
  is on. With `xray` set it shows the wallpaper blurred instead, as niri's xray does: every xray
  window shows the same blur, so windows that meet show no seam, and windows in between don't
  show through.
- `WallpaperBlur`: the parameters of that blurred wallpaper, niri's `blur` section (`passes`,
  `offset`, `saturation`, `noise`, niri's defaults). The wallpaper is blurred once per monitor
  with niri's dual Kawase passes, and again when it or the parameters change.
- `PowerManager`: power supply, energy saver, batteries (`Battery`, capacities in mWh) and the
  power mode (Settings > System > Power), which can also be set.
- `Audio`: the outputs and inputs (`AudioEndpoint`) with their name, form factor, state, volume
  and mute, and the default output and input. Volume and mute can be set on active endpoints,
  and setting `defaultOutput` or `defaultInput` changes the default for all roles, as Settings
  does. There is no PipeWire on Windows, so shared configs load a small file per platform.
- `MediaSessions`: the media sessions Windows shows in its media flyout (`MediaSession`), with
  track info, thumbnail, playback status, timeline and controls, and the current session. The
  position is estimated between the app's reports. `activate` brings the app's window to the
  front.

Upstream services backed by Windows:
- `Quickshell.Services.UPower` reports `PowerManager`'s batteries as laptop batteries
  (`displayDevice` is all of them combined), and `PowerProfiles` maps to the power mode: Best
  power efficiency, Balanced and Best performance. Peripheral batteries are not listed, and there
  are no profile holds or degradation reasons. Tested on a desktop only (no battery).
- `Quickshell.Services.Mpris` lists `MediaSessions` as players: track info, art (as an image
  url), position, length, playback state, loop, shuffle and rate, and `raise` activates the
  app's window. Players cannot quit, go fullscreen, open uris or report their volume.

Built but inert (they talk to DBus, and Windows has no session bus): `Quickshell.Bluetooth`,
`Quickshell.Services.SystemTray` and `Notifications` load and stay empty, the
same as on Linux with the service not running. Each logs one "Could not connect to DBus" warning
when first used.

Not available on Windows (disabled at build time):
- Wayland, Hyprland, X11 and i3 integrations. `Quickshell.Wayland` can still be imported so
  shared configs load: `BackgroundEffect.blurRegion` sets `BlurBehind.region` with `xray`
  (niri's default), while
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
