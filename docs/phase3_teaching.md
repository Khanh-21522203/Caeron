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

All log buffer files live in `src/caeron/logbuffer/`. All implementations are header-only (no .cpp files).

### 1.1 `FrameDescriptor` (frame_descriptor.h)

Describes the layout of a single frame within a term buffer. Every frame is 32-byte aligned.

```
Frame layout (32 bytes minimum):
[0, 4)   - frame_length (i32) — positive = published, 0 = empty, negative = in-progress
[4, 5)   - version (u8) — always 0
[5, 6)   - flags (u8) — BEGIN_FRAG_FLAG | END_FRAG_FLAG = UNFRAGMENTED (0xC0)
[6, 8)   - type (u16) — 0x00 = PAD, 0x01 = DATA
[8, 12)  - term_offset (i32)
[12, 16) - session_id (i32)
[16, 20) - stream_id (i32)
[20, 24) - term_id (i32)
[24, 32) - reserved_value (i64) — for application use
```

**Critical: frame_length semantics:**
- `frame_length > 0` — frame is published and ready to read
- `frame_length == 0` — slot is empty (unpublished)
- `frame_length < 0` — in-progress write (publisher's claim, NOT padding)

**Critical: padding detection:**
- Padding frames are identified by `type == HDR_TYPE_PAD` (0x00), NOT by negative frame_length.
- After TermGapFiller/TermUnblocker write padding, frame_length is **positive**.
- `is_padding_frame()` checks the type field, not the sign of frame_length.

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
    static constexpr i32 TERM_OFFSET_FIELD_OFFSET   = 8;
    static constexpr i32 SESSION_ID_FIELD_OFFSET    = 12;
    static constexpr i32 STREAM_ID_FIELD_OFFSET     = 16;
    static constexpr i32 TERM_ID_FIELD_OFFSET       = 20;

    // Read frame length with ACQUIRE semantics (pairs with frame_length_ordered release)
    static i32 frame_length(const UnsafeBuffer& buffer, i32 offset);

    // Read frame type
    static u16 frame_type(const UnsafeBuffer& buffer, i32 offset);

    // Check if padding frame (type == HDR_TYPE_PAD, NOT frame_length < 0)
    static bool is_padding_frame(const UnsafeBuffer& buffer, i32 offset);

    // Write helpers (used by TermGapFiller, TermUnblocker, BufferClaim)
    static void frame_length_ordered(UnsafeBuffer& buffer, i32 offset, i32 frame_length);
    static void frame_type(UnsafeBuffer& buffer, i32 offset, u16 type);
    static void frame_flags(UnsafeBuffer& buffer, i32 offset, u8 flags);
    static void frame_term_offset(UnsafeBuffer& buffer, i32 offset, i32 term_offset);
    static void frame_term_id(UnsafeBuffer& buffer, i32 offset, i32 term_id);
};
```

**Cross-language mapping:**

| C++ | Java | Rust | C# |
|-----|------|------|-----|
| `frame_length(buf, off)` | `frameLengthVolatile(buf, off)` | `buf.get_i32_acquire(off)` | `Volatile.Read(ref buf[off])` |
| `is_padding_frame(buf, off)` | `isPaddingFrame(buf, off)` | `frame_type(buf, off) == HDR_TYPE_PAD` | `frameType(buf, off) == HDR_TYPE_PAD` |
| `frame_length_ordered(buf, off, v)` | `frameLengthOrdered(buf, off, v)` | `buf.put_i32_release(off, v)` | `Volatile.Write(ref buf[off], v)` |

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

### 1.3 `HeaderWriter` (header_writer.h)

Writes a default DATA frame header into a term buffer. Header-only (inline).

```cpp
inline void write_default_header(
    UnsafeBuffer& buffer,
    i32 term_offset,     // where in the term buffer
    i32 frame_length,    // total frame size (header + payload)
    i32 session_id,
    i32 stream_id,
    i32 term_id);
```

Writes the full 32-byte header: frame_length, version=0, flags=UNFRAGMENTED, type=DATA(0x01), term_offset, session_id, stream_id, term_id.

### 1.4 `LogBufferDescriptor` (log_buffer_descriptor.h)

Describes the tri-partition log buffer metadata layout. Header-only (inline/constexpr).

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

    // Metadata accessors (acquire semantics)
    static i32 active_term_count(UnsafeBuffer& metadata_buffer);
    static i64 raw_tail_volatile(UnsafeBuffer& metadata_buffer, i32 partition_index);

    // Copy default header from metadata to term buffer
    static void apply_default_header(UnsafeBuffer& metadata_buffer,
                                     UnsafeBuffer& term_buffer, i32 term_offset);

    // Rotate log (CAS-based). Returns true if rotation succeeded.
    static bool rotate_log(UnsafeBuffer& metadata_buffer, i32 term_count, i32 term_id);

    // Absolute position computation
    static i64 compute_position(i32 active_term_id, i32 term_offset,
                                i32 position_bits_to_shift, i32 initial_term_id);
    static i32 position_bits_to_shift(i32 term_length);
};
```

