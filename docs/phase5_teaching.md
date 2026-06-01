# Phase 5: CnC Client Communication — Teaching Guide

## What You'll Learn

- Aeron's CnC (Command and Control) file-based IPC architecture
- Ring buffer and broadcast buffer for client-driver communication
- Command flyweights: wire format for client→driver requests
- Response flyweights: wire format for driver→client notifications
- Defensive parsing: bounds checking, overflow prevention, null safety
- The flyweight pattern for zero-copy protocol serialization

---

## 1. Architecture Overview

The CnC layer implements client-driver IPC using shared memory files:

```
┌─────────────┐     ┌─────────────────────────────────────┐     ┌─────────────┐
│   Client     │     │           CnC File                   │     │   Driver    │
│              │     │  ┌─────────────────────────────────┐ │     │             │
│ DriverProxy  │────▶│  │    To-Driver Ring Buffer         │ │────▶│ ClientCmd   │
│              │     │  │    (commands: add_pub, remove...) │ │     │ Adapter     │
│              │     │  └─────────────────────────────────┘ │     │             │
│              │     │  ┌─────────────────────────────────┐ │     │             │
│ DriverEvents │◀────│  │    To-Clients Broadcast Buffer   │ │◀────│ ClientProxy │
│ Adapter      │     │  │    (responses: ready, error...)  │ │     │             │
│              │     │  └─────────────────────────────────┘ │     │             │
└─────────────┘     └─────────────────────────────────────┘     └─────────────┘
```

**Key components:**
- `CncFileDescriptor` — Maps the CnC file layout (metadata + buffers)
- `DriverProxy` — Client-side: writes commands to to-driver ring buffer
- `ClientCommandAdapter` — Driver-side: reads commands and dispatches to handler
- `ClientProxy` — Driver-side: writes responses to to-clients broadcast buffer
- `DriverEventsAdapter` — Client-side: reads responses and dispatches to handler

---

## 2. CnC File Layout

The CnC file has a fixed metadata header followed by variable-length buffers:

```
CncFileDescriptor metadata:
[0, 4)   - cnc_version (i32)
[4, 8)   - to_driver_buffer_length (i32)
[8, 12)  - to_clients_buffer_length (i32)
[12, 16) - counters_metadata_buffer_length (i32)
[16, 20) - counters_values_buffer_length (i32)
[20, 24) - error_log_buffer_length (i32)
[24, 32) - client_liveness_timeout (i64)
[32, 40) - start_timestamp (i64)
[40, 48) - pid (i64)
```

**Offset computation uses i64 intermediates** to prevent overflow:
```cpp
static i32 to_clients_buffer_offset(i32 to_driver_length)
{
    i64 offset = static_cast<i64>(METADATA_LENGTH) + to_driver_length;
    if (offset < 0 || offset > std::numeric_limits<i32>::max()) return -1;
    return static_cast<i32>(offset);
}
```

**All accessor methods validate capacity** before reading:
```cpp
[[nodiscard]] i32 to_driver_buffer_length(const concurrent::UnsafeBuffer& buf) const noexcept
{
    if (buf.capacity() < METADATA_LENGTH) return 0;
    return buf.get_i32(offset_ + TO_DRIVER_BUFFER_LENGTH_OFFSET);
}
```

---

## 3. Command Flyweights

Command flyweights serialize client→driver requests. All use the `[0] i64 client_id, [8] i64 correlation_id` layout matching Java Aeron.

### 3.1 CorrelatedMessageFlyweight Layout

Most commands extend this base layout:

```
[0, 8)   - client_id (i64)
[8, 16)  - correlation_id (i64)
[16, ..) - command-specific fields
```

### 3.2 Variable-Length Command Examples

**PublicationMessageFlyweight:**
```
[0, 8)   - client_id (i64)
[8, 16)  - correlation_id (i64)
[16, 20) - stream_id (i32)
[20, 24) - channel_length (i32)
[24, ..) - channel (variable)
```

