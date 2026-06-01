#pragma once

#include "caeron/common/types.h"
#include "caeron/driver/media/image_connection.h"
#include "caeron/driver/media/receive_destination_transport.h"
#include "caeron/driver/media/socket_address_parser.h"
#include "caeron/driver/media/udp_channel.h"

#include <limits>
#include <memory>
#include <vector>

namespace caeron::driver::media {

class ReceiveChannelEndpoint;  // forward declaration
class DataTransportPoller;     // forward declaration

/// Manages a sparse array of ReceiveDestinationTransport instances for MDS
/// (multi-destination subscription) receive channels.
class MultiRcvDestination
{
public:
    MultiRcvDestination() = default;
    ~MultiRcvDestination() = default;

    MultiRcvDestination(const MultiRcvDestination&) = delete;
    MultiRcvDestination& operator=(const MultiRcvDestination&) = delete;

    void close()
    {
        for (auto& t : transports_)
        {
            if (t)
                t->close();
        }
    }

    /// Add a destination transport, returns its index.
    [[nodiscard]] i32 add_destination(std::unique_ptr<ReceiveDestinationTransport> transport)
    {
        // Find first null slot
        for (i32 i = 0; i < static_cast<i32>(transports_.size()); ++i)
        {
            if (!transports_[i])
            {
                transports_[i] = std::move(transport);
                return i;
            }
        }
        // No null slot; append
        auto index = static_cast<i32>(transports_.size());
        transports_.push_back(std::move(transport));
        return index;
    }

    /// Remove (null out) destination at index.
    void remove_destination(i32 transport_index)
    {
        if (transport_index >= 0 &&
            transport_index < static_cast<i32>(transports_.size()))
        {
            if (transports_[transport_index])
            {
                transports_[transport_index]->close();
                transports_[transport_index].reset();
            }
        }
    }

    /// Check if a destination exists at index.
    [[nodiscard]] bool has_destination(i32 transport_index) const
    {
        return transport_index >= 0 &&
               transport_index < static_cast<i32>(transports_.size()) &&
               transports_[transport_index] != nullptr;
    }

    /// Get transport at index (may be null).
    [[nodiscard]] ReceiveDestinationTransport* transport(i32 transport_index)
    {
        if (transport_index >= 0 &&
            transport_index < static_cast<i32>(transports_.size()))
            return transports_[transport_index].get();
        return nullptr;
    }

    /// Find transport index matching a given UdpChannel.
    [[nodiscard]] i32 find_transport(const UdpChannel& udp_channel) const
    {
        for (i32 i = 0; i < static_cast<i32>(transports_.size()); ++i)
        {
            if (transports_[i] && transports_[i]->udp_channel() == udp_channel)
                return i;
        }
        return -1;
    }

    /// Send to all active image connections.
    [[nodiscard]] i32 send_to_all(
        const std::vector<ImageConnection>& connections,
        const std::byte* data, i32 length, i64 /*now_ns*/)
    {
        i64 total_sent = 0;
        for (const auto& conn : connections)
        {
            if (conn.is_eos)
                continue;

            for (auto& transport : transports_)
            {
                if (transport)
                {
                    auto sent = transport->send_to(data, length, conn.control_address);
                    if (sent > 0)
                        total_sent += sent;
                }
            }
        }
        // Saturating cast: clamp to i32 max to avoid truncation overflow
        if (total_sent > std::numeric_limits<i32>::max())
            return std::numeric_limits<i32>::max();
        return static_cast<i32>(total_sent);
    }

    /// Static helper to send via a single transport.
    [[nodiscard]] static i32 send_to(
        UdpChannelTransport& transport,
        const std::byte* data, i32 length,
        const struct sockaddr_storage& dest_address)
    {
        return transport.send_to(data, length, dest_address);
    }

    /// Update control address at index.
    void update_control_address(i32 transport_index,
                                const struct sockaddr_storage& new_address)
    {
        auto* t = transport(transport_index);
        if (t)
            t->set_current_control_address(new_address);
    }

private:
    std::vector<std::unique_ptr<ReceiveDestinationTransport>> transports_;
};

} // namespace caeron::driver::media
