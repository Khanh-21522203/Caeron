# Phase 2: Concurrent Structures — Teaching Guide

## What You'll Learn

- Lock-free data structures: MPSC ring buffer, SPSC broadcast, MPSC linked queue, SPSC array queue
- C++ memory ordering: `memory_order_relaxed`, `acquire`, `release`, `seq_cst`
- CAS (Compare-And-Swap) loops for lock-free claims
- Cache-line padding to avoid false sharing
- Multi-threaded stress testing patterns

---

## 1. Existing Code Reference

All concurrent structures live in `src/caeron/concurrent/`. Every file has real, non-trivial implementation — none are stubs.

### 1.1 `UnsafeBuffer` (unsafe_buffer.h)

The foundation. A non-owning typed view over raw memory with atomic accessors.

```cpp
// Non-owning view over raw memory
class UnsafeBuffer {
    void* data_ = nullptr;
    i32   capacity_ = 0;

public:
    // Plain read/write (no ordering guarantees)
    i32 get_i32(i32 offset) const;
    void put_i32(i32 offset, i32 value);

    // Volatile read/write (prevents compiler reordering, no CPU barriers)
    i32 get_i32_volatile(i32 offset) const;
    void put_i32_volatile(i32 offset, i32 value);

    // Ordered read/write (acquire/release semantics — the important ones)
    i64 get_i64_ordered(i32 offset) const;   // load-acquire
    void put_i64_ordered(i32 offset, i64 value); // store-release
    i32 get_i32_ordered(i32 offset) const;
    void put_i32_ordered(i32 offset, i32 value);

    // CAS (Compare-And-Swap)
    bool compare_and_set_i64(i32 offset, i64 expected, i64 desired);

    // Atomic add
    i64 get_and_add_i64(i32 offset, i64 increment);

    // Bulk
    void put_bytes(i32 offset, const void* src, i32 length);
    void set_memory(i32 offset, i32 length, std::byte value);
};
```

**Cross-language mapping:**

| C++ | Rust | C# | Go | Python |
|-----|------|-----|-----|--------|
| `get_i32_ordered` (load-acquire) | `AtomicI32::load(Ordering::Acquire)` | `Volatile.Read` | `atomic.LoadInt32` | N/A (GIL) |
| `put_i32_ordered` (store-release) | `AtomicI32::store(val, Ordering::Release)` | `Volatile.Write` | `atomic.StoreInt32` | N/A |
| `compare_and_set_i64` | `AtomicI64::compare_exchange` | `Interlocked.CompareExchange` | `atomic.CompareAndSwapInt64` | N/A |
| `reinterpret_cast<atomic<T>*>` | `unsafe { &*(ptr as *const AtomicT) }` | `Unsafe.As` | `atomic.Value` | `ctypes` |

**Memory ordering cheat sheet:**

| Ordering | What it does | When to use |
|----------|-------------|-------------|
| `relaxed` | Atomic but no ordering with other operations | Counters that don't guard other data |
| `acquire` | Reads after this see writes before the matching release | Reading shared state published by another thread |
| `release` | Writes before this are visible to threads that acquire | Publishing shared state to another thread |
| `seq_cst` | Total global order (strongest, slowest) | Rarely needed in Aeron — acquire/release suffice |

### 1.2 `ManyToOneRingBuffer` (many_to_one_ring_buffer.h)

**Purpose:** Multiple producers (clients) write commands to one consumer (DriverConductor). Used for the to-driver command queue in the CnC file.

**Memory layout:**
```
[0, 8)    - head position (i64, consumer writes)
[8, 16)   - head cache line padding
[64, 128) - tail position (i64, producers CAS)
[128, ..) - data region (power-of-two size)
```

**Key pattern — CAS claim loop:**
```cpp
while (true) {
    tail = buffer.get_i64_ordered(TAIL_OFFSET);
    head = buffer.get_i64_ordered(HEAD_OFFSET);
    // calculate available space
    next_tail = tail + required;
    if (buffer.compare_and_set_i64(TAIL_OFFSET, tail, next_tail))
        break;  // we won the claim
    // another producer beat us — retry
}
// Now we own [tail, next_tail) — write our message
```

**API:**
```cpp
class ManyToOneRingBuffer {
    explicit ManyToOneRingBuffer(UnsafeBuffer buffer);
    i32 capacity() const;
    i32 size() const;           // approximate byte count
    bool write(i32 msg_type_id, const void* src, i32 length);
    template<typename Handler>
    i32 read(Handler&& handler); // single consumer
};
```

