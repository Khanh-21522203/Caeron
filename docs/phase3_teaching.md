# Phase 3: Log Buffer Operations — Teaching Guide

## What You'll Learn

- Aeron's tri-partition log buffer architecture (term rotation, gap handling)
- Frame-level buffer manipulation with release-acquire publication
- Padding frames: how Aeron handles partial writes and wrap-around
- Zero-copy message writing via BufferClaim
- Gap detection and filling for reliable delivery (NAK-based retransmission)
- Publisher unblocking: recovering from crashed/partial writers

---

## 1. Existing Code Reference

All log buffer files live in `src/caeron/logbuffer/`. Four files are already implemented.

### 1.1 `FrameDescriptor` (frame_descriptor.h)

Describes the layout of a single frame within a term buffer. Every frame is 32-byte aligned.

```
Frame layout (32 bytes minimum):
[0, 4)   - frame_length (i32) — negative means padding frame
[4, 5)   - version (u8) — always 0
[5, 6)   - flags (u8) — BEGIN_FRAG_FLAG | END_FRAG_FLAG = UNFRAGMENTED
[6, 8)   - type (u16) — 0x01 = DATA, 0x02 = PAD
[8, 12)  - term_offset (i32)
[12, 16) - session_id (i32)
[16, 20) - stream_id (i32)
[20, 24) - term_id (i32)
[24, 32) - reserved (i64) — for application use
```

```cpp
struct FrameDescriptor {
    static constexpr i32 FRAME_ALIGNMENT    = 32;
    static constexpr i32 MAX_MESSAGE_LENGTH = 16 * 1024 * 1024; // 16 MB

    static constexpr u8 BEGIN_FRAG_FLAG = 0x80;
    static constexpr u8 END_FRAG_FLAG   = 0x40;
    static constexpr u8 UNFRAGMENTED    = BEGIN_FRAG_FLAG | END_FRAG_FLAG;

    static constexpr i32 FRAME_LENGTH_FIELD_OFFSET = 0;
    static constexpr i32 VERSION_FIELD_OFFSET      = 4;
    static constexpr i32 FLAGS_FIELD_OFFSET         = 5;
    static constexpr i32 TYPE_FIELD_OFFSET          = 6;

    // Read frame length (volatile = prevents compiler reordering)
    static i32 frame_length(const UnsafeBuffer& buffer, i32 offset);

    // Read frame type
    static u16 frame_type(const UnsafeBuffer& buffer, i32 offset);

    // Check if padding frame (length < 0)
    static bool is_padding_frame(const UnsafeBuffer& buffer, i32 offset);
};
```

**Cross-language mapping:**

| C++ | Java | Rust | C# |
|-----|------|------|-----|
| `frame_length(buf, off)` | `frameLengthVolatile(buf, off)` | `buf.get_i32_volatile(off)` | `Volatile.Read(ref buf[off])` |
| `is_padding_frame(buf, off)` | `isPaddingFrame(buf, off)` | `frame_length < 0` | `frameLength < 0` |

**Key insight:** Aeron uses negative frame length to indicate padding. This is an optimization — a single integer comparison detects both "unpublished" (length == 0) and "padding" (length < 0) states.

### 1.2 `Header` (header.h)

Metadata passed to fragment handlers when a frame is read.

```cpp
struct Header {
    const UnsafeBuffer& buffer;  // the term buffer
    i32 offset;                  // frame offset within the term
    i32 session_id;
    i32 stream_id;
    i32 term_id;
    i64 position;                // absolute position in the stream
};
```

### 1.3 `HeaderWriter` (header_writer.h / header_writer.cpp)

Writes a default DATA frame header into a term buffer. Used when pre-filling headers during publication.

```cpp
void write_default_header(
    UnsafeBuffer& buffer,
    i32 term_offset,     // where in the term buffer
    i32 frame_length,    // total frame size (header + payload)
    i32 session_id,
    i32 stream_id,
    i32 term_id);
```

Writes the full 32-byte header: frame_length, version=0, flags=UNFRAGMENTED, type=DATA(0x01), term_offset, session_id, stream_id, term_id.

### 1.4 `LogBufferDescriptor` (log_buffer_descriptor.h / log_buffer_descriptor.cpp)

Describes the tri-partition log buffer metadata layout.

