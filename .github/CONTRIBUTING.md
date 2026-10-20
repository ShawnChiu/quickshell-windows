# Contributing to Quickshell for Windows

This is an unofficial, downstream fork. The rules here apply to this fork only. Upstream
Quickshell has its own policy in [CONTRIBUTING.md](../CONTRIBUTING.md), which applies to upstream
and not to this repository.

## This fork and upstream

**Never report problems with this fork to upstream Quickshell.** Open an issue here instead. The
exception is a problem that also happens with the same config on Linux: that one belongs
upstream, reported under upstream's rules.

This fork does not forward contributions upstream. If a change Windows needs also fixes upstream
Quickshell, consider submitting it there yourself, under upstream's rules. Until upstream has it,
it can be carried here as a patch, and it is dropped once upstream merges an equivalent fix.

## Scope

This fork exists to make upstream Quickshell run on Windows, while keeping its changes to
upstream as small as possible so it can follow every upstream update. A change belongs here if
Quickshell needs it to build, run or be useful on Windows:

- platform backends (panels, AppBar, input, IPC over named pipes, config lookup)
- `Quickshell.Windows` types for things QML cannot reach on its own (window handles, monitors,
  idle, blur, power, audio, media)
- adapters from upstream modules to `Quickshell.Windows` types, when the two map closely and
  gaps can be reported honestly (for example through `can*` properties)
- inert stubs of Linux-only modules, so imports in shared configs resolve. Stubs do nothing;
  they never pretend to have Wayland or DBus behavior.
- building, packaging and installing on Windows
- the port's documentation

Things that do not belong here:

- Anything a config can do in QML on its own. This includes **window manager integrations**
  (komorebi, GlazeWM, ...): they already expose IPC, which a config can reach with `Socket` and
  `Process`. Publish them as a separate QML module.
- New types for what configs already do through commands on Linux (reading system info, power
  actions, clipboard history, package updates). Configs do the same on Windows with Windows
  commands.
- Changes that are not Windows specific and that Windows does not need. Those belong upstream,
  under upstream's rules.

[PORTING.md](PORTING.md) lists what is planned and what is not.

## Code contributions

Most of the Windows code in this fork was written with AI assistance. The maintainer reviews,
tests and is responsible for every change that lands on the `windows` branch. AI-assisted
contributions are accepted too. Whatever tools you used:

- **Test it on Windows** and say what you tested on (Windows version, single or multiple
  monitors, DPI scaling, window manager if relevant). Untested changes may still be merged, but
  are marked "Untested" in [PORTING.md](PORTING.md). CI builds on Linux only.
- **You are responsible for it.** Be ready to answer questions about what the change does and
  why. The pull request description may be written with AI, but you must have checked that it
  is accurate. Pull requests opened by agents with no human following up will be closed.
- **Keep the changes to upstream small.** Prefer new files under `src/windows/` over edits to
  upstream files. When an upstream file has to change, keep the edit small and guard it with
  `Q_OS_WIN` or CMake conditions so the Linux build is untouched.
- **Update [PORTING.md](PORTING.md)** when a type's status changes, and document new
  `Quickshell.Windows` types in their header and in
  [src/windows/module.md](../src/windows/module.md).
- Follow upstream's [HACKING.md](../HACKING.md) for code style (`clang-format`, `clang-tidy`).
- One change per pull request.

### Branches and commits

Open pull requests against `windows`. `master` mirrors upstream and never takes changes.

`windows` only moves forward, so a fork of it can follow it with a normal pull or merge. When
upstream is synced, the Windows commits are replayed onto the new upstream, and merged pull
requests may be squashed or reworded on the way.

Commit messages follow [Conventional Commits](https://www.conventionalcommits.org), for example
`fix(windows): keep window masks from clipping what windows draw`. Keep the subject short, and
add a brief body only when the subject is not enough. Do not include merge commits in a pull
request.

## Bug reports

Include:

- Windows version and build
- how you installed or built this fork, and the commit
- your monitor setup (count, resolution, scaling)
- the window manager or bar tools running alongside (komorebi, yasb, ...), and whether any of
  them run as administrator (see [known limitations](PORTING.md#known-limitations))
- whether the same config works on Linux, if you can check
- a minimal `shell.qml` that reproduces the problem, and the output of `qs log`

The crash reporter is not available on Windows. For a crash, include the "Application Error"
entry for `quickshell.exe` from Event Viewer (Windows Logs, Application), which names the
module and offset that crashed.

## License

Like upstream, this fork is licensed under the GNU LGPL 3. Contributions are accepted under the
same license. Code taken from other projects, including samples from Microsoft's documentation,
must name its source and have a compatible license.