**Cross-language:**
- **Rust:** Same CAS loop with `AtomicI64::compare_exchange_weak` in a loop
- **C#:** `Interlocked.CompareExchange` on `long*`
- **Go:** `atomic.CompareAndSwapInt64`
- **Python:** Not practical — use `multiprocessing.Queue` or a C extension

### 1.3 `BroadcastTransmitter` / `BroadcastReceiver`

**Purpose:** One producer (DriverConductor) broadcasts responses to all consumers (clients). Used for the to-clients broadcast buffer in the CnC file.

**Memory layout:**
```
[0, 8)   - tail (i64, transmitter writes)
[8, 16)  - head (i64, receiver writes back)
[16, 64) - reserved
[64, ..) - data (power-of-two)
```

**Key difference from ring buffer:** No CAS needed — single producer. But the receiver must write `head` back so the transmitter knows available space.

**Bug fix applied:** The receiver now publishes consumed position:
```cpp
// In BroadcastReceiver::receive():
if (messages_received > 0)
    buffer_.put_i64_ordered(HEAD_POSITION_OFFSET, next_tail);
```

**API:**
```cpp
class BroadcastTransmitter {
    explicit BroadcastTransmitter(UnsafeBuffer buffer);
    bool transmit(i32 msg_type_id, const void* src, i32 length);
};

class BroadcastReceiver {
    explicit BroadcastReceiver(UnsafeBuffer buffer);
    template<typename Handler>
    i32 receive(Handler&& handler); // returns message count
};
```

### 1.4 `CountersManager` (counters_manager.h)

**Purpose:** Allocate/free counter slots in shared memory. Each counter is a 64-byte metadata slot + an i64 value.

**Memory layout (per counter):**
```
Metadata slot (64 bytes):
  [0, 4)  - state (i32: FREE=0, NOT_FREE=1)
  [4, 8)  - type_id (i32)
  [8, ..) - key + label (remaining bytes)

Value slot (8 bytes):
  [0, 8)  - counter value (i64)
```

**API:**
```cpp
class CountersManager {
    CountersManager(UnsafeBuffer metadata, UnsafeBuffer values, i32 slot_size = 64);
    i32 allocate(i32 type_id, const void* key, i32 key_len,
                 const char* label, i32 label_len);
    void free(i32 counter_id);
    i64& get_counter_value(i32 counter_id);
    void set_counter_value(i32 counter_id, i64 value);
    i32 get_type_id(i32 counter_id);
    template<typename Handler> void forEach(Handler&& handler);
    i32 max_counter_id() const;
    static i32 counter_offset(i32 counter_id, i32 slot_size = 64);
};
```

### 1.5 `AtomicCounter` (atomic_counter.h)

**Purpose:** Thin wrapper over a single counter value in the counters buffer.

```cpp
class AtomicCounter {
    AtomicCounter(i32 counter_id, UnsafeBuffer& values_buffer);
    i32 id() const;
    i64 get() const;
    void set(i64 value);
    void set_ordered(i64 value);
    i64 get_ordered() const;
    i64 get_and_add(i64 increment);
    i64 get_and_increment();
    void increment_ordered();
    bool compare_and_set(i64 expected, i64 desired);
};
```

### 1.6 `ManyToOneConcurrentLinkedQueue` (many_to_one_concurrent_linked_queue.h)

**Purpose:** Lock-free MPSC linked queue. Used internally by the driver for passing work between threads.

**Pattern:** Michael-Scott queue with sentinel node. Each node is heap-allocated.

```cpp
template<typename T>
class ManyToOneConcurrentLinkedQueue {
    void enqueue(T value);         // multiple producers
    bool try_dequeue(T& out);      // single consumer
    bool empty() const;
};
```

### 1.7 `OneToOneConcurrentArrayQueue` (one_to_one_concurrent_array_queue.h)

**Purpose:** Lock-free SPSC array-based circular queue. Cache-line-aligned head/tail to avoid false sharing.

```cpp
template<typename T>
class OneToOneConcurrentArrayQueue {
    explicit OneToOneConcurrentArrayQueue(i32 capacity);
    bool enqueue(T value);         // single producer
    bool try_dequeue(T& out);      // single consumer
    i32 size() const;
    bool empty() const;
    i32 capacity() const;
};
```

