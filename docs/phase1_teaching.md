# Phase 1: Platform Layer — Teaching Guide

This guide teaches you how to complete Phase 1 of the Caeron roadmap. It's written for developers who know C#, Rust, Go, or Python but are new to C++.

## What You'll Learn

- POSIX socket programming (`setsockopt`, `sendmmsg`, `recvmmsg`)
- Memory mapping and page management (`madvise`, page faults)
- C++ patterns that map to what you already know
- How to write tests for platform-level code

---

## Existing Code Reference: `src/platform/posix/`

This section documents every class and method currently implemented. Read this before implementing Phase 1 additions.

### Type Aliases (from `caeron/common/types.h`)

All files use these aliases. They're the same as `cstdint` types:

```cpp
// C++                   // Rust        // C#         // Go         // Python
using u8  = uint8_t;     // u8          // byte        // byte       // int (0-255)
using u16 = uint16_t;    // u16         // ushort      // uint16     // int (0-65535)
using u32 = uint32_t;    // u32         // uint        // uint32     // int
using u64 = uint64_t;    // u64         // ulong       // uint64     // int
using i8  = int8_t;      // i8          // sbyte       // int8       // int
using i16 = int16_t;     // i16         // short       // int16      // int
using i32 = int32_t;     // i32         // int         // int32      // int
using i64 = int64_t;     // i64         // long        // int64      // int
```

### `MemoryMappedFile` (`mmap.h` / `mmap.cpp`)

Maps a file into virtual memory so you can read/write it like an array. Used for CnC file and log buffers.

```
// Rust:   mmap = unsafe { MmapOptions::new().map_mut(&file)? }
// C#:     var mmap = MemoryMappedFile.CreateFromFile(path)
// Go:     mmap, _ := syscall.Mmap(fd, 0, size, syscall.PROT_READ|syscall.PROT_WRITE, syscall.MAP_SHARED)
// Python: mmap = mmap.mmap(fd, size)
```

#### Fields

| Field | Type | Meaning |
|-------|------|---------|
| `addr_` | `void*` | Pointer to the mapped memory region. Like `*mut u8` in Rust, `IntPtr` in C#. |
| `size_` | `i64` | Size of the mapping in bytes |
| `owns_file_` | `bool` | If `true`, the destructor deletes the file from disk (`unlink`) |
| `path_` | `std::string` | File path. `std::string` is like `String` in C#/Rust, `string` in Go. |

#### Static Factory Methods

```cpp
static MemoryMappedFile create_new(const std::string& path, i64 size);
```
- Creates a new file at `path`, sets its size to `size`, maps it read-write
- `owns_file_ = true` → destructor will delete the file
- Internally: `open()` → `ftruncate()` → `mmap()` → `close(fd)`
- Throws `std::runtime_error` on failure (like Go's `err`, Rust's `Err`, but via exceptions)

```cpp
static MemoryMappedFile map_existing(const std::string& path, bool read_only = false);
```
- Opens an existing file, maps it read-write (or read-only)
- `owns_file_ = false` → destructor won't delete the file
- Internally: `open()` → `fstat()` to get size → `mmap()` → `close(fd)`

#### Destructor & Move Semantics

```cpp
~MemoryMappedFile();           // calls unmap()
MemoryMappedFile(MemoryMappedFile&& other) noexcept;  // move constructor
MemoryMappedFile& operator=(MemoryMappedFile&& other) noexcept;  // move assignment
MemoryMappedFile(const MemoryMappedFile&) = delete;   // no copy
MemoryMappedFile& operator=(const MemoryMappedFile&) = delete;  // no copy
```

**Move vs Copy (C++ specific):**
```cpp
// C++ has EXPLICIT move semantics (like Rust, unlike C#/Go/Python)
auto a = MemoryMappedFile::create_new("/tmp/test", 4096);
auto b = std::move(a);   // b now owns the mapping, a is empty
// a.addr_ == nullptr now, a.size_ == 0
// b.addr_ == the mapped memory

// In Rust:  let b = a;  (move by default)
// In C#:   var b = a;   (both point to same object — reference copy)
// In Go:   b := a       (struct copy — both are valid)
// In Python: b = a      (reference copy)
```

`= delete` prevents accidental copies. If you tried `auto b = a;` it won't compile.

#### Accessors

