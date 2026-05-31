- [x] Refresh current protocol state.
- [x] Inspect protocol implementation and unit tests.
- [x] Run protocol build/tests and header checks.
- [x] Document consolidated review findings.

## Review

Verification:

`cmake --build build --target caeron_test_protocol -j2` passes.
`ctest --test-dir build -R caeron_test_protocol --output-on-failure` passes.
Direct include-only compile for all protocol headers passes with `c++ -std=c++20 -I src -c`.

Findings:

1. Medium: `ErrorFlyweight::get_offending_header()` documents that it throws if the stored offending-header length exceeds `frame_length()`, but it silently truncates to the remaining frame bytes instead. With a malformed frame such as `frame_length=64` and `offending_header_length=5000`, callers get a partial offending header and no indication the ERR frame is corrupt. If `frame_length < OFFENDING_HEADER_FIELD_OFFSET`, `available` goes negative and the method silently copies nothing.
2. Medium: `ErrorFlyweight::set_error_message()` cannot be used on a freshly constructed/written ERR frame unless the caller pre-seeds `frame_length()` to at least `12 + offending_header_length()`. It calls `error_string_offset()`, which validates against the current frame length before the setter updates it. Tests work around this by setting `frame_length(256)` first, but the method documentation says it updates frame length itself.
3. Medium: `ErrorFlyweight::set_offending_header()` validates destination capacity but not the source range. If `src_offset + length` exceeds the source buffer capacity, the new `UnsafeBuffer::put_bytes(dst, src, src_offset, length)` relies only on `assert`, so release builds can read past the source buffer.
