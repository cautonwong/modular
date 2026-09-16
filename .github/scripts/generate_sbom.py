#!/usr/bin/env python3
"""Generate a minimal CycloneDX SBOM for the firmware build inputs.

The project has no third-party runtime dependencies by design, so the SBOM
records the project component plus the toolchain/build inputs actually used.
"""
import datetime
import json
import os
import platform
import subprocess
import sys


def run(cmd):
    try:
        return subprocess.run(cmd, capture_output=True, text=True, check=False).stdout.strip()
    except OSError:
        return ""


def first_line(text):
    return text.splitlines()[0] if text else ""


def component(name, version, kind="application"):
    return {"type": kind, "name": name, "version": version or "unknown"}


def main() -> int:
    out = sys.argv[1] if len(sys.argv) > 1 else "sbom.cdx.json"
    cc = os.environ.get("CC", "cc")
    commit = run(["git", "rev-parse", "HEAD"]) or "unknown"
    components = [
        component("cmake", first_line(run(["cmake", "--version"])), "application"),
        component("c-compiler", first_line(run([cc, "--version"])), "application"),
        component("python", platform.python_version(), "application"),
    ]
    doc = {
        "bomFormat": "CycloneDX",
        "specVersion": "1.5",
        "version": 1,
        "metadata": {
            "timestamp": datetime.datetime.now(datetime.timezone.utc).strftime(
                "%Y-%m-%dT%H:%M:%SZ"
            ),
            "tools": [component("edge-modular-ci", "0.5.0", "application")],
            "component": {
                "type": "firmware",
                "name": "edge-modular",
                "version": commit,
                "description": "Foreground/background modular embedded platform",
            },
        },
        "components": components,
    }
    with open(out, "w", encoding="utf-8") as handle:
        json.dump(doc, handle, indent=2, sort_keys=True)
        handle.write("\n")
    print(f"wrote {out} ({len(components)} components)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