**rotate_log semantics:**
- Expects the **current** term's ID (not the next partition's ID).
- Returns `false` without advancing `ACTIVE_TERM_COUNT` if the next partition's tail doesn't match `expected_term_id`.
- Uses CAS for both the tail counter update and the active_term_count advance.

---

## 2. Implemented Classes

### 2.1 `BufferClaim` — Zero-Copy Message Writing

**Purpose:** Represents a claimed region in the term buffer. The publisher writes payload directly, then calls `commit()` to publish.

**Lifecycle:**
```
1. Publisher claims N bytes → frame_length is 0 (unpublished)
2. Publisher writes payload directly into the buffer
3. Publisher calls commit() → frame_length written with release semantics
4. Reader sees frame_length > 0 → reads the frame
```

**Key pattern — release on commit:**
```cpp
void BufferClaim::commit() {
    // Write the frame length LAST with release semantics.
    buffer_.put_i32_ordered(offset_ + FRAME_LENGTH_FIELD_OFFSET, capacity_);
}

void BufferClaim::abort() {
    // Mark as padding — readers will skip this frame.
    buffer_.put_u16(offset_ + TYPE_FIELD_OFFSET, HDR_TYPE_PAD);
    buffer_.put_i32_ordered(offset_ + FRAME_LENGTH_FIELD_OFFSET, capacity_);
}
```

### 2.2 `TermRebuilder` — Insert Out-of-Order Frames

**Purpose:** When the receiver gets frames out of order, `TermRebuilder` inserts each frame at the correct position.

**Critical:** Reads `frame_length` from the **packet header** (`packet.get_i32(0)`), NOT the total packet length. A network packet may contain multiple frames.

**Algorithm:**
```
1. Check if slot is empty (frame_length == 0). If not → skip (duplicate).
2. Copy payload (bytes from HEADER_LENGTH onward)
3. Write header fields: reserved_value(24), stream_id+term_id(16), term_offset+session_id(8), version+flags+type(4)
4. Write frame_length from packet[0] LAST with release semantics
```

### 2.3 `TermReader` — Read Frames with Callback

**Purpose:** Linear scan, invoking a callback for each complete fragment.

**Algorithm:**
```
1. Read frame_length (acquire)
2. frame_length == 0 → stop (unpublished)
3. frame_length < 0 → stop (in-progress write)
4. Align to FRAME_ALIGNMENT
5. is_padding_frame? → skip, advance by aligned_length
6. Update header fields (offset, term_id, session_id, stream_id)
7. Call handler, advance by aligned_length
```

### 2.4 `TermScanner` — Scan for Available Data

**Purpose:** Find how many contiguous bytes are available for sending.

**Critical padding handling:**
- For padding frames: only `HEADER_LENGTH` (32) counts toward `available`.
- The remaining bytes (`aligned_length - HEADER_LENGTH`) go into `padding`.
- Padding always terminates the scan.
- Must check `max_length` before adding even the HEADER_LENGTH.

**Algorithm:**
```
1. Read frame_length (acquire)
2. frame_length == 0 or < 0 → stop
3. Align to FRAME_ALIGNMENT
4. is_padding_frame?
   → available += HEADER_LENGTH (check max_length first)
   → padding = aligned_length - HEADER_LENGTH
   → break (always terminates)
5. available + aligned_length > max_length?
   → If first frame: available = -(available + aligned_length)
   → Otherwise: break
6. available += aligned_length, advance
```

**Returns:** packed `(padding << 32) | available`. Negative available = data exists but too large.

### 2.5 `TermGapScanner` — Detect Missing Frames

**Purpose:** Find gaps in received data for NAK generation.

**Algorithm:**
```
Phase 1: Walk contiguous frames (frame_length > 0, advance by aligned length)
         Stop at frame_length == 0 (gap start) or < 0 (in-progress)

Phase 2: From gap_begin, walk forward by HEADER_LENGTH steps
         Find next non-zero frame_length → gap end
         Call handler.onGap(term_id, gap_begin, gap_length)
```

### 2.6 `TermGapFiller` — Fill Gaps with Padding

**Purpose:** Fill a verified gap with a padding frame so the log can progress.

**Validation:** Rejects zero, negative, or non-aligned `gap_length`.

**Algorithm:**
```
1. Validate gap_length > 0 and aligned
2. Walk backwards from gap_end to gap_start
3. If any slot has non-zero data → return false (data arrived)
4. Apply default header, set type=PAD, write term_offset, term_id
5. Write frame_length with release → return true
```

### 2.7 `TermBlockScanner` — Scan for Batch Processing

