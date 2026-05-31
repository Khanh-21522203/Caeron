# Phase 4: Protocol Completion — Teaching Guide

## What You'll Learn

- Aeron's wire protocol frame layout and type system
- Flyweight pattern for zero-copy protocol parsing
- ERR frame: offending-header echo (ICMP-like) with variable-length error string
- SM frame: status messages with optional group tag (ASF validation)
- Resolution entries: name-to-address mapping with 4-byte alignment
- Defensive parsing: validating wire data before trusting stored lengths

---

## 1. Existing Code Reference

All protocol files live in `src/caeron/protocol/`. All implementations are header-only (no .cpp files).

### 1.1 `HeaderFlyweight` (header_flyweight.h)

Base header shared by all Aeron protocol frames (8 bytes).

```
Layout:
[0, 4)  - frame_length (i32)
[4, 5)  - version (u8) — CURRENT_VERSION = 0x0
[5, 6)  - flags (u8)
[6, 8)  - type (u16) — frame type identifier
```

**Frame type constants:**
```cpp
HDR_TYPE_PAD       = 0x00  // Padding frame
HDR_TYPE_DATA      = 0x01  // Data frame
HDR_TYPE_NAK       = 0x02  // Negative acknowledgement
HDR_TYPE_SM        = 0x03  // Status message
HDR_TYPE_ERR       = 0x04  // Error frame
HDR_TYPE_SETUP     = 0x05  // Setup frame (MDC)
HDR_TYPE_RTTM      = 0x06  // RTT measurement
HDR_TYPE_RES       = 0x07  // Resolution entry
HDR_TYPE_ATS_DATA  = 0x08  // ATS data
HDR_TYPE_ATS_SM    = 0x09  // ATS status message
HDR_TYPE_ATS_SETUP = 0x0A  // ATS setup
HDR_TYPE_RSP_SETUP = 0x0B  // Response setup (MDC)
HDR_TYPE_EXT       = 0xFFFF // Extension
```

### 1.2 `DataHeaderFlyweight` (data_header_flyweight.h)

DATA frame header (32 bytes). Used by publishers to transmit data.

```
Layout:
[0, 4)   - frame_length (i32)
[4, 5)   - version (u8)
[5, 6)   - flags (u8) — BEGIN_FLAG (0x80), END_FLAG (0x40), EOS_FLAG (0x20), REVOKED_FLAG (0x10)
[6, 8)   - type (u16) — HDR_TYPE_DATA (0x01)
[8, 12)  - term_offset (i32)
[12, 16) - session_id (i32)
[16, 20) - stream_id (i32)
[20, 24) - term_id (i32)
[24, 32) - reserved_value (i64)
```

**Flag semantics:**
- `BEGIN_FLAG | END_FLAG` = UNFRAGMENTED (0xC0) — complete message in one frame
- `EOS_FLAG` (0x20) — end of stream
- `REVOKED_FLAG` (0x10) — publication revoked

**Static helper methods:**
- `is_end_of_stream(buffer, offset)` — checks EOS flag
- `is_revoked(buffer, offset)` — checks REVOKED flag

### 1.3 `StatusMessageFlyweight` (status_message_flyweight.h)

SM frame (36 bytes, optional 44 bytes with group tag). Sent by receivers to report consumption position.

```
Layout:
[0, 4)   - frame_length (i32)
[4, 5)   - version (u8)
[5, 6)   - flags (u8) — SEND_SETUP_FLAG (0x80), END_OF_STREAM_FLAG (0x40)
[6, 8)   - type (u16) — HDR_TYPE_SM (0x03)
[8, 12)  - session_id (i32)
[12, 16) - stream_id (i32)
[16, 20) - consumption_term_id (i32)
[20, 24) - consumption_term_offset (i32)
[24, 28) - receiver_window (i32)
[28, 36) - receiver_id (i64)
[36, 44) - group_tag (i64) — optional, present when frame_length == 44
```

**Critical: ASF/group tag validation:**
- `frame_length == 36` → no group tag, `group_tag()` returns 0
- `frame_length == 44` → valid group tag present
- `frame_length` in (36, 44) or > 44 → **malformed**, `group_tag()` throws
- `asf_length()` = `frame_length - 36` — length of Application Specific Feedback