```cpp
[[nodiscard]] std::span<std::byte> span() noexcept;
```
- Returns a view over the entire mapped region
- `std::span<std::byte>` is like `&[u8]` in Rust, `Span<byte>` in C#, `[]byte` in Go
- `noexcept` means it never throws (like Rust's `-> T` vs `-> Result<T>`)
- `[[nodiscard]]` means the compiler warns if you ignore the return value

```cpp
[[nodiscard]] const std::span<const std::byte> span() const noexcept;
```
- `const` overload — returns a read-only view
- Called when the `MemoryMappedFile` itself is `const`

```cpp
[[nodiscard]] void* addr() noexcept;     // raw pointer to mapped memory
[[nodiscard]] i64 size() const noexcept;  // size in bytes
```

#### Private Helper

```cpp
MemoryMappedFile(void* addr, i64 size, bool owns_file, std::string path);
```
- Private constructor used by the factories. `std::move(path)` transfers ownership of the string.

```cpp
void unmap();
```
- Calls `munmap()` to release the mapping
- If `owns_file_`, calls `unlink()` to delete the file from disk
- Safe to call multiple times (checks `addr_ != nullptr`)

---

### `UdpSocket` (`udp_socket.h` / `udp_socket.cpp`)

RAII wrapper for a UDP socket file descriptor.

```
// Rust:   let sock = UdpSocket::bind("0.0.0.0:9999")?
// C#:     var sock = new Socket(AddressFamily.InterNetwork, SocketType.Dgram, ProtocolType.Udp)
// Go:     conn, _ := net.ListenPacket("udp", ":9999")
// Python: sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
```

#### Fields

| Field | Type | Meaning |
|-------|------|---------|
| `fd_` | `int` | The socket file descriptor. `-1` means not open. |

#### Constructor & Destructor

```cpp
UdpSocket();
```
- Creates a UDP socket: `::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)`
- Throws if socket creation fails

```cpp
~UdpSocket();
```
- Calls `close()` to close the fd

#### Move Semantics

```cpp
UdpSocket(UdpSocket&& other) noexcept;             // takes ownership of other.fd_
UdpSocket& operator=(UdpSocket&& other) noexcept;  // closes own fd_, takes other's
UdpSocket(const UdpSocket&) = delete;               // no copy (two objects can't own same fd)
```

#### Network Methods

```cpp
void bind(const std::string& address, u16 port);
```
- Binds the socket to a local address and port
- `address` = IP string like `"0.0.0.0"` or `"127.0.0.1"`
- Internally: `inet_pton()` converts string → binary, then `::bind(fd, &addr, sizeof(addr))`
- `inet_pton` = "presentation to network" (string → bytes). Opposite: `inet_ntop`

```
// Rust:   socket.bind("0.0.0.0:9999".parse()?)?
// C#:     socket.Bind(new IPEndPoint(IPAddress.Any, 9999))
// Go:     conn, _ := net.ListenPacket("udp", ":9999")
// Python: sock.bind(("0.0.0.0", 9999))
```

```cpp
void connect(const std::string& address, u16 port);
```
- Sets a default destination for `send()` (instead of `send_to()`)
- For UDP, this doesn't establish a connection — it just sets the default peer address

```cpp
[[nodiscard]] i32 send_to(const void* data, i32 length,
                           const std::string& address, u16 port);
```
- Sends `length` bytes from `data` to `address:port`
- Returns number of bytes sent, or -1 on error
- `const void*` = pointer to read-only data. Like `*const u8` in Rust, `const byte*` in C#.

```cpp
[[nodiscard]] i32 receive_from(void* buffer, i32 max_length,
                                std::string& from_address, u16& from_port);
```
- Receives up to `max_length` bytes into `buffer`
- Writes the sender's address into `from_address` and `from_port` (output parameters)
- Returns number of bytes received, or -1 on error
- `void*` = pointer to writable memory. Like `*mut u8` in Rust, `byte*` in C#.
- `std::string&` = reference to a string (mutates the caller's variable). Like `&mut String` in Rust, `ref string` in C#.

```
// Rust:   let (n, addr) = socket.recv_from(&mut buf)?
// C#:     int n = socket.ReceiveFrom(buffer, ref remoteEP)
// Go:     n, addr, _ := conn.ReadFrom(buf)
// Python: data, addr = sock.recvfrom(4096)
```

#### Socket Options

```cpp
void set_sndbuf(i32 size);
```
- Sets `SO_SNDBUF` — kernel send buffer size in bytes
- Larger buffer = more messages can be queued before `sendto` blocks/fails
- Default is typically 212992 bytes (~208KB)

```cpp
void set_rcvbuf(i32 size);
```
- Sets `SO_RCVBUF` — kernel receive buffer size in bytes
- Larger buffer = more messages can queue before the kernel drops them
- For high-throughput: set to 4MB+ (e.g., `4 * 1024 * 1024`)

```cpp
void join_multicast(const std::string& group, const std::string& iface = "");
```
- Joins a multicast group (e.g., `"224.0.1.1"`)
- `iface` = local interface IP to use (empty = kernel picks)
- Internally: `setsockopt(IP_ADD_MEMBERSHIP)` with `struct ip_mreq`

```cpp
void leave_multicast(const std::string& group, const std::string& iface = "");
```
- Leaves a multicast group
- Internally: `setsockopt(IP_DROP_MEMBERSHIP)`

#### Utility Methods

```cpp
[[nodiscard]] int fd() const noexcept;
```
- Returns the raw file descriptor. Useful for passing to `epoll_ctl`, etc.

```cpp
[[nodiscard]] bool is_open() const noexcept;
```
- Returns `fd_ >= 0`

```cpp
void close();
```
- Closes the socket fd, sets `fd_ = -1`
- Safe to call multiple times

---

### `EpollPoller` (`epoll_poller.h` / `epoll_poller.cpp`)

RAII wrapper for Linux epoll — I/O event notification. Like `select()` but O(1) for large fd sets.

```
// Rust:   let poll = mio::Poll::new()?
// C#:     var epoll = new Socket(SocketType.Dgram, ProtocolType.Udp)  // no direct equivalent
// Go:     epoll is internal to the runtime (netpoller)
// Python: import select; select.epoll()
```

#### Fields

| Field | Type | Meaning |
|-------|------|---------|
| `epoll_fd_` | `int` | The epoll file descriptor. `-1` means not open. |

#### Constructor & Destructor

```cpp
EpollPoller();
```
- Creates an epoll instance: `epoll_create1(0)`
- Returns a new fd that can monitor other fds for events

```cpp
~EpollPoller();
```
- Closes the epoll fd

#### Move Semantics

```cpp
EpollPoller(EpollPoller&& other) noexcept;
EpollPoller& operator=(EpollPoller&& other) noexcept;
EpollPoller(const EpollPoller&) = delete;
```

#### Methods

```cpp
void add(int fd, u32 events, void* ptr = nullptr);
```
- Registers `fd` with the epoll instance to watch for `events`
- `events` is a bitmask: `EPOLLIN` (readable), `EPOLLOUT` (writable), `EPOLLET` (edge-triggered)
- `ptr` = user data to associate with this fd (retrieved when event fires)
- If `ptr` is null, stores `fd` as the user data instead
- Internally: `epoll_ctl(EPOLL_CTL_ADD, fd, &ev)`

```cpp
void modify(int fd, u32 events, void* ptr = nullptr);
```
- Changes the events/user-data for an already-registered fd
- Internally: `epoll_ctl(EPOLL_CTL_MOD, fd, &ev)`

```cpp
void remove(int fd);
```
- Unregisters `fd` from the epoll instance
- Internally: `epoll_ctl(EPOLL_CTL_DEL, fd, nullptr)`

```cpp
[[nodiscard]] i32 poll(std::vector<struct epoll_event>& events, i32 timeout_ms);
```
- Waits for events on registered fds
- `events` = pre-allocated vector where results are written (caller owns the memory)
- `timeout_ms` = how long to wait: `-1` = block forever, `0` = non-blocking, `>0` = wait N ms
- Returns number of ready fds, or 0 on timeout
- Handles `EINTR` (interrupted by signal) by returning 0 instead of throwing

---

### `Thread` (`thread.h` / `thread.cpp`)

RAII wrapper for POSIX threads (`pthread`).

```
// Rust:   std::thread::spawn(move || { ... })
// C#:     var t = new Thread(() => { ... }); t.Start()
// Go:     go func() { ... }()
// Python: t = threading.Thread(target=func); t.start()
```

#### Fields

| Field | Type | Meaning |
|-------|------|---------|
| `thread_` | `pthread_t` | POSIX thread handle |
| `func_` | `std::function<void()>` | The function to run. Like `FnOnce()` in Rust, `Action` in C#. |
| `joinable_` | `bool` | Whether `join()` hasn't been called yet |

#### Constructor & Destructor

```cpp
explicit Thread(std::function<void()> func, const std::string& name = "");
```
- Creates and immediately starts a new thread running `func`
- `explicit` prevents implicit conversion from a lambda to `Thread`
- If `name` is non-empty, sets the thread name (for debugging, shows in `top -H`)
- Internally: `pthread_create()`

```cpp
~Thread();
```
- If still joinable (not yet joined), detaches the thread
- Detached threads run to completion but aren't waited on

#### Move Semantics

```cpp
Thread(Thread&& other) noexcept;
Thread& operator=(Thread&& other) noexcept;
Thread(const Thread&) = delete;
```

#### Methods

```cpp
void join();
```
- Blocks until the thread finishes
- Like `thread.join()` in C#/Rust, `<-ch` or `wg.Wait()` in Go
- After calling, `joinable_` = false
- Throws if not joinable (already joined or detached)

```cpp
void set_affinity(i32 cpu_index);
```
- Pins the thread to a specific CPU core
- For the Aeron driver: pin Conductor to core 0, Sender to core 1, Receiver to core 2
- Avoids context switches and cache thrashing
- Internally: `pthread_setaffinity_np()` with `cpu_set_t`

```cpp
void set_name(const std::string& name);
```
- Sets the thread name (max 15 chars on Linux)
- Shows in `top -H`, `ps -L`, `/proc/<pid>/task/<tid>/comm`
- Internally: `pthread_setname_np()`

#### Private Static

```cpp
static void* thread_func(void* arg);
```
- The actual function passed to `pthread_create`
- Casts `arg` back to `Thread*` and calls `func_()`
- Returns `nullptr` (required by pthread API)

---

### `clock.h` (header-only, no `.cpp`)

Two inline functions for time measurement.

```cpp
[[nodiscard]] inline i64 nano_time();
```
- Returns current monotonic time in **nanoseconds**
- Monotonic = always increases, never goes backwards (not affected by NTP adjustments)
- Use for measuring elapsed time: `auto start = nano_time(); ...; auto elapsed = nano_time() - start;`
- Internally: `clock_gettime(CLOCK_MONOTONIC, &ts)`
- Like `Instant::now()` in Rust, `Stopwatch.GetTimestamp()` in C#, `time.Now()` in Go

```cpp
[[nodiscard]] inline i64 epoch_time();
```
- Returns current wall-clock time in **milliseconds since Unix epoch** (1970-01-01)
- Use for timestamps, heartbeats, logging
- May jump if the system clock is adjusted (NTP, manual)
- Internally: `clock_gettime(CLOCK_REALTIME, &ts)`
- Like `System.currentTimeMillis()` in Java, `DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()` in C#

---

### Common Patterns Across All Classes

**RAII (Resource Acquisition Is Initialization):**
```
Constructor opens resource → Destructor closes it
No need for defer/using/try-finally
```

**Move-only (no copy):**
```
= delete on copy constructor and copy assignment
Move constructor and move assignment transfer ownership
Ensures only one object owns the OS resource at a time
```

**`noexcept`:**
```
Promise not to throw. Used on move operations and simple accessors.
Like Rust where most operations don't return Result.
```

**`[[nodiscard]]`:**
```
Compiler warns if you ignore the return value.
Prevents bugs like calling is_open() without checking the result.
```

**POSIX `::` prefix:**
```
::socket(), ::bind(), ::close() — the :: prefix means "call the global C function"
Without it, C++ would look for a member function named socket/bind/close first.
```

**Error handling:**
```
POSIX functions return -1 on error (with errno set).
Our wrappers throw std::runtime_error instead.
Like Go's if err != nil { return err } but via exceptions.
```

---

## Quick Reference: C++ ↔ Your Language

### RAII (Resource Acquisition Is Initialization)

| C++ | Rust | C# | Go | Python |
|-----|------|----|----|--------|
| Destructor `~T()` runs when object leaves scope | `Drop::drop()` | `IDisposable.Dispose()` via `using` | `defer f.Close()` | `__del__` or `with` statement |
| Move semantics: `T(T&&)` transfers ownership | `T` moves by default | N/A (reference types) | N/A (GC) | N/A (GC) |
| `= delete` prevents copy | No `Clone` impl | N/A | N/A | N/A |

```cpp
// C++ RAII — destructor runs automatically when `sock` leaves scope
{
    UdpSocket sock;           // constructor opens socket
    sock.bind("0.0.0.0", 9999);
}  // ~UdpSocket() calls ::close(fd_) here — no defer needed

// Rust equivalent:
// {
//     let sock = UdpSocket::bind("0.0.0.0:9999")?;
// }  // drop(sock) called here

// Go equivalent:
// sock, _ := net.ListenPacket("udp", ":9999")
// defer sock.Close()
```

### Move Semantics

```cpp
// C++ — explicit move (like Rust's move, unlike C#/Go/Python's reference copy)
UdpSocket a;              // a owns the socket
UdpSocket b = std::move(a);  // b now owns the socket, a is empty
// a.is_open() == false now

// Rust equivalent:
// let b = a;  // a is moved, can't use a anymore

// C# — classes are reference types, this just copies the reference:
// var b = a;  // both point to same object
```

### `setsockopt` Pattern

```cpp
// C++ — raw syscall with void pointer and size
int value = 50;
::setsockopt(fd_, SOL_SOCKET, SO_BUSY_POLL, &value, sizeof(value));

// Go equivalent:
// conn.SetReadBuffer(size)
// or raw: syscall.SetsockoptInt(fd, syscall.SOL_SOCKET, syscall.SO_BUSY_POLL, 50)

// Rust equivalent:
// socket.setsockopt(sockopt::BusyPoll, &50)?;

// C# equivalent:
// socket.SetSocketOption(SocketOptionLevel.Socket, SocketOptionName.ReceiveBuffer, size);

// Python equivalent:
// sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, size)
```

---

## Remaining Phase 1 Items

| Step | What | Difficulty | Lines |
|------|------|-----------|-------|
| 1.1 | Add `SO_BUSY_POLL` to `UdpSocket` | Easy | ~10 |
| 1.1a | Add `sendmmsg()` / `recvmmsg()` to `UdpSocket` | Medium | ~80 |
| 1.4 | Add `pre_touch()` to `MemoryMappedFile` | Easy | ~15 |
| 1.6 | Platform tests | Medium | ~200 |

---

## Step 1: `SO_BUSY_POLL` — Kernel Busy Polling

### The Problem

When a UDP socket has no data, `recvfrom()` puts the thread to sleep. When a packet arrives, the NIC raises an interrupt, the kernel schedules the thread, and it wakes up. This wake-up costs **1-5 microseconds**.

For a media driver handling thousands of messages per second, 5µs per packet is too slow.

### The Solution

`SO_BUSY_POLL` tells the kernel: "don't sleep — spin in a tight loop checking the NIC for packets for up to N microseconds before giving up."

```
Without SO_BUSY_POLL:
  recvfrom() → sleep → NIC interrupt → wake up → return data    (~5µs)

With SO_BUSY_POLL(50):
  recvfrom() → spin on NIC for up to 50µs → return data         (~0.5µs)
```

### Implementation

Add to `src/platform/posix/udp_socket.h`:

```cpp
/// Enable kernel busy polling. The kernel will spin for up to
/// `microseconds` checking the NIC before sleeping.
/// Typical value: 50-100. Set to 0 to disable.
void set_busy_poll(i32 microseconds);
```

Add to `src/platform/posix/udp_socket.cpp`:

```cpp
void UdpSocket::set_busy_poll(i32 microseconds)
{
    // setsockopt takes: fd, level, option_name, &value, sizeof(value)
    // - SOL_SOCKET = socket-level option (vs IPPROTO_IP for IP-level)
    // - SO_BUSY_POLL = the option (Linux-specific, since kernel 3.11)
    // - &microseconds = pointer to the value
    // - sizeof(...) = size of the value
    if (::setsockopt(fd_, SOL_SOCKET, SO_BUSY_POLL,
                     &microseconds, sizeof(microseconds)) < 0)
    {
        // Not fatal — some kernels don't support it. Log and continue.
        // In production, you'd check errno == ENOPROTOOPT.
    }
}
```

### Key C++ Concepts

```cpp
// &microseconds — "address-of" operator. Like & in Rust for references.
// setsockopt needs a pointer, not a value.

// sizeof(microseconds) — compile-time size of the type in bytes.
// Like std::mem::size_of::<i32>() in Rust, sizeof(int) in C#/Go.
// For i32, this is always 4.

// < 0 — POSIX syscalls return -1 on error, with errno set.
// Like Go's: if err != nil { return err }
// Like Rust's: if result.is_err() { ... }
// Like C#'s: try/catch (but POSIX uses return codes, not exceptions)
```

### When to Use It

In the Receiver agent's main loop, before polling for packets:

```cpp
// In the Receiver's initialization:
receiver_socket.set_busy_poll(50);  // 50µs busy poll
```

**Don't** set this on the DriverConductor's socket — it polls infrequently and busy-polling would waste CPU.

---

## Step 2: `pre_touch()` — Page Fault Prevention

### The Problem

When you `mmap()` a 64MB file, the kernel creates a **virtual** mapping but doesn't allocate **physical** memory. The first time you write to each 4KB page, a **page fault** occurs:

```
mmap(64MB)           → virtual mapping created (instant)
write to page 0      → page fault (~10µs) → physical page allocated
write to page 1      → page fault (~10µs) → physical page allocated
...
write to page 16383  → page fault (~10µs) → physical page allocated

Total: 16,384 page faults × 10µs = 163ms of scattered latency
```

For a media driver, you can't have a 10µs stall when sending the first message.

### The Solution

Touch every page right after `mmap()` to force the kernel to allocate physical memory upfront:

```
mmap(64MB)           → virtual mapping created
pre_touch()          → all pages allocated (~5ms, done once at startup)
write to page 0      → no page fault (already allocated)
```

### Implementation

Add to `src/platform/posix/mmap.h`:

```cpp
/// Pre-touch all pages to force physical allocation.
/// Call this after create_new() to avoid page faults on the hot path.
void pre_touch();
```

Add to `src/platform/posix/mmap.cpp`:

```cpp
void MemoryMappedFile::pre_touch()
{
    if (!addr_ || size_ <= 0) return;

    // Get the system page size (typically 4096 bytes)
    const auto page_size = static_cast<i64>(::sysconf(_SC_PAGESIZE));

    // Touch one byte per page to force allocation.
    // volatile prevents the compiler from optimizing this away.
    auto* p = static_cast<volatile std::byte*>(addr_);
    for (i64 offset = 0; offset < size_; offset += page_size)
    {
        p[offset] = std::byte{0};
    }
}
```

### Key C++ Concepts

```cpp
// static_cast<T>(x) — explicit type conversion. Like (T)x in C, T(x) in Go.
// Rust equivalent: x as T
// C# equivalent: (T)x
// This is the SAFE cast — it checks at compile time.

// volatile — tells the compiler "don't optimize this away."
// Without volatile, the compiler might see that p[offset] = 0 has no
// observable effect and remove the entire loop.
// Rust equivalent: std::ptr::write_volatile
// C# equivalent: volatile keyword
// Go: no direct equivalent (compiler is less aggressive)

// std::byte{0} — creates a byte with value 0.
// std::byte is like Rust's u8 or Go's byte, but strongly typed
// (you can't do arithmetic on it without explicit casts).

// ::sysconf(_SC_PAGESIZE) — POSIX function to get the system page size.
// Like Environment.SystemPageSize in C#, os.Getpagesize() in Go.
```

### When to Use It

After creating a log buffer:

```cpp
auto log_buffer = MemoryMappedFile::create_new("/tmp/aeron-log-123", 64 * 1024 * 1024);
log_buffer.pre_touch();  // force all pages into physical memory
// Now the log buffer is ready — no page faults during actual use
```

### Alternative: `madvise`

```cpp
// This is a HINT — the kernel may or may not allocate pages.
::madvise(addr_, static_cast<size_t>(size_), MADV_WILLNEED);

// For the log buffer hot path, we need the guarantee, so we touch.
// madvise is useful for "I might need this soon" (like prefetching).
```

---

## Step 3: `sendmmsg()` / `recvmmsg()` — Batch UDP I/O

### The Problem

Each `sendto()` syscall costs ~200-500ns (user→kernel→user transition). If the Sender has 10 DATA frames to send, that's 10 syscalls = 2-5µs wasted on overhead alone.

### The Solution

`sendmmsg()` sends multiple packets in one syscall:

```
10 packets with sendto():    10 syscalls × 300ns = 3000ns
10 packets with sendmmsg():   1 syscall  × 500ns =  500ns  (6x faster)
```

### How `sendmmsg` Works

```
struct mmsghdr {
    struct msghdr msg_hdr;   // what to send + where
    unsigned int  msg_len;   // bytes actually sent (output)
};

struct msghdr {
    void*         msg_name;        // destination sockaddr
    socklen_t     msg_namelen;     // size of sockaddr
    struct iovec* msg_iov;         // array of buffer segments
    size_t        msg_iovlen;      // number of segments
    // ... other fields (control, flags)
};

struct iovec {
    void*  iov_base;  // pointer to data
    size_t iov_len;   // length of data
};
```

Think of it as:
- `iovec` = one buffer segment (like a `ReadOnlySpan<byte>` in C#)
- `msghdr` = one message (destination + one or more segments)
- `mmsghdr` = one message + output bytes-sent count

### Implementation

Add to `src/platform/posix/udp_socket.h`:

```cpp
#include <vector>

/// A single message to send via send_mmsg().
struct SendMsg
{
    const void*  data;       // pointer to the packet data
    i32          length;     // packet length in bytes
    std::string  address;    // destination IP (e.g., "192.168.1.1")
    u16          port;       // destination port
};

/// A single receive buffer for recv_mmsg().
struct RecvMsg
{
    std::byte*   buffer;          // where to write received data
    i32          buffer_length;   // size of buffer
    i32          received_length; // actual bytes received (output)
    std::string  from_address;    // source IP (output)
    u16          from_port;       // source port (output)
};

/// Batch send multiple UDP packets in one syscall.
/// Returns the number of packets actually sent (may be less than messages.size()).
[[nodiscard]] i32 send_mmsg(const std::vector<SendMsg>& messages);

/// Batch receive multiple UDP packets in one syscall.
/// Returns the number of packets actually received.
[[nodiscard]] i32 recv_mmsg(std::vector<RecvMsg>& messages);
```

Add to `src/platform/posix/udp_socket.cpp`:

```cpp
#include <sys/socket.h>   // sendmmsg, recvmmsg
#include <netinet/in.h>   // sockaddr_in
#include <arpa/inet.h>    // inet_pton
#include <string.h>       // memset

i32 UdpSocket::send_mmsg(const std::vector<SendMsg>& messages)
{
    // We need temporary storage for the syscall's data structures.
    // std::vector manages heap memory automatically (like List<T> in C#,
    // Vec<T> in Rust, []T in Go).

    // Step 1: Build the sockaddr_in array (one per message).
    // Each sockaddr_in describes where to send the packet.
    std::vector<struct sockaddr_in> addresses(messages.size());
    // Step 2: Build the iovec array (one per message).
    // Each iovec points to the packet data.
    std::vector<struct iovec> iovecs(messages.size());
    // Step 3: Build the mmsghdr array (one per message).
    // Each mmsghdr combines a sockaddr + iovec + output length.
    std::vector<struct mmsghdr> mmsghdrs(messages.size());

    for (size_t i = 0; i < messages.size(); ++i)
    {
        // Fill the destination address
        auto& addr = addresses[i];
        std::memset(&addr, 0, sizeof(addr));  // zero out (like {} in Rust)
        addr.sin_family = AF_INET;
        addr.sin_port = htons(messages[i].port);  // host to network byte order
        ::inet_pton(AF_INET, messages[i].address.c_str(), &addr.sin_addr);

        // Fill the iovec (pointer to data + length)
        iovecs[i].iov_base = const_cast<void*>(messages[i].data);
        iovecs[i].iov_len  = static_cast<size_t>(messages[i].length);

        // Fill the mmsghdr (link address + iovec)
        auto& hdr = mmsghdrs[i];
        std::memset(&hdr, 0, sizeof(hdr));
        hdr.msg_hdr.msg_name    = &addresses[i];
        hdr.msg_hdr.msg_namelen = sizeof(addresses[i]);
        hdr.msg_hdr.msg_iov     = &iovecs[i];
        hdr.msg_hdr.msg_iovlen  = 1;  // one iovec per message
    }

    // Step 4: Call sendmmsg.
    // Returns number of messages sent, or -1 on error.
    auto sent = ::sendmmsg(fd_, mmsghdrs.data(),
                           static_cast<unsigned>(messages.size()), 0);
    return static_cast<i32>(sent);
}

i32 UdpSocket::recv_mmsg(std::vector<RecvMsg>& messages)
{
    // Similar structure: build arrays, call recvmmsg, extract results.

    std::vector<struct sockaddr_in> addresses(messages.size());
    std::vector<struct iovec> iovecs(messages.size());
    std::vector<struct mmsghdr> mmsghdrs(messages.size());

    for (size_t i = 0; i < messages.size(); ++i)
    {
        // iovec points to the receive buffer
        iovecs[i].iov_base = messages[i].buffer;
        iovecs[i].iov_len  = static_cast<size_t>(messages[i].buffer_length);

        // mmsghdr links the iovec + space for source address
        auto& hdr = mmsghdrs[i];
        std::memset(&hdr, 0, sizeof(hdr));
        hdr.msg_hdr.msg_name    = &addresses[i];
        hdr.msg_hdr.msg_namelen = sizeof(addresses[i]);
        hdr.msg_hdr.msg_iov     = &iovecs[i];
        hdr.msg_hdr.msg_iovlen  = 1;
    }

    // MSG_DONTWAIT = non-blocking (like Rust's WouldBlock, Go's net.Error.Timeout())
    auto received = ::recvmmsg(fd_, mmsghdrs.data(),
                               static_cast<unsigned>(messages.size()),
                               MSG_DONTWAIT, nullptr);
    if (received < 0)
        return static_cast<i32>(received);

    // Step 5: Extract results — fill in received_length, from_address, from_port.
    for (ssize_t i = 0; i < received; ++i)
    {
        messages[i].received_length = static_cast<i32>(mmsghdrs[i].msg_len);

        char ip[INET_ADDRSTRLEN];
        ::inet_ntop(AF_INET, &addresses[i].sin_addr, ip, sizeof(ip));
        messages[i].from_address = ip;
        messages[i].from_port = ntohs(addresses[i].sin_port);
    }

    return static_cast<i32>(received);
}
```

### Key C++ Concepts in This Code

```cpp
// std::vector<T> — like List<T> in C#, Vec<T> in Rust, []T slice in Go.
// Automatically manages heap memory. Grows as needed.
std::vector<struct iovec> iovecs(messages.size());
// Creates a vector with `messages.size()` default-initialized elements.

// std::memset(&addr, 0, sizeof(addr)) — zero out memory.
// Like Rust's: let addr: sockaddr_in = unsafe { std::mem::zeroed() };
// Like C#: new sockaddr_in() (structs are zero-initialized)
// Like Go: addr := sockaddr_in{} (zero value)

// const_cast<void*>(messages[i].data) — removes const qualifier.
// The syscall takes void* (not const void*), but our data is const.
// This is safe because sendmmsg only READS from the buffer.
// Rust equivalent: data.as_ptr() as *mut c_void

// static_cast<size_t>(messages[i].length) — safe numeric cast.
// i32 → size_t (unsigned). Like `as usize` in Rust, `(size_t)x` in C.

// .data() — returns a pointer to the vector's internal array.
// Like &vec[0] or vec.as_ptr() in Rust, &slice[0] in Go.

// ssize_t — signed size_t. The kernel returns -1 for errors.
// Like isize in Rust, int in Go (when used with sizes).
```

### How the Sender Uses This

```cpp
// In the Sender's doWork() loop:
void Sender::doWork()
{
    // 1. Collect all pending DATA frames from all publications
    std::vector<SendMsg> batch;
    for (auto& pub : publications_)
    {
        pub.collect_pending_frames(batch);  // fills the batch
    }

    // 2. Send them all in one syscall
    if (!batch.empty())
    {
        socket_.send_mmsg(batch);
    }
}
```

### How the Receiver Uses This

```cpp
// In the Receiver's doWork() loop:
void Receiver::doWork()
{
    // 1. Prepare receive buffers (pre-allocated, reused each iteration)
    std::vector<RecvMsg> batch(max_messages_per_poll_);

    // 2. Receive all pending packets in one syscall
    i32 received = socket_.recv_mmsg(batch);

    // 3. Dispatch each packet to the correct PublicationImage
    for (i32 i = 0; i < received; ++i)
    {
        data_packet_dispatcher_.on_message(
            batch[i].buffer,
            batch[i].received_length,
            batch[i].from_address,
            batch[i].from_port
        );
    }
}
```

---

## Step 4: Platform Tests

### Test Structure

Tests live under `tests/platform/`. Each platform component gets its own test file:

```
tests/
  platform/
    mmap_test.cpp
    udp_socket_test.cpp
    epoll_poller_test.cpp
    thread_test.cpp
    clock_test.cpp
```

### Test Patterns

```cpp
#include <gtest/gtest.h>
#include "platform/posix/udp_socket.h"

// TEST(suite_name, test_name) — like [Fact] in C#, #[test] in Rust,
// func TestXxx(t *testing.T) in Go, def test_xxx() in pytest.
TEST(UdpSocket, BindAndSendReceive)
{
    // Arrange — create two sockets
    caeron::platform::UdpSocket sender;
    caeron::platform::UdpSocket receiver;

    receiver.bind("127.0.0.1", 0);  // port 0 = let OS pick a free port
    // To get the actual port, you'd need getsockname().
    // For testing, use a known port or a helper that picks one.

    // Act — send a message
    const char* msg = "hello";
    sender.send_to(msg, 5, "127.0.0.1", receiver_port);

    // Assert — receive it
    char buf[64];
    std::string from_addr;
    u16 from_port;
    auto n = receiver.receive_from(buf, sizeof(buf), from_addr, from_port);
    EXPECT_EQ(n, 5);
    EXPECT_EQ(std::string(buf, 5), "hello");
}

// EXPECT_EQ(a, b) — like assert_eq! in Rust, Assert.Equal in C#,
// if a != b { t.Errorf(...) } in Go, assert a == b in Python.

// EXPECT_THROW(expr, exception_type) — like #[should_panic] in Rust,
// [ExpectedException] in C#, assert.Panics in Go.
TEST(UdpSocket, BindToInvalidAddressThrows)
{
    caeron::platform::UdpSocket sock;
    EXPECT_THROW(sock.bind("invalid", 9999), std::runtime_error);
}
```

### Testing `SO_BUSY_POLL`

```cpp
TEST(UdpSocket, SetBusyPoll)
{
    caeron::platform::UdpSocket sock;
    // Should not throw — just sets a socket option
    EXPECT_NO_THROW(sock.set_busy_poll(50));

    // Verify the option was set (optional, for thoroughness)
    int value = 0;
    socklen_t len = sizeof(value);
    ::getsockopt(sock.fd(), SOL_SOCKET, SO_BUSY_POLL, &value, &len);
    EXPECT_EQ(value, 50);
}
```

### Testing `pre_touch()`

```cpp
TEST(MemoryMappedFile, PreTouch)
{
    auto mmap = caeron::platform::MemoryMappedFile::create_new("/tmp/caeron-test-pretouch", 4096);
    mmap.pre_touch();

    // Verify we can write to every page without crash
    auto sp = mmap.span();
    for (size_t i = 0; i < sp.size(); i += 4096)
    {
        sp[i] = std::byte{0xFF};
    }
    EXPECT_EQ(sp[0], std::byte{0xFF});
}
```

### Testing `send_mmsg` / `recv_mmsg`

```cpp
TEST(UdpSocket, BatchSendReceive)
{
    caeron::platform::UdpSocket sender;
    caeron::platform::UdpSocket receiver;
    receiver.bind("127.0.0.1", 0);
    auto port = /* get actual port */;

    // Send 3 messages in one syscall
    std::vector<caeron::platform::SendMsg> batch = {
        {"msg1", 4, "127.0.0.1", port},
        {"msg2", 4, "127.0.0.1", port},
        {"msg3", 4, "127.0.0.1", port},
    };
    auto sent = sender.send_mmsg(batch);
    EXPECT_EQ(sent, 3);

    // Receive them
    char buf1[64], buf2[64], buf3[64];
    std::vector<caeron::platform::RecvMsg> recv_batch = {
        {reinterpret_cast<std::byte*>(buf1), 64, 0, "", 0},
        {reinterpret_cast<std::byte*>(buf2), 64, 0, "", 0},
        {reinterpret_cast<std::byte*>(buf3), 64, 0, "", 0},
    };
    auto received = receiver.recv_mmsg(recv_batch);
    EXPECT_EQ(received, 3);
    EXPECT_EQ(recv_batch[0].received_length, 4);
}
```

### Building and Running Tests

```bash
# From the project root:
cmake -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

---

## Summary: What to Implement

1. **`UdpSocket::set_busy_poll()`** — 1 `setsockopt` call, ~10 lines
2. **`MemoryMappedFile::pre_touch()`** — 1 loop over pages, ~15 lines
3. **`UdpSocket::send_mmsg()`** — build arrays + 1 syscall, ~50 lines
4. **`UdpSocket::recv_mmsg()`** — build arrays + 1 syscall + extract results, ~50 lines
5. **Tests** — one test file per component, ~200 lines total

Order: `set_busy_poll` → `pre_touch` → `send_mmsg` → `recv_mmsg` → tests.

Each step builds on the previous one's patterns (setsockopt, POSIX conventions, array building).
