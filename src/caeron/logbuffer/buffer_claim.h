#pragma once

#include "caeron/common/types.h"
#include "caeron/concurrent/unsafe_buffer.h"
#include "caeron/logbuffer/frame_descriptor.h"
#include "caeron/protocol/data_header_flyweight.h"
#include "caeron/protocol/header_flyweight.h"

namespace caeron::logbuffer {

/// Represents a claimed region in a term buffer for zero-copy message writing.
///
/// The publisher claims space, writes payload directly, then calls commit()
/// to publish the frame with release semantics. Alternatively, call abort()
/// to mark the frame as padding (e.g., if an error occurred during writing).
class BufferClaim
{
public:
    BufferClaim() = default;

    /// Wrap a claimed region in the term buffer.
    /// @param buffer  the term buffer
    /// @param offset  the frame offset within the term buffer
    /// @param capacity  the total frame capacity (header + payload)
    void wrap(concurrent::UnsafeBuffer& buffer, i32 offset, i32 capacity) noexcept
    {
        buffer_ = &buffer;
        offset_ = offset;
        capacity_ = capacity;
    }

    /// Get the underlying buffer.
    [[nodiscard]] concurrent::UnsafeBuffer& buffer() const noexcept { return *buffer_; }

    /// Get the offset within the buffer where the payload begins (past the header).
    [[nodiscard]] i32 offset() const noexcept { return offset_ + protocol::DataHeaderFlyweight::HEADER_LENGTH; }

    /// Get the payload length (total capacity minus header size).
    [[nodiscard]] i32 length() const noexcept
    {
        return capacity_ - protocol::DataHeaderFlyweight::HEADER_LENGTH;
    }

    /// Get the total frame capacity.
    [[nodiscard]] i32 capacity() const noexcept { return capacity_; }

    /// Read the frame type field.
    [[nodiscard]] u16 header_type() const noexcept
    {
        return buffer_->get_u16(offset_ + FrameDescriptor::TYPE_FIELD_OFFSET);
    }

    /// Write the frame type field.
    void header_type(u16 type) noexcept
    {
        buffer_->put_u16(offset_ + FrameDescriptor::TYPE_FIELD_OFFSET, type);
    }

    /// Read the flags field.
    [[nodiscard]] u8 flags() const noexcept
    {
        return buffer_->get_u8(offset_ + FrameDescriptor::FLAGS_FIELD_OFFSET);
    }

    /// Write the flags field.
    void flags(u8 value) noexcept
    {
        buffer_->put_u8(offset_ + FrameDescriptor::FLAGS_FIELD_OFFSET, value);
    }

    /// Read the reserved value (8 bytes at offset 24).
    [[nodiscard]] i64 reserved_value() const noexcept
    {
        return buffer_->get_i64(offset_ + 24);
    }

    /// Write the reserved value.
    void reserved_value(i64 value) noexcept
    {
        buffer_->put_i64(offset_ + 24, value);
    }

    /// Copy bytes into the payload area (past the 32-byte header).
    void put_bytes(const concurrent::UnsafeBuffer& src, i32 src_offset, i32 length) noexcept
    {
        buffer_->put_bytes(
            offset_ + protocol::DataHeaderFlyweight::HEADER_LENGTH,
            src.data() + src_offset,
            length);
    }

    /// Publish the frame by writing the frame length with release semantics.
    /// After this call, concurrent readers will see the complete frame.
    void commit() noexcept
    {
        FrameDescriptor::frame_length_ordered(*buffer_, offset_, capacity_);
    }

    /// Abort the claim — mark the frame as padding so readers skip it.
    /// Use this if an error occurred during writing and the frame should not be delivered.
    void abort() noexcept
    {
        FrameDescriptor::frame_type(*buffer_, offset_, protocol::HeaderFlyweight::HDR_TYPE_PAD);
        FrameDescriptor::frame_length_ordered(*buffer_, offset_, capacity_);
    }

private:
    concurrent::UnsafeBuffer* buffer_ = nullptr;
    i32 offset_ = 0;
    i32 capacity_ = 0;
};

} // namespace caeron::logbuffer