**CounterMessageFlyweight:**
```
[0, 8)   - client_id (i64)
[8, 16)  - correlation_id (i64)
[16, 20) - counter_type_id (i32)
[20, 24) - key_buffer_length (i32)
[24, ..) - key_buffer (variable, 4-byte aligned)
[.., +4) - label_length (i32)
[.., ..) - label (variable)
```

### 3.3 Defensive Setter Pattern

Every variable-length setter uses a three-step overflow-safe guard:

```cpp
void set_channel(const void* data, i32 length) noexcept
{
    if (length < 0) return;                                    // reject negative
    if (offset_ < 0) return;                                   // reject negative offset
    const i32 cap = buffer_.capacity();
    if (cap < offset_ || cap - offset_ < CHANNEL_OFFSET) return; // overflow-safe capacity check
    if (length > cap - offset_ - CHANNEL_OFFSET) return;         // overflow-safe length check
    set_channel_length(length);
    if (data != nullptr && length > 0)
        buffer_.put_bytes(offset_ + CHANNEL_OFFSET, data, length);
}
```

**Why subtraction-style?**
- `offset_ + CHANNEL_OFFSET` can overflow if `offset_` is near INT_MAX
- `cap - offset_ < CHANNEL_OFFSET` is safe because we already checked `cap >= offset_`

### 3.4 Derived-Field Accessor Pattern

Fields whose offset depends on an embedded length (e.g., label after key) must validate the embedded length BEFORE computing the derived offset:

```cpp
[[nodiscard]] i32 label_length() const noexcept
{
    if (offset_ < 0) return -1;
    if (cap < offset_ || cap - offset_ < KEY_OFFSET) return -1;  // prove KEY_OFFSET safe
    const i32 kl = key_buffer_length();
    if (kl < 0) return -1;                                        // reject negative
    if (kl > cap - offset_ - KEY_OFFSET - SIZE_OF_INT) return -1; // prove kl safe
    return buffer_.get_i32(offset_ + KEY_OFFSET + kl);            // safe: bounds proven
}
```

**Key rule: compute the sum AFTER the bounds check, not before.**

### 3.5 compute_length() Overflow Protection

All `compute_length()` static methods use i64 arithmetic and return -1 on overflow:

```cpp
[[nodiscard]] static i32 compute_length(i32 channel_length)
{
    if (channel_length < 0) return -1;
    i64 len = static_cast<i64>(CHANNEL_OFFSET) + channel_length;
    if (len > std::numeric_limits<i32>::max()) return -1;
    return static_cast<i32>(len);
}
```

### 3.6 Null-Safety in Setters

All setters document the null-coercion contract:
- `nullptr` with positive length → length field is set, no bytes written (null-to-empty)
- Negative length → no-op (keeps old state)
- Zero length → length field set to 0, no bytes written

```cpp
/// Contract:
///   - Negative length is a no-op.
///   - If data is nullptr, the length field is set but no bytes are written
///     (null-to-empty coercion). This avoids dereferencing a null pointer.
///   - If length is zero, no bytes are written regardless of data.
void set_channel(const void* data, i32 length) noexcept { ... }
```

---

## 4. Response Flyweights

Response flyweights serialize driver→client notifications. Key types:

| Flyweight | Event | Fields |
|---|---|---|
| ErrorResponseFlyweight | ON_ERROR | correlation_id, error_code, error_message |
| PublicationBuffersReadyFlyweight | ON_PUBLICATION_READY | correlation_id, registration_id, session_id, stream_id, log_file_name |
| ImageBuffersReadyFlyweight | ON_AVAILABLE_IMAGE | correlation_id, session_id, stream_id, subscription_registration_id, subscriber_position_id, log_file_name, source_identity |
| ImageMessageFlyweight | ON_UNAVAILABLE_IMAGE | correlation_id, subscription_registration_id, stream_id, channel |
| PublicationErrorFrameFlyweight | ON_PUBLICATION_ERROR | registration_id, destination_registration_id, session_id, stream_id, receiver_id, group_tag, address, error_code, error_message |
| CounterUpdateFlyweight | ON_COUNTER_READY | correlation_id, counter_id |
| OperationSucceededFlyweight | ON_OPERATION_SUCCESS | correlation_id |
| ClientTimeoutFlyweight | ON_CLIENT_TIMEOUT | client_id |