```
Log buffer memory layout:
+-------------------+
| Term Buffer 0     |  term_length bytes
+-------------------+
| Term Buffer 1     |  term_length bytes
+-------------------+
| Term Buffer 2     |  term_length bytes
+-------------------+
| Metadata Buffer   |  4 KB (LOG_META_DATA_LENGTH)
+-------------------+

Metadata buffer layout:
[0, 8)    - tail_counter_0 (i64: packed term_offset << 32 | term_id)
[8, 16)   - tail_counter_1
[16, 24)  - tail_counter_2
[24, 28)  - active_term_count (i32)
[128, 136) - end_of_stream_position (i64)
[256, ..)  - log metadata section
[264, 268) - initial_term_id (i32)
[276, 280) - term_length (i32)
[320, ..)  - default_frame_header (128 bytes)
```

```cpp
struct LogBufferDescriptor {
    static constexpr i32 PARTITION_COUNT = 3;

    // Packing/unpacking tail counters
    static i64 pack_tail(i32 term_offset, i32 term_id);
    static i32 tail_term_id(i64 tail);
    static i32 tail_term_offset(i64 tail);

    // Partition index from term count
    static i32 index_by_term_count(i32 term_count);

    // Absolute position computation
    static i64 compute_position(i32 active_term_id, i32 term_offset,
                                i32 position_bits_to_shift, i32 initial_term_id);
    static i32 position_bits_to_shift(i32 term_length);
};
```

**Cross-language mapping:**

| C++ | Java | Rust | C# |
|-----|------|------|-----|
| `pack_tail(offset, id)` | `packTail(offset, id)` | `(offset as i64) << 32 \| id as i64` | `((long)offset << 32) \| (long)id` |
| `position_bits_to_shift(len)` | `positionBitsToShift(len)` | `len.trailing_zeros()` | `BitOperations.TrailingZeroCount(len)` |

---

## 2. What Needs to Be Implemented

### 2.1 Foundation Enhancements

Before implementing the 9 classes, two existing files need new helpers.

#### `frame_descriptor.h` — Add Write Helpers

```cpp
// Write frame length with release semantics (publishes the frame to readers)
static void frame_length_ordered(UnsafeBuffer& buffer, i32 offset, i32 frame_length);

// Write frame type
static void frame_type(UnsafeBuffer& buffer, i32 offset, u16 type);

// Write term offset field
static void frame_term_offset(UnsafeBuffer& buffer, i32 offset, i32 term_offset);

// Write term ID field
static void frame_term_id(UnsafeBuffer& buffer, i32 offset, i32 term_id);
```

#### `log_buffer_descriptor.h` — Add Metadata Accessors

```cpp
// Copy the default 32-byte header from metadata into a term buffer
static void apply_default_header(UnsafeBuffer& metadata_buffer,
                                  UnsafeBuffer& term_buffer, i32 term_offset);

// Read a packed tail counter with volatile semantics
static i64 raw_tail_volatile(UnsafeBuffer& metadata_buffer, i32 partition_index);

// Read the active term count
static i32 active_term_count(UnsafeBuffer& metadata_buffer);

// Rotate the log to the next term (CAS-based)
static void rotate_log(UnsafeBuffer& metadata_buffer, i32 current_term_count, i32 new_term_id);
```

---

### 2.2 `BufferClaim` — Zero-Copy Message Writing

**Purpose:** Represents a claimed region in the term buffer. The publisher writes payload directly into the buffer, then calls `commit()` to publish.

**When to use:** Instead of copying data into the log buffer, the publisher claims space, writes in-place, and commits. This avoids one memory copy.

**Lifecycle:**
```
1. Publisher claims N bytes → frame_length is 0 (unpublished)
2. Publisher writes payload directly into the buffer
3. Publisher calls commit() → frame_length written with release semantics
4. Reader sees frame_length > 0 → reads the frame
```

