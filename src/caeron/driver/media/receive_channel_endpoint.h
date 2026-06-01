#pragma once

#include "caeron/common/endian.h"
#include "caeron/common/types.h"
#include "caeron/concurrent/unsafe_buffer.h"
#include "caeron/driver/media/image_connection.h"
#include "caeron/driver/media/multi_rcv_destination.h"
#include "caeron/driver/media/socket_address_parser.h"
#include "caeron/driver/media/udp_channel.h"
#include "caeron/driver/media/udp_channel_transport.h"
#include "caeron/protocol/data_header_flyweight.h"
#include "caeron/protocol/error_flyweight.h"
#include "caeron/protocol/header_flyweight.h"
#include "caeron/protocol/nak_flyweight.h"
#include "caeron/protocol/response_setup_flyweight.h"
#include "caeron/protocol/rtt_measurement_flyweight.h"
#include "caeron/protocol/setup_flyweight.h"
#include "caeron/protocol/status_message_flyweight.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace caeron::driver::media {

/// Inbound transport aggregating multiple subscriptions onto one socket,
/// dispatching received data/setup/RTT frames, and sending control frames
/// (SM, NAK, RTT, Error, ResponseSetup) back to sources.
class ReceiveChannelEndpoint : public UdpChannelTransport
{
public:
    static constexpr i64 DESTINATION_ADDRESS_TIMEOUT_NS = 5'000'000'000LL;

    ReceiveChannelEndpoint(
        const UdpChannel& udp_channel,
        i64 status_indicator_counter_id)
        : UdpChannelTransport(
            udp_channel,
            udp_channel.remote_data(),
            udp_channel.local_data(),
            nullptr,  // no connect address for receive
            nullptr,  // no port manager
            udp_channel.socket_rcvbuf_length(),
            udp_channel.socket_sndbuf_length())
        , status_indicator_counter_id_{status_indicator_counter_id}
        , receiver_id_{generate_receiver_id()}
        , group_tag_{udp_channel.group_tag()}
    {}

    ~ReceiveChannelEndpoint() override = default;

    // --- Stream/session reference counting ---
    // All ref-count map operations are protected by ref_count_mutex_ to allow
    // safe concurrent access from conductor and poller threads.

    [[nodiscard]] int inc_ref_to_stream(i32 stream_id)
    {
        std::lock_guard lock(ref_count_mutex_);
        return ++ref_count_by_stream_id_[stream_id];
    }

    [[nodiscard]] int dec_ref_to_stream(i32 stream_id)
    {
        std::lock_guard lock(ref_count_mutex_);
        auto it = ref_count_by_stream_id_.find(stream_id);
        if (it == ref_count_by_stream_id_.end())
            return 0;
        if (--it->second <= 0)
        {
            ref_count_by_stream_id_.erase(it);
            return 0;
        }
        return it->second;
    }

    [[nodiscard]] i64 inc_ref_to_stream_and_session(i32 stream_id, i32 session_id)
    {
        i64 key = (static_cast<i64>(stream_id) << 32) | static_cast<i64>(static_cast<u32>(session_id));
        std::lock_guard lock(ref_count_mutex_);
        return ++ref_count_by_stream_and_session_[key];
    }

    [[nodiscard]] i64 dec_ref_to_stream_and_session(i32 stream_id, i32 session_id)
    {
        i64 key = (static_cast<i64>(stream_id) << 32) | static_cast<i64>(static_cast<u32>(session_id));
        std::lock_guard lock(ref_count_mutex_);
        auto it = ref_count_by_stream_and_session_.find(key);
        if (it == ref_count_by_stream_and_session_.end())
            return 0;
        if (--it->second <= 0)
        {
            ref_count_by_stream_and_session_.erase(it);
            return 0;
        }
        return it->second;
    }

    [[nodiscard]] int inc_response_ref_to_stream(i32 stream_id)
    {
        std::lock_guard lock(ref_count_mutex_);
        return ++response_ref_count_by_stream_id_[stream_id];
    }

    [[nodiscard]] int dec_response_ref_to_stream(i32 stream_id)
    {
        std::lock_guard lock(ref_count_mutex_);
        auto it = response_ref_count_by_stream_id_.find(stream_id);
        if (it == response_ref_count_by_stream_id_.end())
            return 0;
        if (--it->second <= 0)
        {
            response_ref_count_by_stream_id_.erase(it);
            return 0;
        }
        return it->second;
    }

