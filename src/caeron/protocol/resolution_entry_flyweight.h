#pragma once

#include "caeron/common/types.h"
#include "caeron/common/bit_util.h"
#include "caeron/concurrent/unsafe_buffer.h"

#include <cstring>
#include <stdexcept>
#include <string>

namespace caeron::protocol {

/// Flyweight for Resolution Entry frame.
/// Used for name resolution in the Aeron protocol.
class ResolutionEntryFlyweight
{
public:
    // Resolution type constants
    static constexpr u8 RES_TYPE_NAME_TO_IP4_MD = 0x01;
    static constexpr u8 RES_TYPE_NAME_TO_IP6_MD = 0x02;

    // Address length constants
    static constexpr i32 ADDRESS_LENGTH_IP4 = 4;
    static constexpr i32 ADDRESS_LENGTH_IP6 = 16;

    // Flag constants
    static constexpr u8 SELF_FLAG = 0x80;

    // Field offsets
    static constexpr i32 RES_TYPE_FIELD_OFFSET = 0;
    static constexpr i32 RES_FLAGS_FIELD_OFFSET = 1;
    static constexpr i32 UDP_PORT_FIELD_OFFSET = 2;
    static constexpr i32 AGE_IN_MS_FIELD_OFFSET = 4;
    static constexpr i32 ADDRESS_FIELD_OFFSET = 8;

    // Maximum name length
    static constexpr i32 MAX_NAME_LENGTH = 512;

    explicit ResolutionEntryFlyweight(concurrent::UnsafeBuffer& buffer, i32 offset = 0) noexcept
        : buffer_{buffer}, offset_{offset}
    {}

    // Resolution type
    [[nodiscard]] u8 res_type() const noexcept { return buffer_.get_u8(offset_ + RES_TYPE_FIELD_OFFSET); }
    ResolutionEntryFlyweight& set_res_type(u8 type) noexcept
    {
        buffer_.put_u8(offset_ + RES_TYPE_FIELD_OFFSET, type);
        return *this;
    }

    // Flags
    [[nodiscard]] u8 flags() const noexcept { return buffer_.get_u8(offset_ + RES_FLAGS_FIELD_OFFSET); }
    ResolutionEntryFlyweight& set_flags(u8 flags) noexcept
    {
        buffer_.put_u8(offset_ + RES_FLAGS_FIELD_OFFSET, flags);
        return *this;
    }

    // UDP port
    [[nodiscard]] u16 udp_port() const noexcept { return buffer_.get_u16(offset_ + UDP_PORT_FIELD_OFFSET); }
    ResolutionEntryFlyweight& set_udp_port(u16 port) noexcept
    {
        buffer_.put_u16(offset_ + UDP_PORT_FIELD_OFFSET, port);
        return *this;
    }

    // Age in milliseconds
    [[nodiscard]] i32 age_in_ms() const noexcept { return buffer_.get_i32(offset_ + AGE_IN_MS_FIELD_OFFSET); }
    ResolutionEntryFlyweight& set_age_in_ms(i32 age_ms) noexcept
    {
        buffer_.put_i32(offset_ + AGE_IN_MS_FIELD_OFFSET, age_ms);
        return *this;
    }

    /// Put an IPv4 or IPv4 address into the resolution entry.
    /// The res_type must be set before calling this.
    void put_address(const u8* address, i32 address_length)
    {
        const u8 type = res_type();
        if (type == RES_TYPE_NAME_TO_IP4_MD)
        {
            if (address_length != ADDRESS_LENGTH_IP4)
            {
                throw std::invalid_argument("Invalid address length: " + std::to_string(address_length));
            }
            buffer_.put_bytes(offset_ + ADDRESS_FIELD_OFFSET, address, ADDRESS_LENGTH_IP4);
        }
        else if (type == RES_TYPE_NAME_TO_IP6_MD)
        {
            if (address_length != ADDRESS_LENGTH_IP6)
            {
                throw std::invalid_argument("Invalid address length: " + std::to_string(address_length));
            }
            buffer_.put_bytes(offset_ + ADDRESS_FIELD_OFFSET, address, ADDRESS_LENGTH_IP6);
        }
        else
        {
            throw std::runtime_error("unknown RES_TYPE=" + std::to_string(type));
        }
    }

    /// Get the address for the entry by copying it into the destination buffer.
    /// Returns the length of the address copied.
    i32 get_address(u8* dst_buffer, i32 dst_length) const
    {
        const u8 type = res_type();
        if (type == RES_TYPE_NAME_TO_IP4_MD)
        {
            if (ADDRESS_LENGTH_IP4 > dst_length)
            {
                throw std::invalid_argument("Insufficient length: " + std::to_string(dst_length));
            }
            buffer_.get_bytes(offset_ + ADDRESS_FIELD_OFFSET, dst_buffer, ADDRESS_LENGTH_IP4);
            return ADDRESS_LENGTH_IP4;
        }
        else if (type == RES_TYPE_NAME_TO_IP6_MD)
        {
            if (ADDRESS_LENGTH_IP6 > dst_length)
            {
                throw std::invalid_argument("Insufficient length: " + std::to_string(dst_length));
            }
            buffer_.get_bytes(offset_ + ADDRESS_FIELD_OFFSET, dst_buffer, ADDRESS_LENGTH_IP6);
            return ADDRESS_LENGTH_IP6;
        }
        throw std::runtime_error("unknown RES_TYPE=" + std::to_string(type));
    }

