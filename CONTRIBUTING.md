# Contributing

Thanks for improving the modular foreground/background platform. This project is
governed by [`docs/adr.md`](docs/adr.md) (D1-D85), the single source of truth for
decisions. [`docs/adr-conformance.md`](docs/adr-conformance.md) is the only
decision-vs-implementation view. `todo.md` is retained as an archive of the early
D1-D45 subset. Changes that violate a decision will not be accepted without an
explicit ADR update.

## Ground rules

1. Keep `main` green. Never push a commit that fails the CI quality gates.
2. Respect the dependency direction: `app -> edge_module`, `product -> *`.
   Never let `app` include concrete `infra`/`board`/`sys`/other-`app` headers.
3. Runtime code must not allocate. Objects and buffers are caller-owned.
4. Events carry scalar facts only. Commands use explicit consumer-defined ports.
5. No RTOS, thread or service-locator concepts in the framework layers.

## Local workflow

For an environment that matches CI (same runner image and tool versions), use the
devcontainer or read [`docs/toolchain.md`](docs/toolchain.md):

```bash
devcontainer up --workspace-folder .
# or: docker build -t modular-local .devcontainer && docker run --rm -it -v "$PWD:/work" -w /work modular-local bash
```

`ci/toolchain.json` records the versions CI runs, and `check_toolchain.py` fails
if an installed tool is older than its declared minimum.

Enable the local pre-commit gate once per clone:

```bash
git config core.hooksPath .githooks
```

That runs, before every commit: `clang-format` on the staged C sources, the
source-level architecture guards, the guard self-test, the generator smoke test,
and the documentation format/link check; a `commit-msg` hook additionally
enforces the Conventional Commits rule below. Artifact-dependent gates (map
budget, ELF size, static stack usage, dynamic-allocation symbols) need a firmware
build and stay in CI. Missing tools are skipped with a warning rather than
silently passing, and it works offline — no `gh`, no network. It is a pre-filter,
not a replacement: CI still runs every gate over the whole tree. Bypass a single
commit with `git commit --no-verify`.

```bash
# Configure with the strict quality gates
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DEDGE_MODULE_ENABLE_WERROR=ON \
  -DEDGE_MODULE_BUILD_TESTS=ON

cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Run the guard scripts the CI runs (the pre-commit hook runs these for you):

```bash
python3 .github/scripts/check_app_isolation.py
python3 .github/scripts/check_event_ids.py
python3 .github/scripts/check_cmake_apps.py
python3 tests/guards/run_guard_selftest.py
```

Formatting and static analysis:

```bash
clang-format --dry-run --Werror $(git ls-files '*.c' '*.h')
clang-tidy -p build $(git ls-files '*.c' | grep -v '^tests/')
cppcheck --enable=warning,style,performance,portability --error-exitcode=1 \
  -I edge_module/include -I app/dlt645/include -I infra/flash/include \
  -I board/example/include -I sys/example/include \
  edge_module sys app board infra product tests
```

## Commit messages

Use Conventional Commits: `type(scope): subject`, where `type` is one of
`feat`, `fix`, `build`, `ci`, `docs`, `test`, `refactor`, `perf`, `chore`.
The subject is limited to 100 characters.

**Every commit carries a ticket reference `(#NN)` in the subject**, so `git log`
answers "which ticket is this?" without anyone writing a note by hand. The
documented whitelist is by type: `chore`, `docs` and `style` may omit it, because
a typo fix or a note does not need a ticket. Everything else must reference one.
Merge commits, GitHub-generated reverts and dependabot commits are exempt
entirely.

This is enforced by the `commit-msg` hook (`.githooks/commit-msg`) and by CI
(`.github/scripts/check_commit_messages.py`). The PR side is enforced by the
template's required `Closes #NN` line.

## Pull requests

- One logical change per PR.
- Update `CHANGELOG.md` for user-visible changes.
- Add or update tests; new behaviour must be covered by CI.
- All required checks must pass before merge.

## Branch and merge policy (single maintainer)

`main` is protected with `strict: true`: the required checks must be re-verified
on top of the current `main` before a PR can merge, and history is linear
(squash/rebase only). There is **no merge queue**, and none is planned while the
project has a single maintainer:

- A merge queue coordinates *several* queued PRs. With one person there is
  normally one open PR, so the queue would add machinery without removing wait.
- The real cost of `strict` for one maintainer is that a **stale branch** forces
  a full re-verification, and a branch based on an old `main` can contain commits
  that were already squash-merged. Such a PR shows as `dirty`, GitHub cannot build
the merge commit, and **`pull_request` workflows never start** (it looks like "CI
is not running").

So the policy is: **keep branches short-lived and rebase before opening the PR**.
`.githooks/pre-push` warns when a branch is behind `origin/main`.

Revisit this decision when the project has more than one human contributor, or
when more than one agent/PR is routinely in flight.

### CI fast path

A pull request that only touches `docs/**` or `*.md` skips the code-dependent
jobs (compile matrix, coverage, product matrix, neutrality, cross-compile, static
analysis) and runs only the change classifier and the architecture guards (docs
lint, guards, hook checks). Pushes to `main`, and any change outside
`docs/**`/`*.md`, always run the full set. The classifier is conservative by
design, and `ci-success` accepts a skipped code-dependent job so the required
check still reports.

### Emergency exit (a broken required check)

`enforce_admins: true` means even the maintainer cannot bypass the required
checks, so a broken or flaky required check blocks *all* merges. The documented
escape is a **temporary protection change**, never a force push:

1. Record the reason in the PR that is blocked (or in a new issue if none).
2. Temporarily remove the specific required check via repository settings (or the
   branch-protection API) while keeping the others required.
3. Merge the fix that unblocks the check.
4. **Re-add the check immediately** and confirm the required set matches the list
   in `CONTRIBUTING.md`.
5. File a follow-up issue if the escape was caused by a real defect rather than
   an upstream outage.

This is a **human decision**, not something automation does on its own.