---

## 5. DriverProxy

The DriverProxy writes commands to the to-driver ring buffer. Key design:

### 5.1 Scratch Buffer Pattern

Uses a fixed 2048-byte scratch buffer to serialize flyweights before writing to the ring buffer:

```cpp
void add_publication(i64 client_id, i64 correlation_id, i32 stream_id, const char* channel)
{
    if (channel == nullptr) channel = "";  // null-to-empty coercion
    const i32 channel_len = safe_strlen(channel);  // size_t→i32 with overflow check
    const i32 msg_len = PublicationMessageFlyweight::compute_length(channel_len);
    validate_scratch_length(msg_len);  // checks 0 < msg_len <= 2048

    PublicationMessageFlyweight fw{scratch_, 0};
    fw.set_client_id(client_id)
      .set_correlation_id(correlation_id)
      .set_stream_id(stream_id);
    fw.set_channel(channel, channel_len);

    ring_buffer_.write(MSG_TYPE_ID, scratch_, msg_len);
}
```

### 5.2 safe_strlen() Helper

Prevents size_t-to-i32 truncation for extremely long strings:

```cpp
[[nodiscard]] static i32 safe_strlen(const char* s)
{
    if (s == nullptr) return 0;
    const auto len = std::strlen(s);
    if (len > static_cast<size_t>(std::numeric_limits<i32>::max()))
        throw std::runtime_error("string length exceeds i32 max");
    return static_cast<i32>(len);
}
```

### 5.3 validate_scratch_length() Helper

Checks both negative and oversized lengths:

```cpp
static void validate_scratch_length(i32 length)
{
    if (length < 0 || length > SCRATCH_BUFFER_SIZE)
        throw std::out_of_range("message length out of range");
}
```

### 5.4 Correlation ID Generation

Uses an atomic counter for monotonic correlation IDs:

```cpp
[[nodiscard]] i64 next_correlation_id() noexcept
{
    return correlation_counter_.fetch_add(1, std::memory_order_relaxed);
}
```

---

## 6. ClientCommandAdapter

The Driver-side adapter reads commands from the ring buffer and dispatches to a handler.

### 6.1 Dispatch Pattern

Uses a switch on message type ID:

```cpp
void on_message(i32 msg_type_id, const concurrent::UnsafeBuffer& buffer, i32 offset, i32 length)
{
    // const_cast is safe: ring buffer data region is read-write
    concurrent::UnsafeBuffer buffer_rw{const_cast<std::byte*>(buffer.data() + offset), length};

    switch (msg_type_id)
    {
        case command::ADD_PUBLICATION:
        {
            if (length < command::PublicationMessageFlyweight::MINIMUM_LENGTH) { ... break; }
            command::PublicationMessageFlyweight fw{buffer_rw, 0};
            // validate channel_length before reading channel
            const i32 channel_len = fw.channel_length();
            if (channel_len < 0 || channel_len > length - command::PublicationMessageFlyweight::CHANNEL_OFFSET) { ... break; }
            handler_.on_add_publication(fw.client_id(), fw.correlation_id(), fw.stream_id(), ...);
            break;
        }
        // ... all 18 command types
    }
}
```

### 6.2 Length Validation

Every dispatch case validates:
1. Minimum message length before constructing flyweight
2. Embedded lengths (channel, key, label, token, reason) are non-negative
3. Embedded lengths fit within the message using subtraction-style checks