**API:**
```cpp
class BufferClaim {
    // Wrap a claimed region
    void wrap(UnsafeBuffer& buffer, i32 offset, i32 capacity);

    // Access the payload area (past the 32-byte header)
    UnsafeBuffer& buffer();
    i32 offset();      // always HEADER_LENGTH (32)
    i32 length();      // capacity - HEADER_LENGTH

    // Read/write frame fields
    u16 header_type();
    void header_type(u16 type);
    u8 flags();
    void flags(u8 flags);
    i64 reserved_value();
    void reserved_value(i64 value);

    // Copy data into the payload area
    void put_bytes(const UnsafeBuffer& src, i32 src_offset, i32 length);

    // Publish the frame (writes frame_length with release)
    void commit();

    // Abort — mark as padding so the log can progress
    void abort();
};
```

**Key pattern — release on commit:**
```cpp
void BufferClaim::commit() {
    // Write the frame length LAST with release semantics.
    // This ensures all prior writes (payload, header fields) are
    // visible to any thread that reads frame_length > 0.
    buffer_.put_i32_ordered(offset_ + FRAME_LENGTH_FIELD_OFFSET, capacity_);
}
```

**Abort pattern:**
```cpp
void BufferClaim::abort() {
    // Mark as padding — readers will skip this frame.
    buffer_.put_u16(offset_ + TYPE_FIELD_OFFSET, HDR_TYPE_PAD);
    buffer_.put_i32_ordered(offset_ + FRAME_LENGTH_FIELD_OFFSET, capacity_);
}
```

**Cross-language:**
- **Java:** `BufferClaim` in `io.aeron.logbuffer`
- **Rust:** Would be a struct wrapping a `&mut [u8]` slice with offset/length
- **C#:** `BufferClaim` with `Span<byte>` for payload access

---

### 2.3 `TermRebuilder` — Insert Out-of-Order Frames

**Purpose:** When the receiver gets frames out of order (due to network reordering), `TermRebuilder` inserts each frame at the correct position in the term buffer.

**When to use:** The receiver calls `TermRebuilder::insert()` for each received DATA frame.

**Algorithm:**
```
1. Check if the slot at term_offset is empty (frame_length == 0)
2. If not empty → frame already received (duplicate), skip
3. If empty:
   a. Copy payload (bytes from HEADER_LENGTH onward)
   b. Write header fields (term_id, stream_id, session_id) at their offsets
   c. Write frame_length LAST with release semantics
```

**Why write frame_length last?**
```
Without release:
  Writer: [payload] [header] [frame_length]  ← compiler/CPU may reorder
  Reader: sees frame_length > 0, reads incomplete header → CORRUPTION

With release:
  Writer: [payload] [header] ──release──→ [frame_length]
  Reader: frame_length > 0 ──acquire──→ sees complete header + payload
```

**API:**
```cpp
namespace TermRebuilder {
    // Insert a received packet into the term buffer at term_offset.
    // Only writes if the slot is empty (idempotent for duplicates).
    void insert(UnsafeBuffer& term_buffer, i32 term_offset,
                const UnsafeBuffer& packet, i32 length);
};
```

**Cross-language:**
- **Java:** `TermRebuilder.insert(termBuffer, termOffset, packet, length)`
- **Rust:** Same algorithm with `AtomicI32::store` with `Ordering::Release`
- **C#:** `Volatile.Write` for the frame length

---

### 2.4 `TermReader` — Read Frames with Callback

**Purpose:** Linear scan of a term buffer, invoking a callback for each complete fragment. Used by subscribers to read published messages.

**Algorithm:**
```
1. Start at term_offset
2. Read frame_length (volatile)
3. If frame_length <= 0 → stop (unpublished data)
4. If padding frame (length < 0) → skip, advance by aligned(-length)
5. Otherwise:
   a. Compute payload: offset + HEADER_LENGTH, length - HEADER_LENGTH
   b. Call handler.onFragment(payload, header)
   c. Advance by aligned(frame_length)
6. Repeat until fragments_limit reached or end of data
```

**API:**
```cpp
namespace TermReader {
    // Read up to fragments_limit frames starting at term_offset.
    // Returns packed (offset << 32 | fragments_read).
    template<typename FragmentHandler, typename ErrorHandler>
    i64 read(
        UnsafeBuffer& term_buffer,
        i32 term_offset,
        FragmentHandler&& handler,
        i32 fragments_limit,
        Header& header,
        ErrorHandler&& error_handler);
};
```

**Key detail — padding frames are skipped, not returned to the handler.** Padding frames exist to fill gaps when the publisher wraps around or when a gap is filled. The reader sees them as transparent.

---

