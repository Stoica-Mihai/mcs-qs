#!/usr/bin/env python3
"""
Minimal ScreenCast portal test driver.

Holds a single bus connection across CreateSession → SelectSources →
(optional Start) → Close so the per-call caller-disconnect cleanup
doesn't tear the session down between requests.

Usage:
    ./tools/test-screencast.py [--start]

--start  Also call Start after SelectSources (verifies the full
         CreateSession→SelectSources→Start path).
"""

import argparse
import sys
import time
from gi.repository import Gio, GLib

DEST = "org.freedesktop.impl.portal.desktop.mcshell"
ROOT = "/org/freedesktop/portal/desktop"
IFACE = "org.freedesktop.impl.portal.ScreenCast"
SESSION_IFACE = "org.freedesktop.impl.portal.Session"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--start", action="store_true", help="also call Start")
    ap.add_argument("--hold", type=int, default=0,
                    help="seconds to keep the session open after Start")
    ap.add_argument("--multi", action="store_true",
                    help="ask for multi-source selection")
    ap.add_argument("--persist", type=int, default=0, choices=(0, 1, 2),
                    help="persist_mode: 0=no, 1=running, 2=permanent")
    ap.add_argument("--restore-token", default="",
                    help="present a known token to skip the picker")
    args = ap.parse_args()

    bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)

    # CreateSession
    handle = "/test/req1"
    session_handle = "/test/sess1"
    print(f"→ CreateSession (handle={handle}, session={session_handle})")
    res = bus.call_sync(
        DEST, ROOT, IFACE, "CreateSession",
        GLib.Variant("(oosa{sv})", (handle, session_handle, "test.app", {})),
        GLib.VariantType("(ua{sv})"), Gio.DBusCallFlags.NONE, 30000, None,
    )
    code, _ = res.unpack()
    print(f"  ← response={code}")
    if code != 0:
        return 1

    # SelectSources — this is the one that delays reply on the picker
    print(f"→ SelectSources (types=Monitor, multiple={args.multi})")
    options = {
        "types": GLib.Variant("u", 1),         # Monitor
        "multiple": GLib.Variant("b", args.multi),
        "cursor_mode": GLib.Variant("u", 2),   # Embedded
        "persist_mode": GLib.Variant("u", args.persist),
    }
    if args.restore_token:
        options["restore_token"] = GLib.Variant("s", args.restore_token)
    res = bus.call_sync(
        DEST, ROOT, IFACE, "SelectSources",
        GLib.Variant("(oosa{sv})", (handle, session_handle, "test.app", options)),
        GLib.VariantType("(ua{sv})"), Gio.DBusCallFlags.NONE, 30000, None,
    )
    code, _ = res.unpack()
    print(f"  ← response={code}")
    if code != 0:
        return 2

    if args.start:
        print("→ Start")
        res = bus.call_sync(
            DEST, ROOT, IFACE, "Start",
            GLib.Variant("(oossa{sv})", (handle, session_handle, "test.app", "", {})),
            GLib.VariantType("(ua{sv})"), Gio.DBusCallFlags.NONE, 30000, None,
        )
        code, results = res.unpack()
        print(f"  ← response={code} results={results}")

    if args.hold > 0:
        print(f"… holding session open for {args.hold}s")
        time.sleep(args.hold)

    # Close the session cleanly.
    print("→ Session.Close")
    bus.call_sync(
        DEST, session_handle, SESSION_IFACE, "Close",
        None, None, Gio.DBusCallFlags.NONE, 5000, None,
    )
    print("  ← closed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