    /// Put the name for the resolution entry into the frame.
    /// Throws if name_length is negative or exceeds MAX_NAME_LENGTH.
    void put_name(const u8* name, i32 name_length)
    {
        if (name_length < 0 || name_length > MAX_NAME_LENGTH)
        {
            throw std::out_of_range("name_length out of range: " + std::to_string(name_length));
        }
        const i32 no = name_offset(res_type());
        buffer_.put_i16(offset_ + no, static_cast<i16>(name_length));
        buffer_.put_bytes(offset_ + no + sizeof(i16), name, name_length);
    }

    /// Get the name for the entry by copying it into the destination buffer.
    /// Returns the number of bytes copied.
    /// Throws if stored name length is negative, exceeds MAX_NAME_LENGTH, or dst is too small.
    i32 get_name(u8* dst_buffer, i32 dst_length) const
    {
        const i32 no = name_offset(res_type());
        const i16 stored_len = buffer_.get_i16(offset_ + no);
        if (stored_len < 0 || stored_len > MAX_NAME_LENGTH)
        {
            throw std::runtime_error("invalid stored name length: " + std::to_string(stored_len));
        }
        if (stored_len > dst_length)
        {
            throw std::invalid_argument("insufficient dst length: " + std::to_string(dst_length));
        }
        buffer_.get_bytes(offset_ + no + sizeof(i16), dst_buffer, stored_len);
        return stored_len;
    }

    /// Total length of the entry in bytes (padded to 4-byte boundary per transport spec).
    /// Throws if stored name length is negative or exceeds MAX_NAME_LENGTH.
    [[nodiscard]] i32 entry_length() const
    {
        const i32 no = name_offset(res_type());
        const i16 stored_len = buffer_.get_i16(offset_ + no);
        if (stored_len < 0 || stored_len > MAX_NAME_LENGTH)
        {
            throw std::runtime_error("invalid stored name length: " + std::to_string(stored_len));
        }
        return caeron::align(no + static_cast<i32>(sizeof(i16)) + stored_len, static_cast<i32>(sizeof(i64)));
    }

    /// Offset in the entry at which the name begins depending on resolution type.
    [[nodiscard]] static i32 name_offset(u8 res_type)
    {
        switch (res_type)
        {
            case RES_TYPE_NAME_TO_IP4_MD:
                return ADDRESS_FIELD_OFFSET + ADDRESS_LENGTH_IP4;
            case RES_TYPE_NAME_TO_IP6_MD:
                return ADDRESS_FIELD_OFFSET + ADDRESS_LENGTH_IP6;
            default:
                throw std::runtime_error("unknown RES_TYPE=" + std::to_string(res_type));
        }
    }

    /// Calculate the length required for the entry when encoded (padded to 4-byte boundary).
    /// Throws if name_length is negative or exceeds MAX_NAME_LENGTH.
    [[nodiscard]] static i32 entry_length_required(u8 res_type, i32 name_length)
    {
        if (name_length < 0 || name_length > MAX_NAME_LENGTH)
        {
            throw std::out_of_range("name_length out of range: " + std::to_string(name_length));
        }
        return caeron::align(name_offset(res_type) + static_cast<i32>(sizeof(i16)) + name_length,
                            static_cast<i32>(sizeof(i64)));
    }

    /// Get the length of the address given a resolution type.
    [[nodiscard]] static i32 address_length(u8 res_type)
    {
        switch (res_type)
        {
            case RES_TYPE_NAME_TO_IP4_MD:
                return ADDRESS_LENGTH_IP4;
            case RES_TYPE_NAME_TO_IP6_MD:
                return ADDRESS_LENGTH_IP6;
            default:
                throw std::runtime_error("unknown RES_TYPE=" + std::to_string(res_type));
        }
    }

    /// Is the local address a match for binding a socket to ANY IP.
    [[nodiscard]] static bool is_any_local_address(const u8* address, i32 address_length)
    {
        if (address_length == ADDRESS_LENGTH_IP4)
        {
            return address[0] == 0 && address[1] == 0 && address[2] == 0 && address[3] == 0;
        }
        else if (address_length == ADDRESS_LENGTH_IP6)
        {
            u8 val = 0;
            for (i32 i = 0; i < ADDRESS_LENGTH_IP6; ++i)
            {
                val |= address[i];
            }
            return val == 0;
        }
        return false;
    }

    [[nodiscard]] concurrent::UnsafeBuffer& buffer() noexcept { return buffer_; }
    [[nodiscard]] i32 offset() const noexcept { return offset_; }

private:
    concurrent::UnsafeBuffer& buffer_;
    i32 offset_;
};

} // namespace caeron::protocol