### 2.5 `TermScanner` — Scan for Available Data

**Purpose:** Scan a term buffer to find how many contiguous bytes are available for sending. Used by the Sender agent to determine how much data can be batched into a single network packet.

**Algorithm:**
```
1. Start at offset
2. Read frame_length (volatile)
3. If frame_length <= 0 → stop (no more data)
4. Align to FRAME_ALIGNMENT
5. If padding frame: record padding size, advance by HEADER_LENGTH only
6. Accumulate available bytes
7. If available > maxLength:
   - If first frame: return negative available (data exists but too large)
   - Otherwise: return previous available (send what we have)
8. Repeat until limit reached or end of data
9. Return packed (padding << 32 | available)
```

**API:**
```cpp
namespace TermScanner {
    // Returns packed (padding << 32 | available).
    // Negative available means data exists but exceeds maxLength.
    i64 scan_for_availability(
        const UnsafeBuffer& term_buffer,
        i32 offset,
        i32 max_length);
};
```

---

### 2.6 `TermGapScanner` — Detect Missing Frames

**Purpose:** Scan a term buffer to find gaps in received data. When a gap is found, the caller sends a NAK (Negative Acknowledgment) to request retransmission.

**When to use:** The receiver periodically scans the term buffer. If frames are missing (holes between received frames), it generates NAKs.

**Algorithm:**
```
Phase 1 — Find end of contiguous data:
  Walk forward from term_offset, reading frame_length.
  If frame_length > 0: advance by aligned length.
  If frame_length == 0: found a gap → go to Phase 2.

Phase 2 — Find extent of gap:
  From gap_begin, walk forward in HEADER_LENGTH steps.
  If frame_length != 0: found the end of the gap.
  Gap length = current_offset - gap_begin.
  Call handler.onGap(term_id, gap_begin, gap_length).
```

**API:**
```cpp
struct GapHandler {
    virtual void on_gap(i32 term_id, i32 offset, i32 length) = 0;
};

namespace TermGapScanner {
    // Returns the offset of the last contiguous frame.
    i32 scan_for_gap(
        const UnsafeBuffer& term_buffer,
        i32 term_id,
        i32 term_offset,
        i32 limit_offset,
        GapHandler& handler);
};
```

---

### 2.7 `TermGapFiller` — Fill Gaps with Padding

**Purpose:** When a gap has been detected and NAKed but the data hasn't arrived in time, the gap is filled with a padding frame so the log can progress.

**Algorithm:**
```
1. Start at (gap_offset + gap_length) - FRAME_ALIGNMENT
2. Walk backwards to gap_offset in FRAME_ALIGNMENT steps
3. If any slot has non-zero data → data arrived, return false
4. If all slots are empty:
   a. Apply default header from metadata
   b. Set frame type to PAD
   c. Write term_offset and term_id
   d. Write frame_length with release semantics
   e. Return true
```

**Why walk backwards?** If data arrives concurrently while we're checking, walking backwards ensures we see the most recently written data first. If we find any non-zero frame, we abort.

**API:**
```cpp
namespace TermGapFiller {
    // Try to fill a gap with a padding frame.
    // Returns true if the gap was filled, false if data arrived.
    bool try_fill_gap(
        UnsafeBuffer& log_meta_data_buffer,
        UnsafeBuffer& term_buffer,
        i32 term_id,
        i32 gap_offset,
        i32 gap_length);
};
```

---

### 2.8 `TermBlockScanner` — Scan for Batch Processing

**Purpose:** Scan for a contiguous block of complete fragments for batch processing. Used by the Sender to collect multiple frames into a single network packet.

**Difference from TermScanner:** BlockScanner returns the end offset of a contiguous block. TermScanner returns byte counts.

**Algorithm:**
```
1. Start at term_offset
2. Read frame_length (volatile)
3. If frame_length <= 0 → stop
4. If padding frame:
   - If this is the first frame: advance past it (consume padding)
   - Otherwise: stop (padding terminates the block)
5. If next offset would exceed limit → stop
6. Advance by aligned(frame_length)
7. Return final offset
```

**API:**
```cpp
namespace TermBlockScanner {
    // Returns the offset past the last frame in the block.
    i32 scan(
        const UnsafeBuffer& term_buffer,
        i32 term_offset,
        i32 limit_offset);
};
```

