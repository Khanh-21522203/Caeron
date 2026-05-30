# Caeron Architecture

Caeron is a C++23 port of the [Aeron](https://github.com/real-logic/aeron) Media Driver. This document describes the scaffold structure and design decisions.

## C4 Architecture

### Level 1: System Context

The Caeron Media Driver sits between Aeron clients and the network. Clients communicate with the driver through a shared memory-mapped CnC file; the driver sends and receives media frames over UDP.

```mermaid
C4Context
    title System Context Diagram — Caeron Media Driver

    Person(publisher, "Publisher", "Sends messages via Aeron Client")
    Person(subscriber, "Subscriber", "Receives messages via Aeron Client")

    System_Boundary(caeron, "Caeron") {
        System(driver, "Media Driver", "Manages publications, subscriptions, and network I/O")
    }

    System_Ext(client_lib, "Aeron Client Library", "Java or C++ client that talks to the driver via CnC file")
    System_Ext(network, "UDP Network", "Wire protocol transport")

    Rel(publisher, client_lib, "Publishes messages")
    Rel(subscriber, client_lib, "Subscribes to streams")
    Rel(client_lib, driver, "CnC mmap file", "Commands & responses via shared memory")
    Rel(driver, network, "DATA/SM/NAK/SETUP frames", "UDP")
    Rel(network, driver, "DATA/SM/NAK/SETUP frames", "UDP")
```

### Level 2: Container

The driver is composed of several static libraries grouped by concern. Shared libraries sit at the bottom and are reusable by future Aeron Cluster and Aeron Archive components.

```mermaid
C4Container
    title Container Diagram — Caeron Libraries

    Person(client, "Aeron Client", "Sends commands, reads responses")

    Container_Boundary(driver_process, "Media Driver Process") {
        Container(caeron_driver, "caeron_driver", "C++23", "DriverConductor, Sender, Receiver agents")
        Container(caeron_cnc, "caeron_cnc", "C++23", "CnC file descriptor, mapped log buffers")
        Container(caeron_logbuffer, "caeron_logbuffer", "C++23", "Log buffer descriptors, term operations")
        Container(caeron_protocol, "caeron_protocol", "C++23", "Wire protocol flyweights (DATA, SM, NAK, SETUP, RTT)")
        Container(caeron_command, "caeron_command", "C++23", "CnC command flyweights (18 message types)")
        Container(caeron_concurrent, "caeron_concurrent", "C++23", "Lock-free ring buffers, broadcast, queues, counters")
        Container(caeron_common, "caeron_common", "C++23", "Types, byte order, bit utils, memory, errors")
        Container(caeron_platform, "caeron_platform_posix", "C++23", "mmap, UDP socket, epoll, thread, clock")
    }

    System_Ext(network, "UDP Network", "Wire protocol transport")

    Rel(client, caeron_cnc, "Reads/writes CnC mmap file")
    Rel(caeron_driver, caeron_cnc, "Uses")
    Rel(caeron_driver, caeron_protocol, "Uses")
    Rel(caeron_driver, caeron_logbuffer, "Uses")
    Rel(caeron_driver, caeron_concurrent, "Uses")
    Rel(caeron_driver, caeron_platform, "Uses")
    Rel(caeron_cnc, caeron_concurrent, "Uses")
    Rel(caeron_cnc, caeron_platform, "Uses")
    Rel(caeron_logbuffer, caeron_protocol, "Uses")
    Rel(caeron_protocol, caeron_concurrent, "Uses")
    Rel(caeron_concurrent, caeron_common, "Uses")
    Rel(caeron_driver, network, "UDP I/O")
```

### Level 3: Component — Driver Internals

The driver runs three agent threads, each on its own dedicated thread. They communicate through lock-free shared data structures — no mutexes.

```mermaid
C4Component
    title Component Diagram — Media Driver Agents

    Container_Boundary(driver, "caeron_driver") {
        Component(conductor, "DriverConductor", "Agent", "Manages publications, subscriptions, images, clients, counters. Processes commands from CnC ring buffer.")
        Component(sender, "Sender", "Agent", "Reads from publication log buffers, sends DATA frames over UDP. Processes SMs and retransmits on NAK.")
        Component(receiver, "Receiver", "Agent", "Receives DATA/SETUP frames from UDP, inserts into term buffers. Sends SMs and NAKs.")
    }

    Container_Boundary(shared, "Shared Memory Structures") {
        Component(cnc_ring, "ManyToOneRingBuffer", "Lock-free MPSC", "Client → Driver commands")
        Component(cnc_broadcast, "BroadcastTransmitter", "Lock-free SPSC", "Driver → Client responses")
        Component(log_buffers, "LogBuffers", "3-term rotation", "Publication/subscription data with CAS tail counters")
        Component(counters, "CountersManager", "Counter slots", "Allocate/free counters with metadata")
        Component(conductor_queue, "ManyToOneConcurrentLinkedQueue", "Lock-free MPSC", "Sender/Receiver → DriverConductor")
    }

    System_Ext(network, "UDP Network", "Wire protocol transport")
    Person(client, "Aeron Client", "Sends commands via CnC file")

    Rel(client, cnc_ring, "Writes commands")
    Rel(cnc_broadcast, client, "Reads responses")
    Rel(conductor, cnc_ring, "Reads commands")
    Rel(conductor, cnc_broadcast, "Writes responses")
    Rel(conductor, counters, "Allocates counters")
    Rel(conductor, conductor_queue, "Enqueues events for Sender/Receiver")
    Rel(sender, log_buffers, "Reads from term buffers")
    Rel(sender, network, "Sends DATA frames")
    Rel(sender, conductor_queue, "Reports back to Conductor")
    Rel(receiver, network, "Receives DATA/SETUP frames")
    Rel(receiver, log_buffers, "Writes into term buffers")
    Rel(receiver, conductor_queue, "Reports back to Conductor")
    Rel(sender, network, "Sends SM/NAK")
    Rel(receiver, network, "Sends SM/NAK")
```

## Project Layout

```
Caeron/
├── CMakeLists.txt              # Root build (C++23, GoogleTest via FetchContent)
├── cmake/
│   └── CompilerWarnings.cmake  # Strict warnings (-Wall -Wextra -Werror)
├── src/
│   ├── caeron/                 # Core libraries
│   │   ├── common/             # Fundamental types and utilities
│   │   ├── concurrent/         # Lock-free data structures
│   │   ├── protocol/           # Wire protocol flyweights
│   │   ├── command/            # CnC command flyweights
│   │   ├── logbuffer/          # Log buffer descriptors and operations
│   │   ├── cnc/                # Command-and-Control file
│   │   ├── driver/             # Media Driver agents (future)
│   │   ├── cluster/            # Aeron Cluster (placeholder)
│   │   └── archive/            # Aeron Archive (placeholder)
│   └── platform/
│       └── posix/              # POSIX platform abstraction
├── tests/
│   ├── common/
│   ├── protocol/
│   ├── command/
│   ├── concurrent/
│   ├── logbuffer/
│   ├── cnc/
│   ├── driver/
│   └── platform/
└── docs/
```

## Library Dependency Graph

```
caeron_common (INTERFACE)
    └─► caeron_concurrent
            ├─► caeron_protocol
            ├─► caeron_command
            ├─► caeron_logbuffer
            └─► caeron_cnc ──► caeron_platform_posix
                    └─► caeron_driver (future)
```

All libraries are static. `caeron_common` is INTERFACE (header-only). Future `cluster` and `archive` components will depend on the same shared base.

## Design Principles

### 1. Shared Base, Component-Specific Top

The libraries under `src/caeron/` are split into two tiers:

- **Shared** (`common`, `concurrent`, `protocol`, `command`, `logbuffer`, `cnc`): Used by the driver today, but designed to be reused by future `aeron-cluster` and `aeron-archive` ports. No driver-specific logic lives here.
- **Component-specific** (`driver/`, `cluster/`, `archive/`): Each component gets its own subdirectory. The driver is implemented first; cluster and archive are empty placeholders.

### 2. Flyweight Pattern

Protocol and command messages use the **flyweight** pattern: a lightweight struct that interprets a region of an `UnsafeBuffer` at a given offset. Flyweights are composed (not inherited) — each contains a reference to the buffer and an offset.

```cpp
struct DataHeaderFlyweight {
    UnsafeBuffer& buffer_;
    i32 offset_;

    i32 term_offset() const { return buffer_.get_i32(offset_ + TERM_OFFSET_FIELD_OFFSET); }
    DataHeaderFlyweight& set_term_offset(i32 value) { buffer_.put_i32(offset_ + TERM_OFFSET_FIELD_OFFSET, value); return *this; }
};
```

All multi-byte accessors use `memcpy` internally (via `UnsafeBuffer`) to avoid alignment faults. Byte order is little-endian on x86_64 (static_assert enforced).

### 3. Lock-Free Concurrent Structures

The driver's three agent threads (DriverConductor, Sender, Receiver) communicate through shared memory with no mutexes:

| Structure | Direction | Use Case |
|-----------|-----------|----------|
| `ManyToOneRingBuffer` | Client → Driver | Commands from clients |
| `BroadcastTransmitter` | Driver → Client | Responses/events to clients |
| `BroadcastReceiver` | Client ← Driver | Reading responses |
| `ManyToOneConcurrentLinkedQueue` | Driver internal | Inter-agent messaging |
| `OneToOneConcurrentArrayQueue` | Driver internal | Single-producer queues |
| `CountersManager` | Shared | Counter allocation/deallocation |
| `AtomicCounter` | Shared | Atomic operations on a single counter |

### 4. Memory-Mapped Files

The driver communicates with clients through memory-mapped files:

- **CnC file**: Command-and-Control. Contains the command ring buffer, broadcast buffer, and metadata (version, PID, timestamps).
- **Log files**: Each publication/subscription gets a log file with 3 term buffers + metadata. Terms rotate via CAS on tail counters.

`platform::MemoryMappedFile` provides RAII mmap/munmap.

### 5. Platform Abstraction

Platform-specific code lives under `src/platform/posix/`:

| File | Purpose |
|------|---------|
| `mmap.h/.cpp` | RAII memory-mapped files |
| `udp_socket.h/.cpp` | UDP socket wrapper |
| `epoll_poller.h/.cpp` | I/O multiplexing |
| `thread.h/.cpp` | Thread with CPU affinity |
| `clock.h/.cpp` | Monotonic/realtime clocks |

## Key Data Structures

### Log Buffer Layout

Each log file has this layout:

```
┌─────────────────────────────────────────────┐
│  Term Buffer 0  (default 64MB)              │
├─────────────────────────────────────────────┤
│  Term Buffer 1                              │
├─────────────────────────────────────────────┤
│  Term Buffer 2                              │
├─────────────────────────────────────────────┤
│  Log Meta Data (4096 bytes)                 │
│    - tail counters (3 × i64)                │
│    - active term count                      │
│    - MTU length, session/stream IDs         │
│    - end-of-stream position                 │
└─────────────────────────────────────────────┘
```

Terms rotate: term N maps to `buffer[N % 3]`. Tail counters are packed as `(term_id << 32 | term_offset)` and updated via CAS.

### CnC File Layout

```
┌─────────────────────────────────────────────┐
│  Meta Data (128 bytes)                      │
│    - version, PID, start timestamp          │
│    - to-driver buffer length                │
│    - to-clients buffer length               │
│    - counter metadata buffer length         │
│    - counter values buffer length           │
├─────────────────────────────────────────────┤
│  To-Driver Buffer (commands from clients)   │
├─────────────────────────────────────────────┤
│  To-Clients Buffer (responses to clients)   │
├─────────────────────────────────────────────┤
│  Counter Metadata Buffer                    │
├─────────────────────────────────────────────┤
│  Counter Values Buffer                      │
└─────────────────────────────────────────────┘
```

## Wire Protocol

All frames start with an 8-byte `HeaderFlyweight`:

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                        Frame Length                           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|    Version    |     Flags     |            Type               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

Frame types: DATA(0x01), SETUP(0x04), NAK(0x02), SM(0x03), RTT(0x05), ERROR(0x06), etc.

## Build System

- **CMake 3.28+** with C++23
- **GoogleTest** fetched via FetchContent
- **Compile flags**: `-Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion`
- Each library has its own `CMakeLists.txt` under `src/caeron/` and `src/platform/`
- Tests mirror the source structure under `tests/`

## Testing Strategy

- Unit tests for each library component
- Flyweight tests verify field offsets match Java Aeron's binary layout
- Concurrent structure tests verify lock-free correctness
- Platform tests verify POSIX wrapper behavior
