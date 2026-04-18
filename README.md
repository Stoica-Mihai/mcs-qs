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