---

### 2.9 `TermUnblocker` — Unblock Stuck Publishers

**Purpose:** If a publisher crashes or gets stuck mid-write, the term buffer has a partial frame (length < 0 or length == 0). The unblocker converts it to padding so the log can progress.

**When to use:** The conductor agent detects that the publication position hasn't advanced for a timeout period.

**Three cases:**

```
Case 1: frame_length < 0 (partially written frame)
  → The publisher wrote some bytes but didn't commit.
  → Convert to padding with length = -frame_length.
  → Status: UNBLOCKED

Case 2: frame_length == 0 (empty slot at blocked offset)
  → Scan forward looking for a non-zero frame.
  → If found: verify all slots between are zero, fill as padding.
  → Status: UNBLOCKED

Case 3: frame_length == 0 and all slots to end of term are zero
  → Fill to end of term as padding.
  → Status: UNBLOCKED_TO_END (triggers term rotation)

Case 4: frame_length > 0
  → Not blocked (frame is complete or already being processed).
  → Status: NO_ACTION
```

**API:**
```cpp
enum class UnblockStatus { NO_ACTION, UNBLOCKED, UNBLOCKED_TO_END };

namespace TermUnblocker {
    UnblockStatus unblock(
        UnsafeBuffer& log_meta_data_buffer,
        UnsafeBuffer& term_buffer,
        i32 blocked_offset,
        i32 tail_offset,
        i32 term_id);
};
```

---

### 2.10 `LogBufferUnblocker` — Unblock Across Partitions

**Purpose:** Higher-level wrapper that checks all three term partitions for stuck publishers. Converts an absolute position to a partition-local offset and delegates to `TermUnblocker`.

**Algorithm:**
```
1. Extract blocked_term_count and blocked_offset from blocked_position
2. Read active_term_count from metadata
3. If blocked position is at offset 0 of the next term:
   → Call rotate_log() to advance the log
4. Otherwise:
   → Read raw_tail for the blocked partition
   → Extract term_id and tail_offset
   → Delegate to TermUnblocker::unblock()
   → If UNBLOCKED_TO_END: call rotate_log()
5. Return whether unblocking occurred
```

**API:**
```cpp
namespace LogBufferUnblocker {
    bool unblock(
        UnsafeBuffer term_buffers[],    // array of 3 term buffers
        UnsafeBuffer& log_meta_data_buffer,
        i64 blocked_position,
        i32 term_length);
};
```

---

## 3. Key Concepts Explained

### 3.1 The Tri-Partition Log

Aeron uses three term buffers in rotation. At any time, one is "active" for writing:

```
Term 0: [████████████████░░░░░░░░░░]  ← written, being read
Term 1: [██████████░░░░░░░░░░░░░░░░]  ← active (publisher writes here)
Term 2: [░░░░░░░░░░░░░░░░░░░░░░░░░░]  ← next (will be rotated to)
```

When Term 1 fills up, the log rotates: Term 2 becomes active, Term 0 gets cleaned up.

**Why three?** One is being written, one is being read, one is being cleaned up. This avoids coordination between publisher and subscriber.

### 3.2 Padding Frames

A padding frame is a frame with `frame_length < 0` and `type = PAD`. It exists in two situations:

**Wrap-around padding:**
```
Before:
[... data ...][  empty space  ]
                ↑ publisher needs 100 bytes but only 80 remain

After:
[... data ...][ PAD (80 bytes) ][ data at offset 0 ...]
```

**Gap filling:**
```
Before:
[frame 1][frame 2][  empty  ][frame 5][frame 6]
                    ↑ gap (frames 3,4 lost)

After:
[frame 1][frame 2][ PAD (fill gap) ][frame 5][frame 6]
```

Readers skip padding frames transparently — they never reach the fragment handler.

### 3.3 Release-Acquire on frame_length

The `frame_length` field is the publication flag. Every writer follows this pattern:

```
1. Write payload        (plain writes)
2. Write header fields  (plain writes)
3. Write frame_length   (RELEASE) ← this publishes everything above
```

Every reader follows this pattern:

```
1. Read frame_length    (ACQUIRE) ← if > 0, we see everything below
2. Read header fields   (plain reads)
3. Read payload         (plain reads)
```

