#pragma once

#include "caeron/common/endian.h"
#include "caeron/common/types.h"
#include "caeron/driver/media/socket_address_parser.h"
#include "caeron/driver/media/udp_channel.h"
#include "caeron/driver/media/udp_channel_transport.h"
#include "caeron/protocol/error_flyweight.h"
#include "caeron/protocol/header_flyweight.h"
#include "caeron/protocol/nak_flyweight.h"
#include "caeron/protocol/response_setup_flyweight.h"
#include "caeron/protocol/rtt_measurement_flyweight.h"
#include "caeron/protocol/status_message_flyweight.h"

#include <array>
#include <atomic>
#include <cstring>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace caeron::driver::media {

/// Outbound transport aggregating multiple publications onto one socket,
/// dispatching received control frames (SM, NAK, RTT, Error, ResponseSetup).
///
/// The send channel endpoint owns a single UdpChannelTransport for sending
/// data frames and receiving control frames. It dispatches incoming control
/// frames to the appropriate NetworkPublication by session_id|stream_id key.
class SendChannelEndpoint : public UdpChannelTransport
{
public:
    static constexpr i64 DESTINATION_TIMEOUT_NS = 5'000'000'000LL;  // 5 seconds

    SendChannelEndpoint(
        const UdpChannel& udp_channel,
        i64 status_indicator_counter_id)
        : UdpChannelTransport(
            udp_channel,
            udp_channel.remote_data(),
            udp_channel.local_data(),
            &udp_channel.remote_control(),
            nullptr,  // no port manager
            udp_channel.socket_rcvbuf_length(),
            udp_channel.socket_sndbuf_length())
        , status_indicator_counter_id_{status_indicator_counter_id}
    {}

    ~SendChannelEndpoint() override = default;

    // --- Publication registration ---

    /// Register a publication by compound session_id|stream_id key.
    /// The void* is a placeholder for NetworkPublication* (resolved later).
    /// Thread-safe: acquires write lock.
    void register_for_send(i64 session_stream_key, void* publication)
    {
        std::unique_lock lock(publication_mutex_);
        publication_by_session_and_stream_[session_stream_key] = publication;
    }

    /// Unregister a publication. Thread-safe: acquires write lock.
    void unregister_for_send(i64 session_stream_key)
    {
        std::unique_lock lock(publication_mutex_);
        publication_by_session_and_stream_.erase(session_stream_key);
    }

    // --- Control frame callbacks ---
    // These parse the raw buffer and extract flyweight fields for dispatch.

    /// Build compound key from session_id and stream_id.
    [[nodiscard]] static i64 session_and_stream_key(i32 session_id, i32 stream_id)
    {
        return (static_cast<i64>(session_id) << 32) | static_cast<i64>(static_cast<u32>(stream_id));
    }

    /// Dispatch a received control frame based on its type.
    /// Returns true if the frame was dispatched to a registered publication,
    /// false if unknown type or no matching publication.
    /// Thread-safe: acquires shared lock for publication lookup.
    bool dispatch_control_frame(
        const std::byte* buffer, i32 length,
        const struct sockaddr_storage& src_address,
        i32& session_id, i32& stream_id) const
    {
        // Control frames require at least 16 bytes: common header (8) + session_id (4) + stream_id (4)
        if (length < 16)
            return false;

        // Read common header fields (explicit little-endian)
        u16 type = get_le16(buffer + 6);

        // Read session/stream IDs (safe because we checked length >= 16)
        session_id = get_le32(buffer + 8);
        stream_id = get_le32(buffer + 12);

        // Only dispatch recognized control frame types with protocol-specific minimum lengths:
        //   SM:       36 bytes (HEADER_LENGTH), or 44 with group tag
        //   NAK:      28 bytes (HEADER_LENGTH)
        //   RTTM:     40 bytes (HEADER_LENGTH)
        //   ERR:      40 bytes (HEADER_LENGTH) + variable error string
        //   RSP_SETUP: 20 bytes (HEADER_LENGTH)
        switch (type)
        {
        case protocol::HeaderFlyweight::HDR_TYPE_SM:
            if (length < 36) return false;
            break;
        case protocol::HeaderFlyweight::HDR_TYPE_NAK:
            if (length < 28) return false;
            break;
        case protocol::HeaderFlyweight::HDR_TYPE_RTTM:
            if (length < 40) return false;
            break;
        case protocol::HeaderFlyweight::HDR_TYPE_ERR:
            if (length < 40) return false;
            break;
        case protocol::HeaderFlyweight::HDR_TYPE_RSP_SETUP:
            if (length < 20) return false;
            break;
        default:
            return false;
        }

        {
            // Look up the publication by session_id|stream_id
            i64 key = session_and_stream_key(session_id, stream_id);
            std::shared_lock lock(publication_mutex_);
            auto it = publication_by_session_and_stream_.find(key);
            if (it == publication_by_session_and_stream_.end())
                return false;
            // Publication found -- frame dispatched
            return true;
        }
    }

    // Accessors
    [[nodiscard]] bool is_active() const noexcept
    {
        return ref_count_.load(std::memory_order_relaxed) > 0;
    }
    [[nodiscard]] i64 status_indicator_counter_id() const noexcept
    {
        return status_indicator_counter_id_;
    }

    void inc_ref() noexcept { ref_count_.fetch_add(1, std::memory_order_relaxed); }
    void dec_ref() noexcept
    {
        int expected = ref_count_.load(std::memory_order_relaxed);
        while (expected > 0 && !ref_count_.compare_exchange_weak(expected, expected - 1,
                                                                  std::memory_order_relaxed))
        {}
    }
    [[nodiscard]] bool should_be_closed() const noexcept
    {
        return ref_count_.load(std::memory_order_relaxed) <= 0;
    }

private:
    std::atomic<int> ref_count_{0};
    i64 status_indicator_counter_id_ = 0;
    mutable std::shared_mutex publication_mutex_;
    std::unordered_map<i64, void*> publication_by_session_and_stream_;
};

} // namespace caeron::driver::media
