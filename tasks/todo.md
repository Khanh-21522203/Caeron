- [x] Refresh command module file map and project notes.
- [x] Review command flyweight API and wire-layout correctness.
- [x] Review CNC producer/consumer call sites using command flyweights.
- [x] Review unit-test coverage quality and blind spots.
- [x] Run relevant verification.
- [x] Document consolidated review findings.

## Review

Verification:

`cmake --build build --target caeron_test_command caeron_test_cnc -j2` passes.
`ctest --test-dir build -R 'caeron_test_(command|cnc)' --output-on-failure` passes.
Sanitizer build in `/tmp/caeron-ubsan` rebuilds successfully.
`env ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ctest --test-dir /tmp/caeron-ubsan -R 'caeron_test_(command|cnc)' --output-on-failure` now passes.

Current findings:

1. Medium: Fixed-width scalar flyweight getters/setters remain unchecked wrappers around `UnsafeBuffer`; invalid offsets assert in debug and are undefined behavior in release. Some files document this contract, but the API remains sharp.
2. Low: Public raw length setters remain footguns despite comments because normal callers can create malformed embedded lengths.
3. Low: Sanitizer coverage is not integrated into the normal test target/CI path; today it passes, but it remains easy to miss future UB regressions.
4. Low: The tests are comprehensive for recent offset and length bugs, but they still rely on a separately-created sanitizer build outside the project test definitions.

Resolved since the prior review:

1. Negative flyweight offsets now have explicit tests and guards for variable-length APIs.
2. `PublicationErrorFrameFlyweight::address()` now checks the full 16-byte address field.
3. `ClientCommandAdapter` now preserves `client_id` as `i64` in its handler contract and dispatch.
4. Variable-length setter guards now use subtraction-first checks and pass the current UBSan halt-mode overflow-offset tests.
5. Counter, static counter, and image-buffer derived-offset helpers now guard embedded lengths before computing derived offsets.
