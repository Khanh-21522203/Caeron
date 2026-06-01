# Phase 6: Network Media Layer — Teaching Guide

## Overview

Phase 6 implements the network transport layer that sits between raw UDP sockets (Phase 1) and the driver agents (Phase 8+). It parses channel URIs, manages socket lifecycles, dispatches incoming frames by type, and tracks per-source connection state.

**20 C++ headers** | **173 tests** | **3,369 lines of source** | **3,019 lines of tests**

---

## Architecture

```
                    ┌─────────────────────────────────┐
                    │         UdpChannel               │
                    │  (URI parsing, address resolution)│
                    └──────────────┬──────────────────┘
                                   │
                    ┌──────────────▼──────────────────┐
                    │     UdpChannelTransport          │
                    │  (socket lifecycle, RAII)         │
                    └──────┬───────────────┬──────────┘
                           │               │
              ┌────────────▼──┐    ┌───────▼────────────┐
              │SendChannel    │    │ReceiveChannel       │
              │Endpoint       │    │Endpoint              │
              │(outbound)     │    │(inbound)             │
              └──────┬────────┘    └───────┬─────────────┘
                     │                     │
         ┌───────────▼───┐    ┌────────────▼──────────┐
         │ControlTransport│    │DataTransportPoller    │
         │Poller          │    │(DATA/SETUP dispatch)  │
         │(SM/NAK/RTT)    │    └───────────────────────┘
         └────────────────┘
```

---

## Component Walkthrough

### 1. Channel URI Parsing (`UdpChannel`, `ChannelUri`)

**Purpose**: Parse `aeron:udp?endpoint=224.0.1.1:40456|interface=eth0|ttl=16` into resolved socket addresses.

**Key design decisions**:
- `ChannelUri` is a simple key-value parser (split on `?`, then `|`, then `=`)
- `UdpChannel` resolves addresses using `SocketAddressParser` + `NetworkUtil`
- Multicast detection: IPv4 `(first_octet & 0xF0) == 0xE0`, IPv6 `s6_addr[0] == 0xFF`
- Canonical form generation uses a `static std::atomic<int>` counter for uniqueness

**Overflow-safe size parsing**:
```cpp
// parse_size_value() — prevents i64 overflow on "16g" → 16*1024*1024*1024
if (result > std::numeric_limits<i64>::max() / multiplier) {
    return std::numeric_limits<int>::max();  // saturate
}
```

### 2. Socket Address Parsing (`SocketAddressParser`)

**Purpose**: Parse `host:port` and `[::1%eth0]:port` into structured addresses.

**IPv6 bracket notation**:
- `[::1]:40456` — brackets required for IPv6 with port
- `[fe80::1%eth0]:40456` — scope ID preserved for `getaddrinfo`
- `::1` — bare IPv6 (no port) accepted without brackets

**Multicast detection**:
```cpp
// IPv4: 224.0.0.0 - 239.255.255.255
if (af == AF_INET) return (bytes[0] & 0xF0) == 0xE0;
// IPv6: ff00::/8
if (af == AF_INET6) return bytes[0] == 0xFF;
```

### 3. Interface Resolution (`InterfaceSearchAddress`, `NamedInterface`, `ResolvedInterface`)

**Purpose**: Resolve interface specifications like `192.168.1.0/24` or `{eth0}` to concrete network interfaces.

**Design pattern**: `std::variant<InterfaceSearchAddress, NamedInterface>` models Java's sealed interface `UnresolvedInterface`.

**Key function**: `network_util::filter_by_subnet()` uses `getifaddrs()` to enumerate interfaces and matches by prefix length.

### 4. Base Transport (`UdpChannelTransport`)

**Purpose**: RAII wrapper around UDP socket lifecycle.

**Socket lifecycle**:
```
open_datagram_channel()
  ├─ socket(AF_INET/SOCK_DGRAM)
  ├─ setsockopt(SO_REUSEADDR, SO_RCVBUF, SO_SNDBUF)
  ├─ bind()
  ├─ connect()  (if unicast)
  └─ IP_ADD_MEMBERSHIP  (if multicast)
```

**RAII safety**:
- Destructor calls `close()` (idempotent — checks `fd >= 0`, resets to `-1`)
- For multicast: separate `send_fd_` and `receive_fd_` (both cleaned up)
- For unicast: `send_fd_ == receive_fd_` (single socket, no double-close)