    [[nodiscard]] bool should_be_closed() const noexcept
    {
        std::lock_guard lock(ref_count_mutex_);
        return image_ref_count_.load(std::memory_order_relaxed) <= 0 &&
               ref_count_by_stream_id_.empty() &&
               ref_count_by_stream_and_session_.empty() &&
               response_ref_count_by_stream_id_.empty();
    }

    // --- Data frame parsing helpers ---

    /// Extract DATA header fields from a raw buffer.
    /// Returns false if buffer is too small.
    [[nodiscard]] static bool parse_data_header(
        const std::byte* buffer, i32 length,
        i32& session_id, i32& stream_id, i32& term_id,
        i32& term_offset, i32& frame_length, u8& flags)
    {
        if (length < protocol::DataHeaderFlyweight::HEADER_LENGTH)
            return false;

        frame_length = get_le32(buffer);
        flags = static_cast<u8>(buffer[5]);
        term_offset = get_le32(buffer + 8);
        session_id = get_le32(buffer + 12);
        stream_id = get_le32(buffer + 16);
        term_id = get_le32(buffer + 20);

        return true;
    }

    /// Extract SETUP header fields from a raw buffer.
    [[nodiscard]] static bool parse_setup_header(
        const std::byte* buffer, i32 length,
        i32& session_id, i32& stream_id, i32& initial_term_id,
        i32& active_term_id, i32& term_length, i32& mtu_length, i32& ttl)
    {
        if (length < protocol::SetupFlyweight::HEADER_LENGTH)
            return false;

        session_id = get_le32(buffer + 12);
        stream_id = get_le32(buffer + 16);
        initial_term_id = get_le32(buffer + 20);
        active_term_id = get_le32(buffer + 24);
        term_length = get_le32(buffer + 28);
        mtu_length = get_le32(buffer + 32);
        ttl = get_le32(buffer + 36);

        return true;
    }

    /// Extract RTT Measurement fields from a raw buffer.
    [[nodiscard]] static bool parse_rtt_measurement(
        const std::byte* buffer, i32 length,
        i32& session_id, i32& stream_id,
        i64& echo_timestamp, i64& reception_delta, i64& receiver_id, bool& is_reply)
    {
        if (length < protocol::RttMeasurementFlyweight::HEADER_LENGTH)
            return false;

        u8 flags = static_cast<u8>(buffer[5]);
        session_id = get_le32(buffer + 8);
        stream_id = get_le32(buffer + 12);
        echo_timestamp = get_le64(buffer + 16);
        reception_delta = get_le64(buffer + 24);
        receiver_id = get_le64(buffer + 32);
        is_reply = (flags & protocol::RttMeasurementFlyweight::REPLY_FLAG) != 0;

        return true;
    }

    // --- Frame dispatch (called by DataTransportPoller) ---

    /// Dispatch a received DATA frame. Parses the header and extracts fields.
    /// Returns true if the frame was successfully parsed.
    [[nodiscard]] bool dispatch_data_frame(
        const std::byte* buffer, i32 length,
        const struct sockaddr_storage& src_address)
    {
        i32 session_id = 0, stream_id = 0, term_id = 0;
        i32 term_offset = 0, frame_length = 0;
        u8 flags = 0;

        if (!parse_data_header(buffer, length,
                               session_id, stream_id, term_id,
                               term_offset, frame_length, flags))
            return false;

        // Store last received frame info for diagnostic access
        last_data_session_id_.store(session_id, std::memory_order_relaxed);
        last_data_stream_id_.store(stream_id, std::memory_order_relaxed);
        last_data_term_id_.store(term_id, std::memory_order_relaxed);
        last_data_frame_length_.store(frame_length, std::memory_order_relaxed);
        last_data_flags_.store(flags, std::memory_order_relaxed);
        data_frame_count_.fetch_add(1, std::memory_order_relaxed);

        return true;
    }

    /// Dispatch a received SETUP frame. Parses the header and extracts fields.
    [[nodiscard]] bool dispatch_setup_frame(
        const std::byte* buffer, i32 length,
        const struct sockaddr_storage& src_address)
    {
        i32 session_id = 0, stream_id = 0;
        i32 initial_term_id = 0, active_term_id = 0;
        i32 term_length = 0, mtu_length = 0, ttl = 0;

        if (!parse_setup_header(buffer, length,
                                session_id, stream_id,
                                initial_term_id, active_term_id,
                                term_length, mtu_length, ttl))
            return false;

        last_setup_session_id_.store(session_id, std::memory_order_relaxed);
        last_setup_stream_id_.store(stream_id, std::memory_order_relaxed);
        setup_frame_count_.fetch_add(1, std::memory_order_relaxed);

        return true;
    }