**Why not silently return 0 for short ASF?**
A Java Aeron driver throws `AeronException` for malformed ASF. Silently returning 0 would make invalid frames indistinguishable from "no group tag", hiding protocol errors.

### 1.4 `ErrorFlyweight` (error_flyweight.h)

ERR frame (variable length). Reports errors between driver and clients. Layout matches Java Aeron 1.47.0+ `ErrorFlyweight.java`.

```
Layout matching Java 1.47.0:
[0, 4)   - frame_length (i32)
[4, 5)   - version (u8)
[5, 6)   - flags (u8)
[6, 8)   - type (u16) — HDR_TYPE_ERR (0x04)
[8, 12)  - session_id (i32)
[12, 16) - stream_id (i32)
[16, 24) - receiver_id (i64)
[24, 32) - group_tag (i64)
[32, 36) - error_code (i32)
[36, 40) - error_string_length (i32)
[40, ..) - error_string (variable)
```

**Why Java layout, not transport spec?**
The transport spec wiki shows an older layout (1-byte error code at offset 5, offending header echo). Java 1.47.0+ uses a different layout with session/stream/receiver/group fields. Caeron matches Java for wire compatibility with current Aeron peers.

**Key fields:**
- `error_code()` — 4-byte error code at offset 32
- `session_id()`, `stream_id()`, `receiver_id()` — identify the stream/source
- `group_tag()` — optional group tag (set via `set_group_tag`, which also sets `HAS_GROUP_ID_FLAG`)
- `error_message()` — length-prefixed string at offset 40

### 1.5 `ResolutionEntryFlyweight` (resolution_entry_flyweight.h)

Resolution entry for name-to-address mapping. Used in Phase 7 (Name Resolution).

```
Layout (IPv4):
[0, 1)   - res_type (u8) — RES_TYPE_NAME_TO_IP4_MD (0x01)
[1, 2)   - flags (u8) — SELF_FLAG (0x80)
[2, 4)   - udp_port (u16)
[4, 8)   - age_in_ms (i32)
[8, 12)  - address (4 bytes for IPv4)
[12, 14) - name_length (i16)
[14, ..) - name (variable)
[.., ..) - padding to 4-byte boundary

Layout (IPv6):
[0, 1)   - res_type (u8) — RES_TYPE_NAME_TO_IP6_MD (0x02)
[1, 2)   - flags (u8)
[2, 4)   - udp_port (u16)
[4, 8)   - age_in_ms (i32)
[8, 24)  - address (16 bytes for IPv6)
[24, 26) - name_length (i16)
[26, ..) - name (variable)
[.., ..) - padding to 4-byte boundary
```

**Critical: Alignment is 8 bytes (SIZE_OF_LONG)!**
The transport spec says "Pad to 4 bytes," but the Java source uses `SIZE_OF_LONG` (8 bytes). Caeron matches Java for wire compatibility.

**Name length validation:**
- `put_name()` validates `name_length ∈ [0, MAX_NAME_LENGTH]` (512)
- `get_name()` validates stored i16 is in `[0, MAX_NAME_LENGTH]` before reading
- `entry_length()` validates stored i16 before computing alignment
- `entry_length_required()` validates input name_length

---

## 2. Key Design Decisions

### 2.1 Flyweight Pattern

All protocol classes use the flyweight pattern: they wrap a buffer + offset and provide typed accessors. No data copying or allocation.

```cpp
class DataHeaderFlyweight {
    concurrent::UnsafeBuffer& buffer_;
    i32 offset_;
public:
    [[nodiscard]] i32 session_id() const noexcept {
        return buffer_.get_i32(offset_ + SESSION_ID_FIELD_OFFSET);
    }
};
```

**Why flyweight?**
- Zero-copy: reads/writes directly in the network buffer
- Thread-safe: no shared mutable state between flyweight instances
- Composable: multiple flyweights can wrap the same buffer at different offsets

### 2.2 Wire Format vs Internal Format

The ERR frame taught us an important lesson: **the wire format is defined by the transport spec, not the Java source code.**

The Java `ErrorFlyweight.java` (since 1.47.0) has a different layout than the transport spec. When implementing a wire-compatible port, always check the spec first.

### 2.3 Defensive Parsing

