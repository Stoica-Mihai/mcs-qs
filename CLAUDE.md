# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

mcs-qs is a custom fork of [Quickshell](https://quickshell.org) — a QtQuick-based desktop shell toolkit for Wayland compositors. It extends upstream Quickshell with: `ext-background-effect-v1` protocol support, Niri/DWL compositor support, PipeWire audio spectrum analyzer, clipboard image support, IPC with arguments, and inverted corner support for regions. The binary is named `mcs-qs`.

## Build Commands

```bash
# Configure (debug by default, creates compile_commands.json symlink)
just configure                          # debug build
just configure release                  # release build
just configure debug -DNO_PCH=ON -DBUILD_TESTING=ON  # for linting/testing

# Build (auto-configures if needed)
just build

# Run
just run [args]

# Test (requires -DBUILD_TESTING=ON at configure time)
just test

# Format
just fmt

# Lint (requires -DNO_PCH=ON -DBUILD_TESTING=ON and TIDYFOX env var)
just lint-changed    # lint files changed vs HEAD
just lint-staged     # lint staged files only

# Clean
just clean
```

Direct cmake equivalent: `cmake -GNinja -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build`

## Architecture

C++20/Qt6/QML project built with CMake + Ninja. The `src/` tree is organized into modules:

- **launch/** — Entry point and CLI argument handling
- **core/** — QML types, engine context, lazy loading, desktop entries, icon/image providers, logging, clock, and other foundational components
- **window/** — Window management primitives (floating windows, panel windows)
- **wayland/** — Wayland protocol integrations, each in its own subdirectory:
  - `wlr_layershell/` (bars/overlays/backgrounds), `session_lock/`, `toplevel_management/`, `screencopy/`, `background_effect/`, `hyprland/`, `niri/`, `dwl/`, `idle_inhibit/`, `idle_notify/`, `shortcuts_inhibit/`, `gamma_control/`, `data_control/`
- **x11/** — X11 panel window support via libxcb
- **services/** — D-Bus service integrations: `pipewire/`, `mpris/`, `status_notifier/` (system tray), `upower/`, `notifications/`, `pam/`, `polkit/`, `greetd/`
- **bluetooth/** — BlueZ D-Bus integration
- **network/** — Network management
- **io/** — File I/O, sockets, process management
- **ipc/** — Inter-process communication
- **dbus/** — D-Bus utilities shared across services
- **widgets/** — Custom QML widgets (ClippingRectangle, etc.)
- **ui/** — UI components
- **windowmanager/** — Generic window manager interface (ext-workspace protocol)
- **crash/** — Crash handler using cpptrace
- **build/** — Build metadata (version info template)
- **debug/** — Debug info utilities

Features are toggled via CMake options (e.g., `-DWAYLAND=OFF`, `-DSERVICE_PIPEWIRE=OFF`). See BUILD.md for the full list.

## Code Style

- C++20 with Qt6 (minimum 6.6). Use `auto` when type is deducible from context.
- Tabs for indentation, 100-column limit. Formatting enforced by `.clang-format`.
- Use lowercase Qt headers: `<qwindow.h>` not `<QWindow>`.
- Class member ordering: Q_OBJECT macros, Q_PROPERTY, public (constructors, static instance(), member functions, Q_INVOKABLEs, then property accessors grouped as bindable/getter/setter), signals, slots, private.
- Property-backing members prefixed with `m`, bindables prefixed with `b`.
- Doc comments use `///` (body) and `//!` (summary). Reference other types with `@@[Module.][Type.][member]`.
- Commit messages: `scope: description` (e.g., `core: fix lazy loader race`, `service/mpris: add track position`).
- New doc-commented files must be registered in the module's `module.md`.
- User-visible changes go in `changelog/next.md`.

## Nix

A Nix flake provides the build environment. Use `nix develop` or nix-direnv for a shell with all dependencies. `nix build` produces the package.
