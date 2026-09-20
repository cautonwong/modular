#!/usr/bin/env python3
"""Run the architecture guards against negative fixtures.

This proves the guard scripts actually fail when a violation is introduced,
satisfying the architecture requirement that refutation tests exist.
"""
from pathlib import Path
import os
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
    ("check_stack_usage.py", "tests/guards/stack_good.su", True),
    ("check_stack_usage.py", "tests/guards/stack_bad.su", False),
    ("check_module_ids.py", "tests/guards/modules_good", True),
    ("check_module_ids.py", "tests/guards/modules_bad", False),
    ("check_module_ids.py", "tests/guards/modules_bad_block", False),
    ("check_error_ids.py", "tests/guards/errors_good", True),
    ("check_error_ids.py", "tests/guards/errors_bad", False),
    ("check_error_ids.py", "tests/guards/errors_bad_segment", False),
    ("check_product_board_binding.py", "tests/guards/binding_good", True),
    ("check_product_board_binding.py", "tests/guards/binding_bad_duplicate", False),
    ("check_product_board_binding.py", "tests/guards/binding_bad_illegal_pair", False),
    ("check_product_board_binding.py", "tests/guards/binding_bad_orphan_fw", False),
    ("check_commit_messages.py", "tests/guards/commits_good.txt", True),
    ("check_commit_messages.py", "tests/guards/commits_bad.txt", False),
    ("check_docs.py", "tests/guards/docs_good", True),
    ("check_docs.py", "tests/guards/docs_bad", False),
    ("check_module_contract.py", "tests/guards/module_contract_good", True),
    ("check_module_contract.py", "tests/guards/module_contract_bad", False),
    ("check_toolchain.py", "tests/guards/toolchain_ok", True),
    ("check_toolchain.py", "tests/guards/toolchain_bad", False),
    # Gate fixtures that need arguments or a tool shim: (script, fixture, expect, args, env)
    (
        "check_size.py",
        "tests/guards/fake_size.sh",
        True,
        ["1024", "512"],
        {"SIZE_TOOL": "{root}/tests/guards/fake_size.sh"},
    ),
    (
        "check_size.py",
        "tests/guards/fake_size.sh",
        False,
        ["100", "50"],
        {"SIZE_TOOL": "{root}/tests/guards/fake_size.sh"},
    ),
    (
        "check_map_budget.py",
        "tests/guards/map_budget_good.txt",
        True,
        ["--config", "{root}/tests/guards/map_budget.json"],
    ),
    (
        "check_map_budget.py",
        "tests/guards/map_budget_bad.txt",
        False,
        ["--config", "{root}/tests/guards/map_budget.json"],
    ),
    ("check_guard_coverage.py", ".", True),
    ("check_guard_coverage.py", "tests/guards/coverage_bad", False),
    ("check_pal_arch_binding.py", ".", True),
    ("check_pal_arch_binding.py", "tests/guards/pal_binding_bad", False),
    ("check_area_registration.py", "tests/guards/area_registration_good", True),
    ("check_area_registration.py", "tests/guards/area_registration_bad", False),
]


def main() -> int:
    failed = 0
    for case in CASES:
        script, fixture, expect_pass = case[0], case[1], case[2]
        extra = [str(a) for a in case[3]] if len(case) > 3 else []
        env_extra = dict(case[4]) if len(case) > 4 else {}
        args = [a.replace("{root}", str(ROOT)) for a in extra]
        env = {**os.environ, **{k: str(v).replace("{root}", str(ROOT)) for k, v in env_extra.items()}}
        result = subprocess.run(
            [sys.executable, str(SCRIPTS / script), str(ROOT / fixture), *args],
            capture_output=True,
            text=True,
            env=env,
        )
        passed = result.returncode == 0
        ok = passed == expect_pass
        status = "ok" if ok else "UNEXPECTED"
        if not ok:
            failed += 1
        label = f"{script} {fixture}" + (f" {' '.join(extra)}" if extra else "")
        print(f"[{status:10}] {label} (expected {'pass' if expect_pass else 'fail'})")
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