**Purpose:** Scan for a contiguous block of complete fragments.

**Critical padding handling:**
- Padding at start: returns `offset + HEADER_LENGTH` only (not the full aligned length).
- Padding mid-block: terminates before the padding.

### 2.8 `TermUnblocker` — Unblock Stuck Publishers

**Purpose:** Convert partial/empty frames to padding so the log can progress.

**Case 1: frame_length < 0 (partially written)**
```
1. CAS: frame_length from negative to abs_length (claim the frame)
2. If CAS fails → return NO_ACTION (publisher completed or another unblocker won)
3. Write padding header (default header, type=PAD, term_offset, term_id)
4. frame_length_ordered writes the final value with release
```

**Case 2: frame_length == 0 (empty slot)**
```
1. Scan forward bounded by tail_offset (NOT capacity)
2. If non-zero frame found: verify intermediate slots are zero
   - padding_length = current_offset - blocked_offset
   - If padding_length == 0 → NO_ACTION (concurrent publisher claimed the slot)
   - Otherwise: write padding header, return UNBLOCKED
3. If all slots to tail_offset are zero:
   - padding_length = tail_offset - blocked_offset
   - If padding_length == 0 → NO_ACTION
   - Otherwise: write padding header, return UNBLOCKED_TO_END
```

### 2.9 `LogBufferUnblocker` — Unblock Across Partitions

**Purpose:** Higher-level wrapper that converts absolute position to partition-local offset.

**Critical details:**
- Reads current term_id from the **current** partition for `rotate_log`.
- Propagates `rotate_log` return value (doesn't unconditionally return `true`).

---

## 3. Key Concepts

### 3.1 The Tri-Partition Log

```
Term 0: [████████████████░░░░░░░░░░]  ← written, being read
Term 1: [██████████░░░░░░░░░░░░░░░░]  ← active (publisher writes here)
Term 2: [░░░░░░░░░░░░░░░░░░░░░░░░░░]  ← next (will be rotated to)
```

### 3.2 Padding Frames

Padding frames have `type == HDR_TYPE_PAD` (0x00) and **positive** frame_length. They exist in two situations:

**Wrap-around padding:** Publisher needs 100 bytes but only 80 remain → write 80-byte PAD, continue at offset 0.

**Gap filling:** Frames 3,4 lost → TermGapFiller writes a PAD spanning the gap so subscribers can progress.

Readers skip padding frames transparently — they never reach the fragment handler.

### 3.3 Release-Acquire on frame_length

```
Writer: [payload] [header] ──release──→ [frame_length]
Reader: frame_length > 0 ──acquire──→ sees complete header + payload
```

- `frame_length()` uses `get_i32_ordered` (acquire)
- `frame_length_ordered()` uses `put_i32_ordered` (release)
- This ensures all header/payload writes are visible when frame_length > 0

### 3.4 Frame Alignment

All frames are aligned to 32 bytes:
- 10-byte message → 32-byte frame
- 50-byte message → 64-byte frame
- Next frame at `current_offset + align(frame_length, 32)`

### 3.5 Term Rotation

```
Before: active_term_count = 5, partition = 5 % 3 = 2
After:  active_term_count = 6, partition = 6 % 3 = 0
```

`rotate_log` uses CAS for both the tail counter and active_term_count. Returns `false` if the next partition's tail doesn't match `expected_term_id`.

---

## 4. Dependencies

```
FrameDescriptor (foundation)
    ├── TermReader, TermScanner, TermBlockScanner, TermGapScanner
    ├── TermRebuilder, TermGapFiller, TermUnblocker
    └── BufferClaim

LogBufferDescriptor (metadata)
    ├── TermGapFiller, TermUnblocker (apply_default_header)
    └── LogBufferUnblocker (tail counters, rotate_log)
```

---

## 5. Summary

| # | File | Purpose |
|---|------|---------|
| 1 | `frame_descriptor.h` | Frame layout, read/write helpers, padding detection |
| 2 | `log_buffer_descriptor.h` | Metadata layout, tail counters, rotation |
| 3 | `buffer_claim.h` | Zero-copy write claim with commit/abort |
| 4 | `term_rebuilder.h` | Insert out-of-order received frames |
| 5 | `term_reader.h` | Read frames with fragment callback |
| 6 | `term_scanner.h` | Scan available contiguous data |
| 7 | `term_gap_scanner.h` | Detect gaps for NAK generation |
| 8 | `term_gap_filler.h` | Fill gaps with padding frames |
| 9 | `term_block_scanner.h` | Scan contiguous blocks for batch processing |
| 10 | `term_unblocker.h` | Unblock stuck publishers |
| 11 | `log_buffer_unblocker.h` | Cross-partition unblocking |

All header-only. ~800 lines of implementation + ~500 lines of tests (44 tests).