**EAGAIN handling**:
```cpp
// receive() — non-blocking, returns false on EAGAIN
if (errno == EAGAIN || errno == EWOULDBLOCK) return false;

// send() — returns 0 bytes sent on EAGAIN
if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
```

### 5. Channel Endpoints (`SendChannelEndpoint`, `ReceiveChannelEndpoint`)

**Purpose**: Aggregate multiple publications/subscriptions onto one socket.

**Thread safety**:
- `publication_by_session_and_stream_` protected by `std::shared_mutex`
- `ref_count_` is `std::atomic<int>`
- Ref-count maps protected by `ref_count_mutex_`
- Diagnostic counters are `std::atomic<i64>`

**Frame dispatch** (SendChannelEndpoint):
```cpp
// Minimum 16 bytes: common header (8) + session_id (4) + stream_id (4)
if (length < 16) return;

auto type = get_le16(buffer + HeaderFlyweight::TYPE_FIELD_OFFSET);
switch (type) {
    case HeaderFlyweight::HDR_TYPE_SM:   // 0x03
    case HeaderFlyweight::HDR_TYPE_NAK:  // 0x02
    case HeaderFlyweight::HDR_TYPE_RTTM: // 0x06
    case HeaderFlyweight::HDR_TYPE_ERR:  // 0x04
    case HeaderFlyweight::HDR_TYPE_RSP_SETUP: // 0x0B
}
```

**Frame construction** (ReceiveChannelEndpoint):
```cpp
// Status Message — 44 bytes with group tag
put_le32(sm_buffer_ + 8, session_id);
put_le32(sm_buffer_ + 12, stream_id);
put_le32(sm_buffer_ + 16, term_id);
put_le32(sm_buffer_ + 20, term_offset);
put_le32(sm_buffer_ + 24, window_length);
put_le64(sm_buffer_ + 28, receiver_id);
put_le64(sm_buffer_ + 36, group_tag);
```

### 6. Transport Pollers (`DataTransportPoller`, `ControlTransportPoller`)

**Purpose**: Poll registered transports for incoming frames and dispatch to endpoints.

**Container choice**: `std::deque` (not `std::vector`) for pointer stability across `push_back`/`erase`. This is critical because `epoll_event.data.ptr` stores raw pointers to `ChannelAndTransport` structs.

**Two polling modes**:
- **Direct iteration** (`poll_direct`): For few transports, poll each FD directly with `recvfrom()`
- **Epoll-based** (`poll_epoll`): For many transports, use `epoll_wait()` for efficient dispatch

**Frame validation before dispatch**:
```cpp
if (!transport->is_valid_frame(buffer, bytes_received)) return;
dispatch_frame(buffer, bytes_received, endpoint, transport_index);
```

### 7. ImageConnection

**Purpose**: Per-source connection state for liveness tracking.

**Cache-line padding**:
```cpp
struct ImageConnection {
    alignas(64) std::atomic<i64> time_of_last_activity_ns{0};
    alignas(64) std::atomic<i64> time_of_last_frame_ns{0};
    i64 eos_position = INT64_MAX;
    bool is_eos = false;
    sockaddr_storage control_address{};
};
static_assert(sizeof(ImageConnection) >= 128);
```

Each hot field gets its own cache line to prevent false sharing between the receiver agent (updating `time_of_last_frame_ns`) and the conductor agent (reading `time_of_last_activity_ns`).

### 8. Port Management (`WildcardPortManager`)

**Purpose**: Dynamic port allocation for multi-destination subscription (MDC).

**Port range**: Configured as `[low, high]`. If both 0, delegates to OS wildcard.

**Allocation**: Tracks all non-zero ports in `std::unordered_set<int>` to prevent double-allocation. Port exhaustion throws `std::runtime_error`.

---

## Common Pitfalls

### 1. Signed-Overflow UB in Bounds Checks

```cpp
// WRONG — offset + N can overflow
if (offset + N > capacity) return;

// RIGHT — subtraction cannot overflow when offset >= 0
if (capacity - offset < N) return;
```

All Phase 6 code uses the subtraction pattern.

### 2. IPv6 Bracket Notation