### 6.3 Handler Interface

The handler uses `i64 client_id` (not i32) to preserve the full wire value:

```cpp
struct Handler {
    void on_add_publication(i64 client_id, i64 correlation_id, i32 stream_id, std::string_view channel);
    void on_add_subscription(i64 client_id, i64 correlation_id, i32 stream_id, ...);
    // ...
};
```

---

## 7. ClientProxy

The Driver-side proxy writes responses to the to-clients broadcast buffer.

### 7.1 Broadcast Transmitter

Uses a `BroadcastTransmitter` that wraps the broadcast buffer:

```cpp
void on_error(i64 correlation_id, i32 error_code, const char* error_message)
{
    if (error_message == nullptr) error_message = "";
    const i32 msg_len = ErrorResponseFlyweight::compute_length(safe_strlen(error_message));
    validate_scratch_length(msg_len);

    ErrorResponseFlyweight fw{scratch_, 0};
    fw.set_correlation_id(correlation_id)
      .set_error_code(error_code);
    fw.set_error_message(error_message, safe_strlen(error_message));

    transmitter_.transmit(ControlProtocolEvents::ON_ERROR, scratch_, msg_len);
}
```

---

## 8. DriverEventsAdapter

The Client-side adapter reads responses from the broadcast buffer and dispatches to a handler.

### 8.1 Correlation ID Filtering

Most events are filtered by `active_correlation_id_`:

```cpp
void on_event(i32 event_type_id, const concurrent::UnsafeBuffer& buffer, i32 offset, i32 length)
{
    switch (event_type_id)
    {
        case ControlProtocolEvents::ON_ERROR:
        {
            // ON_ERROR always dispatched (not filtered by correlation_id)
            // CHANNEL_ENDPOINT_ERROR gets special handling
            handler_.on_error(fw.correlation_id(), fw.error_code(), ...);
            break;
        }
        case ControlProtocolEvents::ON_PUBLICATION_READY:
        {
            if (fw.correlation_id() != active_correlation_id_) break;  // filtered
            handler_.on_publication_ready(...);
            break;
        }
        // ...
    }
}
```

### 8.2 Minimum-Length Validation

Every event case validates minimum flyweight length before constructing the flyweight, and validates embedded lengths (log_file_name, source_identity, error_message) against the event length.

---

## 9. Common Pitfalls

### 9.1 Signed-Overflow UB in Bounds Guards

**Wrong:**
```cpp
if (offset_ + FIELD_OFFSET > capacity) return;  // UB if offset_ near INT_MAX
```

**Right:**
```cpp
if (offset_ < 0 || capacity < offset_ || capacity - offset_ < FIELD_OFFSET) return;
```

### 9.2 Derived-Offset Overflow

**Wrong:**
```cpp
const i32 derived = KEY_OFFSET + kl + SIZE_OF_INT;  // UB if kl near INT_MAX
if (derived > capacity - offset_) return -1;
```

**Right:**
```cpp
if (kl > capacity - offset_ - KEY_OFFSET - SIZE_OF_INT) return -1;  // bounds first
return KEY_OFFSET + kl + SIZE_OF_INT;  // then compute (safe: bounds proven)
```

### 9.3 strlen() Truncation

**Wrong:**
```cpp
const i32 len = static_cast<i32>(std::strlen(s));  // truncates if > INT_MAX
```

**Right:**
```cpp
const i32 len = safe_strlen(s);  // throws if > INT_MAX
```

### 9.4 Null C-String Dereference

**Wrong:**
```cpp
const i32 len = safe_strlen(channel);  // crashes if channel is nullptr
```

**Right:**
```cpp
if (channel == nullptr) channel = "";  // null-to-empty coercion
const i32 len = safe_strlen(channel);
```

### 9.5 Raw Setter Misuse

`set_channel_length()`, `set_key_buffer_length()`, etc. are RAW FIELD SETTERS with no validation. Use `set_channel()`, `set_key_buffer()` for bounds-checked writes.

