name = "Quickshell.Windows"
description = "Windows specific Quickshell types"
headers = [
	"idle.hpp",
	"hwnd.hpp",
	"blur_behind.hpp",
	"wallpaper_blur.hpp",
	"power.hpp",
	"audio.hpp",
	"media.hpp",
]
-----
Types that give Windows semantics to what the Linux modules do through Wayland, DBus or
PipeWire. Only available in Windows builds of this fork.

## Sharing a config with Linux

Most of a config written for Linux loads unchanged:
- @@Quickshell.PanelWindow reserves screen space through the Win32 AppBar API, the way the
  taskbar does.
- @@Quickshell.Services.UPower and @@Quickshell.Services.Mpris are backed by @@PowerManager
  and @@MediaSessions.
- @@Quickshell.Wayland can be imported, but only `BackgroundEffect.blurRegion` does anything:
  it sets @@BlurBehind.region with @@BlurBehind.xray.

Where Windows has no equivalent of an upstream module, such as PipeWire, use the types here
from a file that is only loaded on Windows, since importing `Quickshell.Windows` fails on Linux.
@@Audio is the usual case.

## Blur

@@BlurBehind blurs what is behind part of any window, as an attached object, and the blur stays
while the window is unfocused, unlike Mica and Acrylic.

With @@BlurBehind.xray the window shows the desktop wallpaper blurred instead, as niri's xray
does. The wallpaper is blurred once per monitor with the parameters in @@WallpaperBlur, so
every xray window shows the same image and windows that meet show no seam.

```qml
PanelWindow {
  id: root
  color: "#80000000"

  BlurBehind.region: Region { item: root.contentItem }
  BlurBehind.xray: true
}
```

Both turn solid while Transparency effects is off or battery or energy saver is on, like
Windows' own blur.

## Window managers

Tiling window managers such as komorebi report windows by handle (HWND). @@Hwnd takes those
handles as numbers to activate or close a window, or find its process.

Windows only lets a process bring a window to the foreground if it received the last input
event, so @@Hwnd.activate() and @@MediaSession.activate() work from a click handler and may
return false otherwise.