### 1.8 `UnsafeBufferPosition` (position.h)

**Purpose:** Wraps a single i64 slot for tracking a monotonically increasing byte offset (e.g., publication position, subscription position).

```cpp
class UnsafeBufferPosition {
    UnsafeBufferPosition(UnsafeBuffer buffer, i32 offset);
    i64 get() const;
    i64 get_volatile() const;
    i64 get_ordered() const;
    void set(i64 value);
    void set_ordered(i64 value);
    i64 get_and_add(i64 increment);
    void increment_ordered();
};
```

### 1.9 Cached Clocks (not yet implemented)

`CachedEpochClock` and `CachedNanoClock` are listed in Phase 2.9 but don't have files yet. They cache the current time for the duration of a duty cycle to avoid repeated `clock_gettime` syscalls.

---

## 2. Remaining Phase 2 Work

### 2.1 Hardening (Steps 2.1–2.9)

Most structures are functionally complete. The remaining work is:

| Structure | Work Needed | Difficulty |
|-----------|-------------|------------|
| `ManyToOneRingBuffer` | Add corrupt-record protection in `read()` (validate length doesn't exceed capacity) | Easy |
| `BroadcastTransmitter` | ✅ Bug fixed (`HEAD_POSITION_OFFSET`) | Done |
| `BroadcastReceiver` | ✅ Bug fixed (writes head back) | Done |
| `CountersManager` | Add `get_key()`, `get_label()` retrieval methods | Easy |
| `AtomicCounter` | Add bounds validation at construction | Easy |
| `OneToOneConcurrentArrayQueue` | Fix placement-new in `try_dequeue` (use destroy-only, don't reconstruct) | Medium |
| `UnsafeBuffer` | Add `get_and_add_i32` atomic | Easy |
| Cached clocks | Implement `CachedEpochClock`, `CachedNanoClock` | Easy |

### 2.2 Tests (Step 2.10)

Multi-threaded stress tests. This is the most important part of Phase 2.

**Test patterns:**

```cpp
// MPSC ring buffer stress test
TEST(ManyToOneRingBuffer, MultiProducerStress)
{
    // Setup: shared buffer in memory
    constexpr i32 BUFFER_SIZE = 1024 * 1024; // 1MB
    auto storage = std::make_unique<std::byte[]>(BUFFER_SIZE);
    UnsafeBuffer buffer{storage.get(), BUFFER_SIZE};
    ManyToOneRingBuffer ring{buffer};

    constexpr int NUM_PRODUCERS = 4;
    constexpr int MSGS_PER_PRODUCER = 10000;
    std::atomic<int> total_read{0};

    // Producer: each writes MSGS_PER_PRODUCER messages
    auto producer = [&](int id) {
        for (int i = 0; i < MSGS_PER_PRODUCER; ++i) {
            i32 msg = id * 100000 + i;
            while (!ring.write(1, &msg, sizeof(msg)))
                std::this_thread::yield(); // backpressure
        }
    };

    // Consumer: read all messages
    std::atomic<bool> producers_done{false};
    auto consumer = [&]() {
        while (!producers_done.load() || ring.size() > 0) {
            int n = ring.read([&](i32 type, const std::byte* data, i32 len) {
                total_read.fetch_add(1);
            });
            if (n == 0) std::this_thread::yield();
        }
    };

    // Launch
    std::vector<std::jthread> threads;
    for (int i = 0; i < NUM_PRODUCERS; ++i)
        threads.emplace_back(producer, i);
    threads.emplace_back(consumer);

    // Wait for producers
    for (int i = 0; i < NUM_PRODUCERS; ++i)
        threads[i].join();
    producers_done.store(true);
    threads.back().join();

    EXPECT_EQ(total_read.load(), NUM_PRODUCERS * MSGS_PER_PRODUCER);
}
```

**What to test for each structure:**

| Structure | Test |
|-----------|------|
| `ManyToOneRingBuffer` | N producers × M messages, verify all received, no duplicates, no corruption |
| `BroadcastTransmitter/Receiver` | 1 producer, N receivers, verify all receive all messages |
| `CountersManager` | N threads allocating/freeing concurrently, no ID collisions |
| `ManyToOneConcurrentLinkedQueue` | N producers × M enqueues, 1 consumer, verify all dequeued |
| `OneToOneConcurrentArrayQueue` | 1 producer, 1 consumer, verify FIFO order, no data loss |

---

## 3. Key Concepts Explained

### 3.1 CAS (Compare-And-Swap)

The atomic building block for lock-free algorithms:

```cpp
// Pseudocode:
bool cas(atomic<T>* addr, T expected, T desired) {
    // Atomically: if (*addr == expected) { *addr = desired; return true; }
    //             else { expected = *addr; return false; }
}

// In C++ (via UnsafeBuffer):
buffer.compare_and_set_i64(offset, expected_value, new_value);

// In Rust:
let result = atomic.compare_exchange(expected, desired,
    Ordering::Acquire, Ordering::Relaxed);

// In C#:
Interlocked.CompareExchange(ref *addr, desired, expected);

// In Go:
atomic.CompareAndSwapInt64(addr, expected, desired)
```

### 3.2 Cache-Line Padding (False Sharing)

When two threads write to different variables that share a cache line (64 bytes), the CPU bounces the cache line between cores — killing performance.

```cpp
// BAD: head and tail share a cache line
struct RingHeader {
    std::atomic<i64> head;  // consumer writes
    std::atomic<i64> tail;  // producers write
}; // 16 bytes — both on the same 64-byte cache line

// GOOD: padded to separate cache lines
struct RingHeader {
    alignas(64) std::atomic<i64> head;
    alignas(64) std::atomic<i64> tail;
}; // 128 bytes — each on its own cache line
```

In Aeron, the ring buffer header is 128 bytes: head on cache line 0, tail on cache line 1.

### 3.3 Release-Acquire Pattern

The core publish-consume pattern in Aeron:

```cpp
// Producer thread:
write_data(buffer, payload);           // plain writes
buffer.put_i32_ordered(offset, flag);  // store-release (publishes all prior writes)

// Consumer thread:
i32 flag = buffer.get_i32_ordered(offset); // load-acquire (sees all prior releases)
read_data(buffer, payload);                // safe — we acquired the flag
```

This is what makes the ring buffer work: the producer writes the message body first, then publishes it by writing the length field with release semantics. The consumer reads the length with acquire semantics, then safely reads the body.

### 3.4 Why Not mutex?

| | mutex | lock-free |
|-|-------|-----------|
| **Latency** | Unbounded (OS scheduling) | Bounded (CAS retry) |
| **Priority inversion** | Yes | No |
| **Thread failure** | Deadlock | Others continue |
| **Shared memory IPC** | Needs futex/robust mutex | Just atomic operations |
| **Complexity** | Simple | Hard (but Aeron handles it) |

Aeron uses shared memory (mmap'd CnC file) for client-driver IPC. Mutexes across process boundaries are fragile. Atomics on shared memory just work.

---

## 4. Common Patterns Across All Structures

### 4.1 Record Format

Both ring buffer and broadcast use the same record format:
```
[0, 4) - length (i32, written LAST with release semantics)
[4, 8) - message type ID (i32)
[8, 8+len) - message body
```

A negative length means **padding** (the reader skips to the start of the buffer).

### 4.2 Wrap-Around Padding

When a record would wrap past the end of the buffer:
1. Write a padding record (negative length = bytes remaining)
2. Write the actual record at offset 0
3. Advance tail by (padding + record)

### 4.3 Capacity Must Be Power-of-Two

All buffers require `capacity = 2^n`. This allows `tail & mask` instead of `tail % capacity` — a single AND instruction vs. an expensive division.

---

## 5. Summary: What to Implement

| # | Task | Difficulty |
|---|------|------------|
| 1 | `ManyToOneRingBuffer`: add corrupt-record length validation in `read()` | Easy |
| 2 | `CountersManager`: add `get_key()`, `get_label()` methods | Easy |
| 3 | `AtomicCounter`: validate counter_id at construction | Easy |
| 4 | `OneToOneConcurrentArrayQueue`: fix `try_dequeue` to not reconstruct | Medium |
| 5 | `UnsafeBuffer`: add `get_and_add_i32` | Easy |
| 6 | `CachedEpochClock` + `CachedNanoClock`: new files | Easy |
| 7 | `tests/concurrent/`: multi-threaded stress tests for all structures | **Important** |

The tests (item 7) are the most critical deliverable. They prove the lock-free algorithms are correct under contention.