    /// Dispatch a received RTT measurement. Parses the header and extracts fields.
    [[nodiscard]] bool dispatch_rtt_measurement(
        const std::byte* buffer, i32 length,
        const struct sockaddr_storage& src_address)
    {
        i32 session_id = 0, stream_id = 0;
        i64 echo_timestamp = 0, reception_delta = 0, receiver_id = 0;
        bool is_reply = false;

        if (!parse_rtt_measurement(buffer, length,
                                   session_id, stream_id,
                                   echo_timestamp, reception_delta,
                                   receiver_id, is_reply))
            return false;

        last_rtt_session_id_.store(session_id, std::memory_order_relaxed);
        last_rtt_stream_id_.store(stream_id, std::memory_order_relaxed);
        rtt_frame_count_.fetch_add(1, std::memory_order_relaxed);

        return true;
    }

    // Diagnostic accessors for dispatched frame counts
    [[nodiscard]] i64 data_frame_count() const noexcept { return data_frame_count_.load(std::memory_order_relaxed); }
    [[nodiscard]] i64 setup_frame_count() const noexcept { return setup_frame_count_.load(std::memory_order_relaxed); }
    [[nodiscard]] i64 rtt_frame_count() const noexcept { return rtt_frame_count_.load(std::memory_order_relaxed); }

    [[nodiscard]] i32 last_data_session_id() const noexcept { return last_data_session_id_.load(std::memory_order_relaxed); }
    [[nodiscard]] i32 last_data_stream_id() const noexcept { return last_data_stream_id_.load(std::memory_order_relaxed); }
    [[nodiscard]] i32 last_setup_session_id() const noexcept { return last_setup_session_id_.load(std::memory_order_relaxed); }
    [[nodiscard]] i32 last_setup_stream_id() const noexcept { return last_setup_stream_id_.load(std::memory_order_relaxed); }

    // --- Send control frames ---

    /// Send a status message to all image connections.
    void send_status_message(
        const std::vector<ImageConnection>& connections,
        i32 session_id, i32 stream_id, i32 term_id,
        i32 term_offset, i32 window_length, u8 flags)
    {
        // Build SM frame in pre-allocated buffer
        auto* buf = sm_buffer_.data();

        // Frame length: 36 bytes (without group tag) or 44 bytes (with group tag)
        const bool has_group = group_tag_.has_value();
        const i32 frame_length = has_group
            ? protocol::StatusMessageFlyweight::HEADER_LENGTH_WITH_GROUP_TAG
            : protocol::StatusMessageFlyweight::HEADER_LENGTH;

        // Determine effective flags (add HAS_GROUP_ID_FLAG if group tag present)
        const u8 effective_flags = has_group
            ? static_cast<u8>(flags | protocol::StatusMessageFlyweight::HAS_GROUP_ID_FLAG)
            : static_cast<u8>(flags & 0xFF);

        // Write common header (explicit little-endian)
        put_le32(buf, frame_length);
        buf[4] = std::byte{0};  // version
        buf[5] = static_cast<std::byte>(effective_flags);
        put_le16(buf + 6, protocol::HeaderFlyweight::HDR_TYPE_SM);

        // SM fields (explicit little-endian)
        put_le32(buf + 8, session_id);
        put_le32(buf + 12, stream_id);
        put_le32(buf + 16, term_id);
        put_le32(buf + 20, term_offset);
        put_le32(buf + 24, window_length);
        put_le64(buf + 28, receiver_id_);

        if (has_group)
        {
            put_le64(buf + 36, group_tag_.value());
        }

        // Send to all connections
        for (const auto& conn : connections)
        {
            if (!conn.is_eos)
            {
                try
                {
                    (void)send_to(reinterpret_cast<const std::byte*>(buf), frame_length,
                                  conn.control_address);
                }
                catch (const std::exception& e)
                {
                    std::fprintf(stderr, "[ReceiveChannelEndpoint] send_status_message failed: %s\n", e.what());
                }
            }
        }
    }

