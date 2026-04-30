# mcs-qs

**mcs-qs** is a custom fork of [Quickshell](https://quickshell.org) — a flexible QtQuick-based desktop shell toolkit for Wayland. Originally based on [noctalia-qs](https://github.com/noctalia-dev/noctalia-shell).

## What is mcs-qs?

mcs-qs extends Quickshell with additional features and patches, including:

- `ext-background-effect-v1` Wayland protocol support
- Niri and DWL compositor support
- PipeWire audio spectrum analyzer
- Clipboard image support (`setClipboardImage`)
- IPC with optional arguments
- Inverted corner support for regions
- `Quickshell.Services.SysInfo` — native system monitoring (CPU, memory, temps, network, disk I/O, GPU). GPU metrics cover AMD via sysfs, NVIDIA via dlopened libnvidia-ml.so.1 (graceful fallback when absent), and Intel clock. Zero subprocess polling.
- xdg-desktop-portal Screenshot and ScreenCast backends — apps go through `org.freedesktop.impl.portal.Screenshot` and `org.freedesktop.impl.portal.ScreenCast` for native screenshots and screen-share, with the picker UI and persist-token storage owned by the shell. Drop-in replacement for `xdg-desktop-portal-wlr`.
- `Quickshell.Services.PipeWire.PwScreenCastStream` — publish a video source as a PipeWire `Video/Source` node so any consumer (browser, OBS, GStreamer) can receive frames. Used internally by the ScreenCast portal but exposed as a generic building block.
- `DBusIpcHandler` — declaratively publish QML functions, signals, and properties over a private D-Bus surface; takes the place of ad-hoc `mcs-qs ipc call` shell-outs and lets external clients drive the shell with type-safe contracts.
- Native logind (`org.freedesktop.login1`) and BlueZ pairing-agent bindings — replaces `loginctl`/`bluetoothctl` shell-outs with reactive D-Bus services.
- `Pipewire.setForceRate()` — native sample-rate override via the global PipeWire metadata, no `pw-metadata` shell-out.
- `HttpFetcher` — reusable async HTTP/JSON fetcher; replaces `curl` shell-outs for weather, holidays, and similar polled data.

The binary is named `mcs-qs` and is a drop-in replacement for `quickshell`.

## Credits

mcs-qs is built on top of **Quickshell**, developed by [outfoxxed](https://git.outfoxxed.me/outfoxxed) and contributors, and was originally forked from **noctalia-qs** by the [Noctalia](https://github.com/noctalia-dev) project.

- Quickshell website: https://quickshell.org
- Quickshell source: https://git.outfoxxed.me/quickshell/quickshell
- Quickshell mirror: https://github.com/quickshell-mirror/quickshell

All credit for the core framework goes to the Quickshell project and its contributors.

## Building

See [BUILD.md](BUILD.md) for build instructions.

```bash
cmake -GNinja -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build
sudo cmake --install build
# binary: build/src/mcs-qs
```

## License

Licensed under the GNU LGPL 3, same as the upstream Quickshell project.

Unless you explicitly state otherwise, any contribution submitted for inclusion shall be licensed as above, without any additional terms or conditions.
