- [x] Convergence-review media implementation with independent agents until a follow-up pass finds no new issues.
- [x] Convergence-review media unit tests and verification gaps.
- [x] Reconcile agent findings against current source and remove stale/duplicate candidates.
- [x] Run focused media verification after review convergence.
- [x] Record final converged findings.

## Media Convergence Review

Scope: `src/caeron/driver/media` and `tests/media`.

Method:

1. Split implementation and test review across independent agents.
2. Locally verify every candidate against the current files.
3. Run a targeted second pass over confirmed candidates and adjacent code.
4. Stop when the follow-up pass produces no new verified issues.

Verification:

`cmake --build build/default --target caeron_test_media -j2` passes.
`ctest --test-dir build/default -R caeron_test_media --output-on-failure` passes with elevated permissions.
`cmake --build build/ubsan --target caeron_test_media -j2` passes.
`ctest --test-dir build/ubsan -R caeron_test_media --output-on-failure` passes with elevated permissions.
Current media test count is 210.

Converged current findings:

1. High: `SendChannelEndpoint` multicast wiring sends data to `remote_control` and listens for control on `remote_data`, reversing the intended multicast publication flow.
2. High: `MultiRcvDestination::remove_destination()` can destroy a destination transport while `DataTransportPoller` still stores raw pointers to it.
3. High: `DataTransportPoller` and `ControlTransportPoller` can misinterpret epoll user-data if they share the same `EpollPoller`.
4. Medium: poller destruction leaves epoll registrations pointing into destroyed poller list nodes.
5. Medium: `UdpChannelTransport::open_datagram_channel()` is not exception-safe after partial open.
6. Medium: `UdpChannel` accepts mixed endpoint/control address families.
7. Medium: numeric URI parameters accept trailing garbage.
8. Medium: multicast TTL rejects negatives but not values above 255.
9. Medium: oversized numeric IPv6 scope IDs can truncate when narrowed to `unsigned int`.
10. Low: unbracketed IPv6 can be parsed as a generic host:port string instead of rejected as malformed.
11. Test gap: control poller tests prove bytes were read, not that frames dispatched to a registered publication.
12. Test gap: malformed-frame tests can pass without proving the datagram was received and rejected.
13. Test gap: some older pointer-stability tests remain non-exercising smoke tests, though newer epoll-path tests do send traffic.
14. Test gap: endpoint concurrency tests remain crash-only/probabilistic smoke tests without TSAN or strong invariants.

Rejected candidate:

1. `InterfaceSearchAddress` returning a down interface was not accepted; current code only proceeds after `network_util::find_first_matching_local_address()` finds an up match.

---

- [x] Re-review media after four-item fix pass.
- [x] Verify claimed fixes in current source.
- [x] Run default and UBSan media verification.
- [x] Record remaining issues and coverage gaps.

## Media Re-review After Four Fixes

Scope: `src/caeron/driver/media` and `tests/media`.

Verification:

`cmake --build build/default --target caeron_test_media -j2` passes.
`ctest --test-dir build/default -R caeron_test_media --output-on-failure` passes with elevated permissions.
`cmake --build build/ubsan --target caeron_test_media -j2` passes.
`ctest --test-dir build/ubsan -R caeron_test_media --output-on-failure` passes with elevated permissions.
`build/default/tests/media/caeron_test_media --gtest_list_tests | rg -n "^  " | wc -l` reports 210 tests.

Current findings:

1. Medium: `UdpChannelTransport::open_datagram_channel` is still not exception-safe after a successful bind. The bind failure, multicast send-socket creation failure, and multicast connect failure paths have explicit cleanup, but other throwing paths after bind still leave an opened transport object until destructor/explicit close. Examples include `set_nonblocking(receive_fd_)`, `join_multicast()`, `set_nonblocking(send_fd_)`, and the unicast `connect()` failure path. A caught exception leaves `receive_fd_`/`send_fd_` non-negative, so retry hits the already-open guard and any managed-port reservation is held until later destruction.
2. Low/test: the wildcard AF_INET6 fix is present in source, but there is no direct regression test for `InterfaceSearchAddress::wildcard().resolve(false, AF_INET6)` returning an AF_INET6 any address.
3. Low/test: multicast cleanup fixes are present for send-socket creation and connect failure, but there is no direct regression test for managed-port cleanup on multicast open failures after bind. The existing `BindFailureFreesManagedPort` only covers bind failure.
4. Accepted limitation: endpoint concurrency tests remain bounded smoke tests, useful for contention but not TSAN/linearizability proof.