    /// Send a NAK message to all image connections.
    void send_nak_message(
        const std::vector<ImageConnection>& connections,
        i32 session_id, i32 stream_id, i32 term_id,
        i32 term_offset, i32 nak_length)
    {
        auto* buf = nak_buffer_.data();
        constexpr i32 frame_length = protocol::NakFlyweight::HEADER_LENGTH;

        put_le32(buf, frame_length);
        buf[4] = std::byte{0};  // version
        buf[5] = std::byte{0};
        put_le16(buf + 6, protocol::HeaderFlyweight::HDR_TYPE_NAK);
        put_le32(buf + 8, session_id);
        put_le32(buf + 12, stream_id);
        put_le32(buf + 16, term_id);
        put_le32(buf + 20, term_offset);
        put_le32(buf + 24, nak_length);

        for (const auto& conn : connections)
        {
            if (!conn.is_eos)
            {
                try
                {
                    (void)send_to(reinterpret_cast<const std::byte*>(buf), frame_length,
                                  conn.control_address);
                }
                catch (const std::exception& e)
                {
                    std::fprintf(stderr, "[ReceiveChannelEndpoint] send_nak_message failed: %s\n", e.what());
                }
            }
        }
    }

    /// Send an RTT measurement to all image connections.
    void send_rtt_measurement(
        const std::vector<ImageConnection>& connections,
        i32 session_id, i32 stream_id,
        i64 echo_timestamp_ns, i64 reception_delta, bool is_reply)
    {
        auto* buf = rtt_buffer_.data();
        constexpr i32 frame_length = protocol::RttMeasurementFlyweight::HEADER_LENGTH;

        put_le32(buf, frame_length);
        buf[4] = std::byte{0};  // version
        buf[5] = is_reply ? std::byte{protocol::RttMeasurementFlyweight::REPLY_FLAG} : std::byte{0};
        put_le16(buf + 6, protocol::HeaderFlyweight::HDR_TYPE_RTTM);
        put_le32(buf + 8, session_id);
        put_le32(buf + 12, stream_id);
        put_le64(buf + 16, echo_timestamp_ns);
        put_le64(buf + 24, reception_delta);
        put_le64(buf + 32, receiver_id_);

        for (const auto& conn : connections)
        {
            if (!conn.is_eos)
            {
                try
                {
                    (void)send_to(reinterpret_cast<const std::byte*>(buf), frame_length,
                                  conn.control_address);
                }
                catch (const std::exception& e)
                {
                    std::fprintf(stderr, "[ReceiveChannelEndpoint] send_rtt_measurement failed: %s\n", e.what());
                }
            }
        }
    }

    /// Send an error frame to all image connections.
    void send_error_frame(
        const std::vector<ImageConnection>& connections,
        i32 session_id, i32 stream_id,
        i32 error_code, const std::string& error_message)
    {
        auto* buf = error_buffer_.data();
        const i32 max_msg_len = static_cast<i32>(error_buffer_.size()) - protocol::ErrorFlyweight::HEADER_LENGTH;
        const auto msg_len = std::min(static_cast<i32>(error_message.size()), max_msg_len);
        const i32 frame_length = protocol::ErrorFlyweight::HEADER_LENGTH + msg_len;

        put_le32(buf, frame_length);
        buf[4] = std::byte{0};  // version
        buf[5] = std::byte{0};
        put_le16(buf + 6, protocol::HeaderFlyweight::HDR_TYPE_ERR);
        put_le32(buf + 8, session_id);
        put_le32(buf + 12, stream_id);
        put_le64(buf + 16, receiver_id_);
        // Group tag at offset 24 (8 bytes, zeroed)
        put_le64(buf + 24, 0);
        put_le32(buf + 32, error_code);
        put_le32(buf + 36, msg_len);
        if (msg_len > 0)
            std::memcpy(buf + 40, error_message.data(), static_cast<size_t>(msg_len));

        for (const auto& conn : connections)
        {
            if (!conn.is_eos)
            {
                try
                {
                    (void)send_to(reinterpret_cast<const std::byte*>(buf), frame_length,
                                  conn.control_address);
                }
                catch (const std::exception& e)
                {
                    std::fprintf(stderr, "[ReceiveChannelEndpoint] send_error_frame failed: %s\n", e.what());
                }
            }
        }
    }

    /// Send a response setup frame to all image connections.
    void send_response_setup(
        const std::vector<ImageConnection>& connections,
        i32 session_id, i32 stream_id, i32 response_session_id)
    {
        auto* buf = response_setup_buffer_.data();
        constexpr i32 frame_length = protocol::ResponseSetupFlyweight::HEADER_LENGTH;

        put_le32(buf, frame_length);
        buf[4] = std::byte{0};  // version
        buf[5] = std::byte{0};
        put_le16(buf + 6, protocol::HeaderFlyweight::HDR_TYPE_RSP_SETUP);
        put_le32(buf + 8, session_id);
        put_le32(buf + 12, stream_id);
        put_le32(buf + 16, response_session_id);

        for (const auto& conn : connections)
        {
            if (!conn.is_eos)
            {
                try
                {
                    (void)send_to(reinterpret_cast<const std::byte*>(buf), frame_length,
                                  conn.control_address);
                }
                catch (const std::exception& e)
                {
                    std::fprintf(stderr, "[ReceiveChannelEndpoint] send_response_setup failed: %s\n", e.what());
                }
            }
        }
    }