---

## 10. Testing Philosophy

### 10.1 Wire-Format Layout Tests

Verify raw byte offsets match Java Aeron:

```cpp
TEST(PublicationMessageFlyweight, RawByteLayout)
{
    std::array<std::byte, 256> storage{};
    UnsafeBuffer buf{storage};
    PublicationMessageFlyweight fw{buf, 0};
    fw.set_client_id(0x1122334455667788LL)
      .set_correlation_id(0xAABBCCDD11223344LL)
      .set_stream_id(42);
    fw.set_channel("test", 4);

    EXPECT_EQ(buf.get_i64(0), 0x1122334455667788LL);   // client_id at offset 0
    EXPECT_EQ(buf.get_i64(8), static_cast<i64>(0xAABBCCDD11223344LL)); // correlation_id at 8
    EXPECT_EQ(buf.get_i32(16), 42);                     // stream_id at 16
    EXPECT_EQ(buf.get_i32(20), 4);                      // channel_length at 20
}
```

### 10.2 Negative-Offset Tests

Verify negative offsets are rejected:

```cpp
TEST(PublicationMessageFlyweight, NegativeOffsetChannelReturnsNull)
{
    std::array<std::byte, 256> storage{};
    UnsafeBuffer buf{storage};
    PublicationMessageFlyweight fw{buf, -1};
    EXPECT_EQ(fw.channel(), nullptr);  // must not crash
}
```

### 10.3 Malformed-Command Tests

Verify adapter rejects truncated/invalid messages:

```cpp
TEST(ClientCommandAdapter, TruncatedPublicationMessage)
{
    // Write a message with channel_length = 100 but only 28 bytes total
    // Adapter must reject, not read out of bounds
}
```

### 10.4 Round-Trip Tests

Verify full proxy→adapter→proxy→adapter cycle:

```cpp
TEST(CncIntegration, AddPublicationRoundTrip)
{
    DriverProxy driver_proxy{ring_buffer};
    driver_proxy.add_publication(client_id, correlation_id, stream_id, "aeron:udp?endpoint=localhost:40456");

    // Read from ring buffer via adapter
    MockHandler handler;
    ClientCommandAdapter adapter{handler};
    ring_buffer.read(adapter);

    EXPECT_EQ(handler.last_client_id, client_id);
    EXPECT_EQ(handler.last_channel, "aeron:udp?endpoint=localhost:40456");
}
```

---

## 11. Cross-Language Mapping

| Java Source | C++ Header | Notes |
|---|---|---|
| `CncFileDescriptor.java` | `cnc_file_descriptor.h` | CnC file layout, offset computation |
| `MappedLogBuffersFactory.java` | `mapped_log_buffers_factory.h` | Log buffer creation/mapping |
| `DriverProxy.java` | `driver_proxy.h` | Client-side command writer |
| `ClientCommandAdapter.java` | `client_command_adapter.h` | Driver-side command reader |
| `ClientProxy.java` | `client_proxy.h` | Driver-side response writer |
| `DriverEventsAdapter.java` | `driver_events_adapter.h` | Client-side response reader |
| `CorrelatedMessageFlyweight.java` | `correlated_message_flyweight.h` | Base command layout |
| `PublicationMessageFlyweight.java` | `publication_message_flyweight.h` | Add publication command |
| `SubscriptionMessageFlyweight.java` | `subscription_message_flyweight.h` | Add subscription command |
| `RemoveMessageFlyweight.java` | `remove_message_flyweight.h` | Remove resource command |
| `CounterMessageFlyweight.java` | `counter_message_flyweight.h` | Add counter command |
| `ImageBuffersReadyFlyweight.java` | `image_buffers_ready_flyweight.h` | Image ready response |
| `PublicationErrorFrameFlyweight.java` | `publication_error_frame_flyweight.h` | Publication error response |