Wire data is untrusted. Stored lengths must be validated before use:

1. **Negative values** — throw, don't clamp to 0
2. **Values exceeding max** — throw (e.g., error_message_length > MAX_ERROR_MESSAGE_LENGTH)
3. **Source range validation** — check `src_offset + length <= src.capacity()`

**Why not clamp?**
Clamping to 0 for negative lengths makes malformed frames look like "no data" instead of "corrupt data." This hides bugs and makes debugging harder.

### 2.4 Setter Simplicity

`set_error_message()` writes the length-prefixed string and updates `frame_length` in one call. No need to pre-seed `frame_length` or compute offsets manually.

```cpp
void set_error_message(const std::string& msg) {
    const auto len = static_cast<i32>(msg.size());
    buffer_.put_i32(offset_ + ERROR_STRING_FIELD_OFFSET, len);
    buffer_.put_bytes(offset_ + HEADER_LENGTH, msg.data(), len);
    set_frame_length(HEADER_LENGTH + len);
}
```

---

## 3. Common Pitfalls

### 3.1 Error Code Location

The ERR frame error code is at **offset 32** (4 bytes), matching Java 1.47.0. The transport spec wiki shows it at byte 5 (1 byte), but that's the old layout.

### 3.2 Group Tag ASF Validation

The SM frame's `group_tag()` must throw for ANY ASF length that isn't exactly 0 or 8. Frame lengths of 37-43 are malformed.

### 3.3 Resolution Entry Alignment

Resolution entries are padded to **8 bytes** (SIZE_OF_LONG), matching the Java source. The transport spec says 4 bytes, but Java uses 8 for wire compatibility.

### 3.4 `[[nodiscard]]` and `EXPECT_THROW`

GTest's `EXPECT_THROW` discards the return value, triggering `[[nodiscard]]` warnings. Use:
```cpp
EXPECT_THROW(
    { [[maybe_unused]] i32 v = err.error_string_offset(); },
    std::runtime_error);
```

---

## 4. Testing Philosophy

### 4.1 Wire-Format Assertions

Tests should verify data at raw byte offsets, not just through accessors. This catches offset bugs that accessor round-trips miss.

```cpp
// Good: verify raw bytes
buf.put_u8(5, 42);
ErrorFlyweight err{buf};
EXPECT_EQ(err.error_code(), 42);

// Weak: only tests accessor
err.set_error_code(42);
EXPECT_EQ(err.error_code(), 42);
```

### 4.2 Non-Zero Offset Tests

Always test at non-zero offsets to catch offset-wrapping bugs:
```cpp
constexpr i32 offset = 128;
ErrorFlyweight err{buf, offset};
// ... set fields ...
EXPECT_EQ(buf.get_i32(offset + 8), 32); // verify at correct offset
EXPECT_EQ(buf.get_i32(0), 0);           // verify offset 0 unaffected
```

### 4.3 Corrupt Data Tests

Test with malformed wire data to verify defensive parsing:
```cpp
// Error message too long
std::string long_msg(ErrorFlyweight::MAX_ERROR_MESSAGE_LENGTH + 1, 'x');
EXPECT_THROW(err.set_error_message(long_msg), std::out_of_range);
```

---

## 5. Cross-Language Mapping

| Java Source | C++ Header | Notes |
|---|---|---|
| `HeaderFlyweight.java` | `header_flyweight.h` | Base header, type constants |
| `DataHeaderFlyweight.java` | `data_header_flyweight.h` | DATA frame, flags, static helpers |
| `StatusMessageFlyweight.java` | `status_message_flyweight.h` | SM frame, group tag validation |
| `SetupFlyweight.java` | `setup_flyweight.h` | SETUP frame for MDC |
| `RttMeasurementFlyweight.java` | `rtt_measurement_flyweight.h` | RTT request/reply |
| `NakFlyweight.java` | `nak_flyweight.h` | NAK for retransmission |
| `ErrorFlyweight.java` (1.47.0) | `error_flyweight.h` | Matches Java 1.47.0 layout |
| `ResponseSetupFlyweight.java` | `response_setup_flyweight.h` | Response setup for MDC |
| `ResolutionEntryFlyweight.java` | `resolution_entry_flyweight.h` | Name resolution, 4-byte alignment |
