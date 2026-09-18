#!/usr/bin/env python3
"""Run the architecture guards against negative fixtures.

This proves the guard scripts actually fail when a violation is introduced,
satisfying the architecture requirement that refutation tests exist.
"""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = ROOT / ".github" / "scripts"

# (script, fixture root, expect_pass)
CASES = [
    ("check_app_isolation.py", "tests/guards/isolation_good", True),
    ("check_app_isolation.py", "tests/guards/isolation_bad_infra", False),
    ("check_app_isolation.py", "tests/guards/isolation_bad_app", False),
    ("check_app_isolation.py", "tests/guards/isolation_bad_rtos", False),
    ("check_app_isolation.py", "tests/guards/isolation_bad_register", False),
    ("check_app_isolation.py", "tests/guards/isolation_bad_libc", False),
    ("check_app_transitive_includes.py", "tests/guards/transitive_good", True),
    ("check_app_transitive_includes.py", "tests/guards/transitive_bad", False),
    ("check_layer_dependencies.py", "tests/guards/layer_good", True),
    ("check_layer_dependencies.py", "tests/guards/layer_good_infra", True),
    ("check_layer_dependencies.py", "tests/guards/layer_bad", False),
    ("check_layer_dependencies.py", "tests/guards/layer_bad_app", False),
    ("check_layer_dependencies.py", "tests/guards/layer_bad_infra_soc", False),
    ("check_cmake_apps.py", "tests/guards/cmake_good", True),
    ("check_cmake_apps.py", "tests/guards/cmake_bad", False),
    ("check_event_ids.py", "tests/guards/events_good", True),
    ("check_event_ids.py", "tests/guards/events_bad", False),
    ("check_event_payload.py", "tests/guards/event_payload_good", True),
    ("check_event_payload.py", "tests/guards/event_payload_bad", False),
    ("check_no_dynamic_memory.py", "tests/guards/nm_good.txt", True),
    ("check_no_dynamic_memory.py", "tests/guards/nm_bad.txt", False),
    ("check_adr_gates.py", "tests/guards/adr_good", True),
    ("check_adr_gates.py", "tests/guards/adr_bad", False),
    ("check_module_ids.py", "tests/guards/modules_good", True),
    ("check_module_ids.py", "tests/guards/modules_bad", False),
    ("check_error_ids.py", "tests/guards/errors_good", True),
    ("check_error_ids.py", "tests/guards/errors_bad", False),
    ("check_error_ids.py", "tests/guards/errors_bad_segment", False),
]


def main() -> int:
    failed = 0
    for script, fixture, expect_pass in CASES:
        result = subprocess.run(
            [sys.executable, str(SCRIPTS / script), str(ROOT / fixture)],
            capture_output=True,
            text=True,
        )
        passed = result.returncode == 0
        ok = passed == expect_pass
        status = "ok" if ok else "UNEXPECTED"
        if not ok:
            failed += 1
        print(f"[{status:10}] {script} {fixture} (expected {'pass' if expect_pass else 'fail'})")
        if not ok:
            print(result.stdout)
            print(result.stderr)

    if failed:
        print(f"guard self-test: {failed} unexpected result(s)")
        return 1
    print(f"guard self-test: PASS ({len(CASES)} cases)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