    // --- MDS destination management ---

    [[nodiscard]] i32 add_destination(std::unique_ptr<ReceiveDestinationTransport> transport)
    {
        if (!multi_rcv_destination_)
            multi_rcv_destination_ = std::make_unique<MultiRcvDestination>();
        return multi_rcv_destination_->add_destination(std::move(transport));
    }

    void remove_destination(i32 transport_index)
    {
        if (multi_rcv_destination_)
            multi_rcv_destination_->remove_destination(transport_index);
    }

    [[nodiscard]] bool has_destination(i32 transport_index) const
    {
        return multi_rcv_destination_ && multi_rcv_destination_->has_destination(transport_index);
    }

    [[nodiscard]] ReceiveDestinationTransport* destination(i32 transport_index)
    {
        return multi_rcv_destination_ ? multi_rcv_destination_->transport(transport_index) : nullptr;
    }

    // Accessors
    [[nodiscard]] bool has_explicit_control() const noexcept
    {
        return udp_channel().has_explicit_control();
    }

    [[nodiscard]] const struct sockaddr_storage* explicit_control_address() const
    {
        return has_explicit_control() ? &udp_channel().remote_control() : nullptr;
    }

    [[nodiscard]] bool has_destination_control() const noexcept
    {
        return multi_rcv_destination_ != nullptr;
    }

    [[nodiscard]] i64 receiver_id() const noexcept { return receiver_id_; }
    [[nodiscard]] std::optional<i64> group_tag() const noexcept { return group_tag_; }
    [[nodiscard]] i64 status_indicator_counter_id() const noexcept
    {
        return status_indicator_counter_id_;
    }

    void inc_image_ref() noexcept { image_ref_count_.fetch_add(1, std::memory_order_relaxed); }
    void dec_image_ref() noexcept
    {
        int expected = image_ref_count_.load(std::memory_order_relaxed);
        while (expected > 0 && !image_ref_count_.compare_exchange_weak(expected, expected - 1,
                                                                       std::memory_order_relaxed))
        {}
    }

private:
    [[nodiscard]] static i64 generate_receiver_id()
    {
        static std::atomic<i64> counter{1};
        return counter.fetch_add(1, std::memory_order_relaxed);
    }

    MultiRcvDestination* multi_rcv_destination_ptr()
    {
        return multi_rcv_destination_.get();
    }

    std::unique_ptr<MultiRcvDestination> multi_rcv_destination_;
    i64 status_indicator_counter_id_ = 0;
    i64 receiver_id_ = 0;
    std::optional<i64> group_tag_;

    // Reference count maps (protected by ref_count_mutex_)
    mutable std::mutex ref_count_mutex_;
    std::unordered_map<i32, i32> ref_count_by_stream_id_;
    std::unordered_map<i64, i64> ref_count_by_stream_and_session_;
    std::unordered_map<i32, i32> response_ref_count_by_stream_id_;
    std::atomic<int> image_ref_count_{0};

    // Dispatch state (last received frame info for diagnostics, atomic for thread safety)
    std::atomic<i32> last_data_session_id_{0};
    std::atomic<i32> last_data_stream_id_{0};
    std::atomic<i32> last_data_term_id_{0};
    std::atomic<i32> last_data_frame_length_{0};
    std::atomic<u8> last_data_flags_{0};
    std::atomic<i64> data_frame_count_{0};

    std::atomic<i32> last_setup_session_id_{0};
    std::atomic<i32> last_setup_stream_id_{0};
    std::atomic<i64> setup_frame_count_{0};

    std::atomic<i32> last_rtt_session_id_{0};
    std::atomic<i32> last_rtt_stream_id_{0};
    std::atomic<i64> rtt_frame_count_{0};

    // Pre-allocated send buffers (aligned for cache line isolation)
    alignas(64) std::array<std::byte, 128> sm_buffer_{};
    alignas(64) std::array<std::byte, 64> nak_buffer_{};
    alignas(64) std::array<std::byte, 64> rtt_buffer_{};
    alignas(64) std::array<std::byte, 256> error_buffer_{};
    alignas(64) std::array<std::byte, 32> response_setup_buffer_{};
};

} // namespace caeron::driver::media
