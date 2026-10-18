#!/usr/bin/env python3
"""Smoke-test the CI artifact generators (ticket #40).

The generators are not gates, so they cannot take a positive/negative fixture
pair. Instead each one is run into a temporary directory and the artifact is
asserted to be valid JSON carrying the keys the downstream pipeline reads.

Usage: run_generator_selftest.py
"""
from pathlib import Path
import json
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = ROOT / ".github" / "scripts"

# script -> (artifact name, keys the pipeline depends on)
GENERATORS = {
    "generate_build_metadata.py": ("build-metadata.json", ("project", "commit", "cc", "cmake", "python")),
    "generate_sbom.py": ("sbom.cdx.json", ("bomFormat", "specVersion", "metadata", "components")),
}


def main() -> int:
    failed = 0
    with tempfile.TemporaryDirectory() as tmp:
        for script, (name, keys) in GENERATORS.items():
            out = Path(tmp) / name
            result = subprocess.run(
                [sys.executable, str(SCRIPTS / script), str(out)],
                capture_output=True,
                text=True,
                cwd=ROOT,
                env={**os.environ, "CC": os.environ.get("CC", "cc")},
            )
            if result.returncode != 0:
                print(f"[FAIL      ] {script}: exit {result.returncode}: {result.stderr.strip()[:200]}")
                failed += 1
                continue
            try:
                document = json.loads(out.read_text(encoding="utf-8"))
            except (OSError, ValueError) as exc:
                print(f"[FAIL      ] {script}: artifact is not valid JSON: {exc}")
                failed += 1
                continue
            missing = [key for key in keys if key not in document]
            if missing:
                print(f"[FAIL      ] {script}: missing keys {missing}")
                failed += 1
            else:
                print(f"[ok        ] {script} -> {name}")

        # Test tools/scaffold.py generator (all 5 layers: app, infra, soc, board, product)
        scaffold_script = ROOT / "tools" / "scaffold.py"
        if scaffold_script.is_file():
            res_app = subprocess.run(
                [sys.executable, str(scaffold_script), "app", "smoke_app", "--root", str(tmp)],
                capture_output=True,
                text=True,
                cwd=ROOT,
            )
            res_infra = subprocess.run(
                [sys.executable, str(scaffold_script), "infra", "smoke_infra", "--root", str(tmp)],
                capture_output=True,
                text=True,
                cwd=ROOT,
            )
            res_soc = subprocess.run(
                [sys.executable, str(scaffold_script), "soc", "smoke_soc", "--root", str(tmp)],
                capture_output=True,
                text=True,
                cwd=ROOT,
            )
            res_board = subprocess.run(
                [sys.executable, str(scaffold_script), "board", "smoke_board", "--soc", "smoke_soc", "--root", str(tmp)],
                capture_output=True,
                text=True,
                cwd=ROOT,
            )
            res_prod = subprocess.run(
                [sys.executable, str(scaffold_script), "product", "smoke_prod", "--board", "smoke_board", "--family", "example", "--app", "smoke_app", "--root", str(tmp)],
                capture_output=True,
                text=True,
                cwd=ROOT,
            )
            purity = subprocess.run(
                [sys.executable, str(SCRIPTS / "check_app_purity.py"), str(tmp)],
                capture_output=True,
                text=True,
            )
            ports = subprocess.run(
                [sys.executable, str(SCRIPTS / "check_consumer_ports.py"), str(tmp)],
                capture_output=True,
                text=True,
            )
            invariants = subprocess.run(
                [sys.executable, str(SCRIPTS / "check_aggregate_invariants.py"), str(tmp)],
                capture_output=True,
                text=True,
            )
            wiring = subprocess.run(
                [sys.executable, str(SCRIPTS / "check_composition_root_wiring.py"), str(tmp)],
                capture_output=True,
                text=True,
            )
            if (res_app.returncode == 0 and res_infra.returncode == 0 and
                res_soc.returncode == 0 and res_board.returncode == 0 and res_prod.returncode == 0 and
                purity.returncode == 0 and ports.returncode == 0 and invariants.returncode == 0 and wiring.returncode == 0):
                print(f"[ok        ] scaffold.py -> app, infra, soc, board, product pass all architecture guard rules")
            else:
                print(f"[FAIL      ] scaffold.py self-test failed")
                failed += 1

    if failed:
        print(f"generator self-test: {failed} failure(s)")
        return 1
    print(f"generator self-test: PASS (generators + scaffold)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
