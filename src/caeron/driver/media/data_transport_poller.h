#pragma once

#include "caeron/common/endian.h"
#include "caeron/common/types.h"
#include "caeron/driver/media/receive_channel_endpoint.h"
#include "caeron/driver/media/udp_channel_transport.h"
#include "platform/posix/epoll_poller.h"

#include <list>
#include <sys/epoll.h>
#include <vector>

namespace caeron::driver::media {

/// Maximum UDP payload length (IPv4 max - headers).
inline constexpr i32 MAX_UDP_PAYLOAD_LENGTH = 65504;

/// Poll registered receive transports for data frames (DATA, SETUP, RTT)
/// and dispatch to the appropriate endpoint callbacks.
///
/// Uses epoll for I/O multiplexing. The poller reads incoming datagrams,
/// determines the frame type from the common header, and dispatches to
/// the ReceiveChannelEndpoint's parsing methods.
class DataTransportPoller
{
public:
    explicit DataTransportPoller(platform::EpollPoller& poller)
        : poller_{poller}
    {}

    ~DataTransportPoller() = default;

    DataTransportPoller(const DataTransportPoller&) = delete;
    DataTransportPoller& operator=(const DataTransportPoller&) = delete;

    /// Register a receive transport for data reading.
    void register_for_read(
        ReceiveChannelEndpoint& endpoint,
        UdpChannelTransport& transport,
        i32 transport_index)
    {
        ChannelAndTransport entry{&endpoint, &transport, transport_index};

        // Add to epoll first -- if this throws, no stale entry is left in the deque.
        // We need a temporary pointer that will remain valid after push_back.
        channel_and_transports_.push_back(entry);
        try
        {
            poller_.add(transport.receive_fd(), EPOLLIN,
                        &channel_and_transports_.back());
        }
        catch (...)
        {
            // Remove the stale entry if epoll registration fails
            channel_and_transports_.pop_back();
            throw;
        }
    }

    /// Cancel reading for a specific transport.
    void cancel_read(
        const ReceiveChannelEndpoint& endpoint,
        const UdpChannelTransport& transport)
    {
        for (auto it = channel_and_transports_.begin();
             it != channel_and_transports_.end(); ++it)
        {
            if (it->channel_endpoint == &endpoint &&
                it->transport == &transport)
            {
                poller_.remove(transport.receive_fd());
                channel_and_transports_.erase(it);
                return;
            }
        }
    }

    /// Cancel all transports for a given endpoint.
    void cancel_read_for_all_transports(const ReceiveChannelEndpoint& endpoint)
    {
        for (auto it = channel_and_transports_.begin();
             it != channel_and_transports_.end(); )
        {
            if (it->channel_endpoint == &endpoint)
            {
                poller_.remove(it->transport->receive_fd());
                it = channel_and_transports_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    /// Poll all registered transports. Returns total bytes received.
    [[nodiscard]] i32 poll_transports()
    {
        total_bytes_received_ = 0;

        if (channel_and_transports_.empty())
            return 0;

        // For few transports, poll directly; for many, use epoll
        if (channel_and_transports_.size() <= ITERATION_THRESHOLD)
        {
            poll_direct();
        }
        else
        {
            poll_epoll();
        }

        return total_bytes_received_;
    }

private:
    static constexpr size_t ITERATION_THRESHOLD = 4;

    struct ChannelAndTransport
    {
        ReceiveChannelEndpoint* channel_endpoint;
        UdpChannelTransport* transport;
        i32 transport_index;
    };

    /// Direct poll for few transports -- check each FD with recvfrom.
    void poll_direct()
    {
        for (auto& entry : channel_and_transports_)
        {
            struct sockaddr_storage src_address{};
            i32 bytes_received = 0;

            while (entry.transport->receive(
                byte_buffer_.data(), MAX_UDP_PAYLOAD_LENGTH,
                src_address, bytes_received))
            {
                if (bytes_received <= 0)
                    break;

                total_bytes_received_ += bytes_received;

                // Dispatch based on frame type
                dispatch_frame(byte_buffer_.data(), bytes_received,
                               src_address, entry);
            }
        }
    }

    /// Epoll-based poll for many transports.
    void poll_epoll()
    {
        std::vector<struct epoll_event> events(channel_and_transports_.size());
        auto n = poller_.poll(events, 0);

        for (i32 i = 0; i < n; ++i)
        {
            auto* entry = static_cast<ChannelAndTransport*>(events[i].data.ptr);
            if (!entry || !entry->transport)
                continue;

            struct sockaddr_storage src_address{};
            i32 bytes_received = 0;

            while (entry->transport->receive(
                byte_buffer_.data(), MAX_UDP_PAYLOAD_LENGTH,
                src_address, bytes_received))
            {
                if (bytes_received <= 0)
                    break;

                total_bytes_received_ += bytes_received;

                dispatch_frame(byte_buffer_.data(), bytes_received,
                               src_address, *entry);
            }
        }
    }

    /// Dispatch a received frame based on its type.
    void dispatch_frame(
        const std::byte* buffer, i32 length,
        const struct sockaddr_storage& src_address,
        const ChannelAndTransport& entry)
    {
        if (length < 8)
            return;

        // Validate frame
        if (!entry.transport->is_valid_frame(buffer, length))
            return;

        // Read frame type at offset 6 (explicit little-endian)
        u16 type = get_le16(buffer + 6);

        switch (type)
        {
        case protocol::HeaderFlyweight::HDR_TYPE_DATA:
            // Data frame -- parse header and dispatch to the endpoint
            (void)entry.channel_endpoint->dispatch_data_frame(buffer, length, src_address);
            break;

        case protocol::HeaderFlyweight::HDR_TYPE_SETUP:
            // Setup frame -- parse header and dispatch to the endpoint
            (void)entry.channel_endpoint->dispatch_setup_frame(buffer, length, src_address);
            break;

        case protocol::HeaderFlyweight::HDR_TYPE_RTTM:
            // RTT measurement -- parse and dispatch to the endpoint
            (void)entry.channel_endpoint->dispatch_rtt_measurement(buffer, length, src_address);
            break;

        default:
            // Unknown frame type -- ignore
            break;
        }
    }

    platform::EpollPoller& poller_;
    // Use list: guarantees pointer/reference/iterator stability across all
    // mutations (push_back, erase, splice). Critical because we store raw
    // pointers into this container as epoll user-data. deque::erase can
    // invalidate references to other elements.
    std::list<ChannelAndTransport> channel_and_transports_;
    alignas(64) std::array<std::byte, MAX_UDP_PAYLOAD_LENGTH> byte_buffer_{};
    i32 total_bytes_received_ = 0;
};

} // namespace caeron::driver::media
