# Porting status

What of upstream Quickshell works on Windows, and how it differs from Linux. Usage of the
Windows-only types is documented in their headers and in
[src/windows/module.md](../src/windows/module.md).

Status:
- **Works**: implemented and tested on Windows 11.
- **Untested**: implemented, not yet verified.
- **Windows backed**: the upstream API, implemented on top of a `Quickshell.Windows` type, so
  shared configs run unchanged.
- **Inert**: can be imported and used, but never reports anything.
- **Unavailable**: not built on Windows. Importing it fails.

Types without platform-specific code are not listed.

## Quickshell

| Type | Status | Notes |
|---|---|---|
| `PanelWindow` | Works | Placed and reserves space through the Win32 AppBar API, as the taskbar does. Reloading briefly removes and re-adds the reserved space. See [known limitations](#known-limitations) for programs running as administrator. |
| `PanelWindow.focusable` | Works | Becoming focusable while shown takes the foreground, as `Exclusive` keyboard focus does on Wayland, and hands it back to the previous window when it stops. |
| `QsWindow.mask` | Works | Only limits input, as on Wayland: what the window draws outside it still shows, and clicks there reach whatever is below. |
| `FloatingWindow` | Works | Minimizing keeps `visible` true, and setting `visible` does not restore a minimized window. Set `minimized = false` to bring it back. |
| `PopupWindow` | Works | |
| `DesktopEntries` | Works | Lists the Start menu's All apps (the shell's Apps folder): programs, Store apps, File Explorer, Settings and URL shortcuts such as Steam's games, one per name. Names are in the display language; a shortcut's file name, when it differs, is a keyword. `execute()` runs the app through Explorer. `startupClass` is the target's file name without `.exe`, so `heuristicLookup` matches window managers' app ids. Rescanned when the Start menu's folders change, not when a Store app is installed. |
| Multiple monitors, mixed DPI | Untested | |

## Quickshell.Io

| Type | Status | Notes |
|---|---|---|
| `Process` | Works | `running = false` terminates the process, as `signal()` does: console programs never see the polite close Qt sends. A running process is also ended when quickshell exits or crashes; what it starts is not. |
| `IpcHandler` | Works | Over named pipes. |
| `Socket`, `SocketServer` | Works | Over named pipes. A `path` without `\\.\pipe\` is taken as a pipe name. |
| `FileView` | Works | |

## Command line

| Command | Status | Notes |
|---|---|---|
| `qs ipc`, `qs list`, `qs kill`, `qs log` | Works | |
| `qs -d` | Works | Runs in the background. `quickshell.exe` is a console program, so starting it otherwise opens a console window. |
| Config lookup | Works | `$XDG_CONFIG_HOME\quickshell` (if set), then `%USERPROFILE%\.config\quickshell`, then `%APPDATA%\quickshell`. |

## Services

| Module | Status | Notes |
|---|---|---|
| `Quickshell.Services.UPower` | Windows backed | By `PowerManager`. Batteries are laptop batteries and `displayDevice` is all of them combined. `PowerProfiles` maps to the power mode: Best power efficiency, Balanced and Best performance. Peripheral batteries are not listed, and there are no profile holds or degradation reasons. Tested on a desktop only (no battery). |
| `Quickshell.Services.Mpris` | Windows backed | By `MediaSessions`. Track info, art (as an image url), position, length, playback state, loop, shuffle and rate; `raise` activates the app's window. Players cannot quit, go fullscreen, open uris or report their volume. Tested with a test session, not yet with real players. |
| `Quickshell.Services.Pipewire` | Unavailable | Use `Quickshell.Windows.Audio` from a file loaded only on Windows. |
| `Quickshell.Services.SystemTray` | Inert | Talks to DBus, which Windows does not have. Logs one "Could not connect to DBus" warning when first used. |
| `Quickshell.Services.Notifications` | Inert | As above. |
| `Quickshell.Bluetooth` | Inert | As above. |
| `Quickshell.Services.Pam`, `Polkit`, `Greetd` | Unavailable | |
| `Quickshell.Networking` | Unavailable | |

## Compositor integrations

| Module | Status | Notes |
|---|---|---|
| `Quickshell.Wayland` | Inert | Can be imported so shared configs load. `BackgroundEffect.blurRegion` is the exception: it sets `BlurBehind.region` with `xray`, niri's default. `WlrLayershell`, `WlrLayer`, `WlrKeyboardFocus`, `IdleInhibitor` and `IdleMonitor` exist but do nothing. Use the `Quickshell.Windows` idle types instead. |
| `Quickshell.Hyprland`, `Quickshell.I3`, `Quickshell.X11` | Unavailable | |
| `Quickshell.WindowManager` | Unavailable | Built, but there is no Windows backend for it. |

## Other

| Feature | Status | Notes |
|---|---|---|
| Crash reporter | Unavailable | |
| qmlls tooling support | Unavailable | |

## Windows only

`import Quickshell.Windows`, documented in the headers and
[src/windows/module.md](../src/windows/module.md).

| Type | Status | Notes |
|---|---|---|
| `IdleInhibitor` | Works | Keeps the display on and the system awake while `enabled`. |
| `IdleMonitor` | Works | `isIdle` after `timeout` seconds without keyboard or mouse input. |
| `Hwnd` | Works | `activate`, `close` and `processId` for a window by handle, as komorebi reports them. |
| `Monitor` | Works | `deviceName` of a screen, as window managers report them. Tested with a single monitor only. |
| `BlurBehind` | Works | Stays blurred while the window is unfocused. With `xray` it shows the blurred wallpaper. |
| `WallpaperBlur` | Works | Parameters of the xray wallpaper blur, niri's `blur` section. |
| `PowerManager` | Works | Tested on a desktop only (no battery). |
| `Audio` | Works | Hot plugging (such as Bluetooth headphones) and renaming endpoints are untested. |
| `MediaSessions` | Works | Tested with a test session, not yet with real players. |

## Planned

- Display brightness through WMI, for built-in laptop displays only.

## Not planned

- Replacing `explorer.exe`. Quickshell runs beside it.
- System tray and notifications, for now: taking them over from Explorer is not practical.
- New types for what configs do themselves through commands on Linux too (reading procfs,
  `systemctl` power actions, clipboard history, package updates). Configs do the same on
  Windows with Windows commands.

## Known limitations

- While another program running as administrator manages the taskbar (seen with yasb and
  komorebi), Explorer rejects AppBar registration from non-elevated processes. Panels are still
  placed but do not reserve space unless quickshell runs as administrator too.
