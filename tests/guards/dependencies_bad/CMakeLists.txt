# Fixture for check_dependencies.py: the build pins a DIFFERENT commit than the
# manifest records, which is the drift the checker exists to catch.
set(EDGE_MODULE_FIXTURE_COMMIT "deadbeefdeadbeefdeadbeefdeadbeefdeadbeef"
    CACHE STRING "fixture")
