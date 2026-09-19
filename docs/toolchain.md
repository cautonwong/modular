# Reproducible local environment (D38)

D38 asks for a fixed toolchain. `ci/toolchain.json` makes that executable:
`check_toolchain.py` fails when a tool that **is** installed is older than its
declared minimum, and skips tools that are absent (a host job has no ARM
toolchain, a plain checkout has no clang-tidy). The check runs in CI, in the
`static-analysis` job, where the tools are actually installed.

## The manifest

`ci/toolchain.json` is the source of truth: `minimum` is what the project still
supports, `verified` is what the ubuntu-24.04 runner ships and what the
devcontainer installs.

| Tool | Minimum | Verified (CI) | Gate it serves |
|---|---|---|---|
| gcc | 13.0 | 13.2 | host build, sanitizers, coverage |
| cmake | 3.20 | 3.28 | configure (`CMakeLists.txt` requires 3.20) |
| ninja | 1.10 | 1.11 | build |
| clang | 18.0 | 18.1 | host build, sanitizers, CodeQL |
| **clang-format** | **18.0** | **18.1** | **formatting** |
| clang-tidy | 18.0 | 18.1 | static analysis |
| cppcheck | 2.13 | 2.13 | static analysis |
| python3 | 3.10 | 3.12 | guards, generators, docs lint, tooling |
| arm-none-eabi-gcc | 10.3 | 13.2 | Cortex-M0/M4 cross build |
| qemu-system-arm | 6.2 | 8.2 | MPS2 QEMU smoke |

### Why clang-format is called out

It changes its output between major versions. Formatting a tree with 14 and
checking it with 18 reports failures that are not real -- which is exactly what
happened once in this repository. **Keep the local major version equal to CI's**;
`check_toolchain.py` fails on a locally installed clang-format below 18.

## Using the devcontainer

```bash
# one command, same tool set as the CI runner image (ubuntu-24.04)
devcontainer up --workspace-folder .
```

Or build it directly:

```bash
docker build -t modular-local .devcontainer
docker run --rm -it -v "$PWD:/work" -w /work modular-local bash
```

The container also sets `core.hooksPath` so the commit gates are active inside it.

## Local vs CI: what cannot be reproduced locally

| Gate | Locally | Why |
|---|---|---|
| host tests (ctest) | yes | needs cmocka, which the container installs |
| formatting / clang-tidy / cppcheck | yes | in the container |
| architecture guards, docs lint, commit lint | yes | python3 only |
| ARM / RISC-V cross build | yes | toolchains are in the container |
| QEMU smokes | yes | qemu-system-arm / -misc are in the container |
| Renode, HIL | **no** | needs a platform description and real hardware (#22, #30) |
| IAR/iccarm | **no** | needs a licence (#23) |
| CodeQL | **no** | runs as a GitHub action |

A local pass is therefore strong but not complete: the two rows that are missing
are exactly the ones tracked as `blocked-infra`.

## Changing a version

1. Update `ci/toolchain.json` (`verified`, and `minimum` only when support is
   genuinely dropped).
2. Update `.devcontainer/Dockerfile` if the package set changes.
3. Say why in the commit message; the check will fail on the next CI run if the
   manifest and the runner disagree.