This is the same pattern used in Phase 2's ring buffer (`put_i32_ordered` / `get_i32_ordered`).

### 3.4 Frame Alignment

All frames are aligned to 32 bytes. This means:
- A 10-byte message occupies a 32-byte frame
- A 50-byte message occupies a 64-byte frame (2 × 32)
- The next frame starts at `current_offset + align(frame_length, 32)`

Alignment ensures frame headers are always at predictable offsets and enables efficient SIMD operations.

### 3.5 Position Tracking

Aeron tracks positions as absolute byte offsets in the stream (not partition-local):

```
position = (term_count × term_length) + term_offset

where:
  term_count = active_term_id - initial_term_id
  term_length = size of each term buffer
  term_offset = offset within the active term
```

The `position_bits_to_shift` is `log2(term_length)`, used for efficient bit-shift division.

### 3.6 Term Rotation

When the publisher fills a term, the log rotates:

```
Before rotation: active_term_count = 5, active partition = 5 % 3 = 2
After rotation:  active_term_count = 6, active partition = 6 % 3 = 0
```

Rotation uses CAS to ensure only one thread performs it:

```cpp
void rotate_log(UnsafeBuffer& metadata, i32 current_count, i32 new_term_id) {
    i32 new_count = current_count + 1;
    // CAS: only rotate if active_term_count is still what we expect
    metadata.compare_and_set_i32(ACTIVE_TERM_COUNT_OFFSET, current_count, new_count);
    // Write the new term's tail counter
    metadata.put_i64_ordered(tail_counter_offset, pack_tail(0, new_term_id));
}
```

---

## 4. Dependencies Between Classes

```
FrameDescriptor (foundation)
    ├── TermReader (reads frames)
    ├── TermScanner (scans frames)
    ├── TermBlockScanner (scans blocks)
    ├── TermGapScanner (finds gaps)
    ├── TermRebuilder (inserts frames)
    ├── TermGapFiller (fills gaps) ──── uses apply_default_header
    ├── TermUnblocker (unblocks)  ──── uses apply_default_header
    ├── BufferClaim (zero-copy write)
    └── LogBufferUnblocker ──────── uses TermUnblocker

LogBufferDescriptor (metadata)
    ├── TermGapFiller (reads default header)
    ├── TermUnblocker (reads default header)
    └── LogBufferUnblocker (reads tail counters, rotates log)
```

**Implementation order:**
1. Enhance `frame_descriptor.h` (write helpers)
2. Enhance `log_buffer_descriptor.h` (metadata accessors)
3. `BufferClaim` (standalone, no dependencies)
4. `TermRebuilder` (standalone)
5. `TermReader` (standalone)
6. `TermScanner` (standalone)
7. `TermGapScanner` (standalone)
8. `TermBlockScanner` (standalone)
9. `TermGapFiller` (depends on `apply_default_header`)
10. `TermUnblocker` (depends on `apply_default_header`)
11. `LogBufferUnblocker` (depends on `TermUnblocker`)

---

## 5. Summary: What to Implement

| # | File | Lines (est.) | Difficulty |
|---|------|-------------|------------|
| 1 | `frame_descriptor.h` — add write helpers | ~20 | Easy |
| 2 | `log_buffer_descriptor.h` — add metadata accessors | ~40 | Medium |
| 3 | `buffer_claim.h` + `.cpp` | ~80 | Easy |
| 4 | `term_rebuilder.h` + `.cpp` | ~40 | Easy |
| 5 | `term_reader.h` + `.cpp` | ~60 | Medium |
| 6 | `term_scanner.h` + `.cpp` | ~50 | Medium |
| 7 | `term_gap_scanner.h` + `.cpp` | ~60 | Medium |
| 8 | `term_gap_filler.h` + `.cpp` | ~60 | Medium |
| 9 | `term_block_scanner.h` + `.cpp` | ~40 | Easy |
| 10 | `term_unblocker.h` + `.cpp` | ~80 | Hard |
| 11 | `log_buffer_unblocker.h` + `.cpp` | ~60 | Medium |
| 12 | `tests/logbuffer/` — tests for all classes | ~400 | Important |

Total: ~990 lines of implementation + ~400 lines of tests.

**Most important deliverable:** The tests prove that release-acquire publication, gap filling, and unblocking work correctly under concurrent access.