```cpp
// WRONG — can't distinguish host from port
auto parts = split(host_port, ':');

// RIGHT — detect brackets first
if (host_port.starts_with('[')) {
    auto close = host_port.find(']');
    // extract host between [ and ], port after ]:
}
```

### 3. Socket FD Double-Close

```cpp
// WRONG — double-close if send_fd_ == receive_fd_
close(send_fd_);
close(receive_fd_);

// RIGHT — check for shared socket
if (send_fd_ != receive_fd_) close(receive_fd_);
close(send_fd_);
send_fd_ = receive_fd_ = -1;
```

### 4. Pointer Stability in Poller Containers

```cpp
// WRONG — vector reallocation invalidates pointers stored in epoll
std::vector<ChannelAndTransport> transports_;

// RIGHT — deque never invalidates existing element pointers
std::deque<ChannelAndTransport> transports_;
```

### 5. Thread Safety in Endpoint Maps

```cpp
// WRONG — unprotected concurrent access
publication_map_[key] = pub;

// RIGHT — write lock for mutations
std::unique_lock lock(pub_mutex_);
publication_map_[key] = pub;
```

---

## Cross-Language Mapping

| C++ File | Java File | Key Differences |
|----------|-----------|-----------------|
| `socket_address_parser.h` | `SocketAddressParser.java` | No `NameResolver` coupling; returns host+port |
| `network_util.h` | `NetworkUtil.java` | Uses `getifaddrs()` instead of `NetworkInterface` |
| `interface_search_address.h` | `InterfaceSearchAddress.java` | `sockaddr_storage` replaces `InetSocketAddress` |
| `resolved_interface.h` | `ResolvedInterface.java` | POD struct, no `NetworkInterface` object |
| `named_interface.h` | `NamedInterface.java` | `if_nametoindex()` replaces `getByName()` |
| `udp_channel.h` | `UdpChannel.java` | `static std::atomic<int>` for canonical counter |
| `udp_channel_transport.h` | `UdpChannelTransport.java` | RAII socket FDs instead of NIO `DatagramChannel` |
| `send_channel_endpoint.h` | `SendChannelEndpoint.java` | `std::shared_mutex` for publication map |
| `receive_channel_endpoint.h` | `ReceiveChannelEndpoint.java` | Pre-allocated `alignas(64)` send buffers |
| `data_transport_poller.h` | `DataTransportPoller.java` | `std::deque` for pointer stability |
| `control_transport_poller.h` | `ControlTransportPoller.java` | Same deque pattern |
| `image_connection.h` | `ImageConnection.java` | `alignas(64)` atomic fields |
| `wildcard_port_manager.h` | `WildcardPortManager.java` | `std::unordered_set<int>` for port tracking |
| `multi_rcv_destination.h` | `MultiRcvDestination.java` | `vector<unique_ptr>` sparse array |
| `receive_destination_transport.h` | `ReceiveDestinationTransport.java` | Composes `UdpChannelTransport` |

---

## Testing Strategy

### Pure Logic Tests (no sockets)
- `socket_address_parser_test.cpp` — IPv4/IPv6 parsing, edge cases, multicast detection
- `network_util_test.cpp` — prefix matching, interface enumeration
- `interface_search_address_test.cpp` — address/prefix parsing
- `named_interface_test.cpp` — brace notation parsing
- `control_mode_test.cpp` — enum parsing
- `udp_channel_test.cpp` — full URI parsing, multicast, MDC, canonical form
- `wildcard_port_manager_test.cpp` — port allocation, exhaustion, cycling

### Integration Tests (real sockets on loopback)
- `udp_channel_transport_test.cpp` — socket lifecycle, send/receive, multicast join
- `send_channel_endpoint_test.cpp` — send operations, ref counting
- `receive_channel_endpoint_test.cpp` — receive dispatch, SM/NAK construction
- `data_transport_poller_test.cpp` — DATA/SETUP frame polling
- `control_transport_poller_test.cpp` — SM/NAK/RTT frame polling
- `image_connection_test.cpp` — atomic field updates, cache-line alignment

### Key Test Patterns
- All socket tests use loopback (`127.0.0.1` / `::1`) — fast, reliable, no network dependency
- Concurrent tests use `std::thread` + `std::atomic` for synchronization
- Wire format tests construct frames byte-by-byte and verify field values
- Edge case tests: empty strings, max port (65535), overflow on size parsing