Resolved in the current tree:

1. Wildcard any-address resolve now rewrites the returned address family to match `protocol_family`.
2. Multicast send-socket creation and connect failure paths now free the managed port and close sockets manually.
3. `BindFailureFreesManagedPort` now uses a deterministic occupied UDP port instead of assuming port 1 is restricted.

---

- [x] Fresh current-tree media review after the 8-item fix pass.
- [x] Re-check implementation fixes and unit-test coverage.
- [x] Run default and UBSan media verification.
- [x] Document current remaining findings.

## Fresh Media Review

Scope: `src/caeron/driver/media` and `tests/media` as currently checked out.

Verification:

`cmake --build build/default --target caeron_test_media -j2` passes.
`ctest --test-dir build/default -R caeron_test_media --output-on-failure` passes with elevated permissions.
`cmake --build build/ubsan --target caeron_test_media -j2` passes.
`ctest --test-dir build/ubsan -R caeron_test_media --output-on-failure` passes with elevated permissions.
`build/default/tests/media/caeron_test_media --gtest_list_tests | rg -n "^  " | wc -l` reports 210 tests.

Current findings:

1. Medium: `InterfaceSearchAddress::resolve` still returns any-local addresses before validating `address_.ss_family` against `protocol_family`. `InterfaceSearchAddress::wildcard().resolve(false, AF_INET6)` returns an IPv4 any address for an IPv6 caller instead of throwing or returning IPv6 any.
2. Medium: `UdpChannelTransport::open_datagram_channel` still leaks a managed-port reservation if multicast open fails while creating the separate send socket. The bind-failure path frees the reservation, but the `send_fd_ < 0` path closes `receive_fd_`, sets it to `-1`, and bypasses destructor cleanup/free.
3. Low/test: `UdpChannelTransport.BindFailureFreesManagedPort` depends on binding port 1 failing. That is host-policy dependent and can false-fail on systems where unprivileged low ports are allowed and port 1 is free. Use a deterministic conflict or invalid bind address instead.
4. Low/test: endpoint concurrency tests remain bounded smoke tests. They are useful contention coverage and pass under the current UBSan build, but they are not TSAN coverage or a linearizability proof.

Resolved in the current tree:

1. Scoped IPv6 multicast detection now has direct tests.
2. Group-tag zero status message serialization now has direct tests.
3. Poller epoll user-data storage uses `std::list`.
4. IPv6 interface parsing without port is covered.
5. Bind failure after managed-port reservation is covered, with the flakiness caveat above.

---

- [x] Exhaustively audit each media header and unit test file.
- [x] Classify findings as implementation bug, API/design risk, or test gap.
- [x] Re-run focused verification after the expanded audit.
- [x] Replace the media review section with the expanded result.

## Expanded Media Review

Scope: every file under `src/caeron/driver/media` and `tests/media`.

Goal: find all review-worthy issues, not just the top subset. Include lower-severity correctness risks and unit-test blind spots, but avoid style-only comments unless they create real maintenance or behavior risk.

Results:

Verification:

`cmake --build build/default --target caeron_test_media -j2` passes.
`ctest --test-dir build/default -R caeron_test_media --output-on-failure` passes with elevated permissions.
`cmake --build build/ubsan --target caeron_test_media -j2` passes.
`ctest --test-dir build/ubsan -R caeron_test_media --output-on-failure` passes with elevated permissions.

Post-fix verification on the latest files:

1. Resolved: `InterfaceSearchAddress::resolve` validates `address_.ss_family` against `protocol_family` before lookup.
2. Resolved: `UdpChannelTransport::open_datagram_channel` frees the managed bind address on bind failure.
3. Resolved: `DataTransportPoller` and `ControlTransportPoller` now use `std::list`, avoiding epoll user-data pointers into erase-moving deque elements.
4. Resolved: `InterfaceSearchAddress::parse` accepts bracketed IPv6 without a port, including `[::1]/128`.
5. Resolved in implementation: `socket_address_parser::is_multicast_address` strips `%scope` before IPv6 `inet_pton`. Residual test note: no direct scoped multicast regression test was found in `socket_address_parser_test.cpp`.
6. Resolved: `UdpChannelTransport.BindFailureFreesManagedPort` covers bind failure after managed-port reservation.
7. Resolved: `ReceiveChannelEndpoint.SendStatusMessageWithGroupTagZero` covers explicit `group-tag=0` status-message serialization.
8. Accepted limitation: endpoint concurrency tests remain fixed-duration smoke tests; they are useful contention coverage but not a proof of linearizability or TSAN-equivalent race freedom.

Resolved stale findings from earlier passes:

1. `UdpChannel::parse` now rejects non-UDP Aeron transports.
2. `UdpChannel::operator==` and canonical form are deterministic for equivalent channels.
3. `UdpChannelTransport::close` frees the actual bound managed address on successful open.
4. `ReceiveChannelEndpoint` preserves `group-tag=0` presence with `std::optional`.
5. IPv4 prefixes above `/32` are rejected.
6. `SendChannelEndpoint::dispatch_control_frame` now enforces protocol-specific minimum frame lengths.
7. `UdpChannel::parse_size_value` rejects negatives before suffixed multiplication.
8. The old send `EAGAIN` test was replaced by a realistic non-negative send contract, and receive `EAGAIN` is covered separately.

---

- [x] Review media module public API and implementation for correctness risks.
- [x] Review media unit tests for coverage quality and blind spots.
- [x] Run focused media build/tests and capture failures.
- [x] Document consolidated review findings.

## Media Review

Scope: `src/caeron/driver/media`, `tests/media`, and test/build wiring needed to compile or execute the media unit tests.

Review questions:

1. Do media classes preserve socket/channel/address invariants across parsing, binding, polling, and endpoint setup?
2. Do tests exercise error paths and edge cases rather than only happy paths?
3. Are any implementation choices brittle, undefined, platform-specific, or likely to break under realistic use?

Results:

Verification:

`cmake --build build/default --target caeron_test_media -j2` passes.
`ctest --test-dir build/default -R caeron_test_media --output-on-failure` fails inside the sandbox because UDP socket creation and interface enumeration are not permitted.
`ctest --test-dir build/default -R caeron_test_media --output-on-failure` passes when run with elevated permissions.

Current findings:

1. High: `UdpChannel::operator==` compares a canonical form that deliberately appends a process-local monotonic counter, so two separately parsed equal URIs never compare equal. `MultiRcvDestination::find_transport()` depends on that equality and will fail to locate an existing destination by an equivalent channel.
2. Medium: `UdpChannelTransport` asks the `PortManager` for an allocated bind address but closes by freeing the original bind address. When the original address has port 0 and the manager allocates a real port, close frees port 0 instead of the allocated port, leaking the allocation in the manager.
3. Medium: `ReceiveChannelEndpoint` stores `group_tag` as `value_or(0)` and later treats `group_tag_ != 0` as presence. An explicit `group-tag=0` is therefore serialized as if no group tag exists, even though the URI preserves presence as an optional.
4. Medium: `InterfaceSearchAddress` accepts IPv4 prefixes up to 128, and the test suite codifies `192.168.1.0/128` as valid. That contradicts the IPv4 default `/32` behavior and means invalid IPv4 interface specs are silently accepted.
5. Low: The epoll pointer regression tests for data/control pollers can false-pass. Some never make an fd readable, and the erase tests use only two transports, which stays on the direct-poll path rather than the epoll path they intend to validate.
6. Low: The data poller lacks negative coverage for invalid DATA/SETUP/RTT datagrams even though the control poller has invalid-version and mismatched-length coverage.
7. Low: Several UDP send/receive tests block on `UdpSocket::receive_from()` after a sleep. If a packet is dropped or the send path regresses, those tests can hang instead of failing cleanly.

---

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
