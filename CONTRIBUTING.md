# Contributing

Thanks for improving the modular foreground/background platform. This project is
governed by the architecture decisions in [`todo.md`](todo.md) (frozen D1-D45) and
[`docs/adr.md`](docs/adr.md) (extended decision record). Changes that violate a
frozen decision will not be accepted without an explicit ADR update.

## Ground rules

1. Keep `main` green. Never push a commit that fails the CI quality gates.
2. Respect the dependency direction: `app -> edge_module`, `product -> *`.
   Never let `app` include concrete `infra`/`board`/`sys`/other-`app` headers.
3. Runtime code must not allocate. Objects and buffers are caller-owned.
4. Events carry scalar facts only. Commands use explicit consumer-defined ports.
5. No RTOS, thread or service-locator concepts in the framework layers.

## Local workflow

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

Run the same guard scripts the CI runs:

```bash
python3 .github/scripts/check_app_isolation.py
python3 .github/scripts/check_event_ids.py
python3 .github/scripts/check_cmake_apps.py
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

## Pull requests

- One logical change per PR.
- Update `CHANGELOG.md` for user-visible changes.
- Add or update tests; new behaviour must be covered by CI.
- All required checks must pass before merge.
