#pragma once

#include "caeron/common/types.h"
#include "caeron/driver/media/send_channel_endpoint.h"
#include "caeron/driver/media/udp_channel_transport.h"
#include "caeron/protocol/header_flyweight.h"
#include "platform/posix/epoll_poller.h"

#include <cerrno>
#include <cstring>
#include <list>
#include <sys/epoll.h>
#include <vector>

namespace caeron::driver::media {

/// Poll registered send transports for control frames (SM, NAK, RTT, Error,
/// ResponseSetup) and dispatch to the appropriate SendChannelEndpoint callbacks.
class ControlTransportPoller
{
public:
    explicit ControlTransportPoller(platform::EpollPoller& poller)
        : poller_{poller}
    {}

    ~ControlTransportPoller() = default;

    ControlTransportPoller(const ControlTransportPoller&) = delete;
    ControlTransportPoller& operator=(const ControlTransportPoller&) = delete;

    /// Register a send endpoint for control frame reading.
    void register_for_read(SendChannelEndpoint& endpoint)
    {
        Transport entry{&endpoint};

        // For unicast, use the send_fd; for multicast, use receive_fd
        int fd = endpoint.is_multicast()
            ? endpoint.receive_fd()
            : endpoint.send_fd();

        if (fd >= 0)
        {
            // Add to epoll first -- if this throws, no stale entry is left in the deque.
            transports_.push_back(entry);
            try
            {
                poller_.add(fd, EPOLLIN, &transports_.back());
            }
            catch (...)
            {
                // Remove the stale entry if epoll registration fails
                transports_.pop_back();
                throw;
            }
        }
    }

    /// Cancel reading for a send endpoint.
    void cancel_read(const SendChannelEndpoint& endpoint)
    {
        for (auto it = transports_.begin(); it != transports_.end(); ++it)
        {
            if (it->endpoint == &endpoint)
            {
                int fd = endpoint.is_multicast()
                    ? endpoint.receive_fd()
                    : endpoint.send_fd();
                if (fd >= 0)
                    poller_.remove(fd);
                transports_.erase(it);
                return;
            }
        }
    }

    /// Poll all registered transports. Returns total bytes received.
    [[nodiscard]] i32 poll_transports()
    {
        total_bytes_received_ = 0;

        if (transports_.empty())
            return 0;

        if (transports_.size() <= ITERATION_THRESHOLD)
            poll_direct();
        else
            poll_epoll();

        return total_bytes_received_;
    }

private:
    static constexpr size_t ITERATION_THRESHOLD = 4;

    struct Transport
    {
        SendChannelEndpoint* endpoint;
    };

    void poll_direct()
    {
        for (auto& entry : transports_)
        {
            int fd = entry.endpoint->is_multicast()
                ? entry.endpoint->receive_fd()
                : entry.endpoint->send_fd();

            if (fd < 0)
                continue;

            poll_fd(fd, entry);
        }
    }

    void poll_epoll()
    {
        std::vector<struct epoll_event> events(transports_.size());
        auto n = poller_.poll(events, 0);

        for (i32 i = 0; i < n; ++i)
        {
            auto* entry = static_cast<Transport*>(events[i].data.ptr);
            if (!entry || !entry->endpoint)
                continue;

            int fd = entry->endpoint->is_multicast()
                ? entry->endpoint->receive_fd()
                : entry->endpoint->send_fd();

            if (fd >= 0)
                poll_fd(fd, *entry);
        }
    }

    void poll_fd(int fd, Transport& entry)
    {
        struct sockaddr_storage src_address{};

        for (;;)
        {
            socklen_t addr_len = sizeof(src_address);
            auto received = ::recvfrom(fd, recv_buffer_.data(), recv_buffer_.size(), 0,
                                       reinterpret_cast<struct sockaddr*>(&src_address),
                                       &addr_len);

            if (received < 0)
            {
                // EAGAIN/EWOULDBLOCK/EINTR are expected on non-blocking sockets
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
                    break;
                // Real error -- log and break
                break;
            }

            if (received == 0)
                break;

            if (received < 8)
                continue;

            // Validate frame before dispatch (consistent with DataTransportPoller)
            if (!entry.endpoint->is_valid_frame(
                    recv_buffer_.data(), static_cast<i32>(received)))
                continue;

            total_bytes_received_ += static_cast<i32>(received);

            // Dispatch to endpoint
            i32 session_id = 0, stream_id = 0;
            entry.endpoint->dispatch_control_frame(
                recv_buffer_.data(), static_cast<i32>(received),
                src_address, session_id, stream_id);
        }
    }

    static constexpr i32 MAX_UDP_PAYLOAD_LENGTH = 65504;

    platform::EpollPoller& poller_;
    // Use list: guarantees pointer/reference/iterator stability across all
    // mutations. Critical because we store raw pointers as epoll user-data.
    std::list<Transport> transports_;
    alignas(64) std::array<std::byte, MAX_UDP_PAYLOAD_LENGTH> recv_buffer_{};
    i32 total_bytes_received_ = 0;
};

} // namespace caeron::driver::media
