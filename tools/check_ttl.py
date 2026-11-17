#!/usr/bin/env python3
"""Check that the committed LV2 TTL files match the metadata generator."""
from __future__ import annotations

import pathlib
import subprocess
import sys


def main() -> int:
    if len(sys.argv) < 3:
        print("usage: check_ttl.py <ttl-generator> <repo-root>", file=sys.stderr)
        return 2
    generator = pathlib.Path(sys.argv[1])
    root = pathlib.Path(sys.argv[2])
    output = subprocess.check_output([str(generator)], text=True)

    plugin_marker = "# ---- plugin ----"
    preset_marker = "# ---- presets ----"
    try:
        plugin_at = output.index(plugin_marker)
        preset_at = output.index(preset_marker)
        manifest = output[:plugin_at].rstrip() + "\n"
        common = output[:output.index("<https://github.com/tylorismusic-commits/repair-suite>\n")].rstrip() + "\n\n"
        plugin = common + output[plugin_at:preset_at].rstrip() + "\n"
        presets = common + output[preset_at:].rstrip() + "\n"
    except ValueError as exc:
        print(f"generator output is malformed: {exc}", file=sys.stderr)
        return 1

    expected = {
        "manifest.ttl": manifest,
        "repair_suite.ttl": plugin,
        "presets.ttl": presets,
    }
    failed = False
    for name, text in expected.items():
        path = root / "lv2" / name
        current = path.read_text() if path.exists() else ""
        if current != text:
            print(f"stale TTL: {path}", file=sys.stderr)
            failed = True
    if not failed:
        print("LV2 metadata is in sync")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
