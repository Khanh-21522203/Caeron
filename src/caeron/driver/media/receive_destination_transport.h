#pragma once

#include "caeron/common/types.h"
#include "caeron/driver/media/udp_channel.h"
#include "caeron/driver/media/udp_channel_transport.h"

#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>

namespace caeron::driver::media {

class ReceiveChannelEndpoint;  // forward declaration

/// Per-destination transport for MDS (multi-destination subscription) mode.
///
/// Each ReceiveDestinationTransport is a separate UDP socket for a distinct
/// multicast group or unicast endpoint in a multi-destination subscription.
class ReceiveDestinationTransport : public UdpChannelTransport
{
public:
    ReceiveDestinationTransport(
        const UdpChannel& udp_channel,
        int socket_rcvbuf_length,
        int socket_sndbuf_length)
        : UdpChannelTransport(
            udp_channel,
            udp_channel.remote_data(),
            udp_channel.local_data(),
            nullptr,  // no connect address for receive
            nullptr,  // no port manager
            socket_rcvbuf_length,
            socket_sndbuf_length)
    {
        // Initialize explicit-control state from the UdpChannel
        has_explicit_control_ = udp_channel.has_explicit_control();
        if (has_explicit_control_)
        {
            current_control_address_ = udp_channel.remote_control();
        }
    }

    ~ReceiveDestinationTransport() override = default;

    /// Activity tracking
    [[nodiscard]] i64 time_of_last_activity_ns() const noexcept
    {
        return time_of_last_activity_ns_;
    }

    void set_time_of_last_activity_ns(i64 now_ns) noexcept
    {
        time_of_last_activity_ns_ = now_ns;
    }

    /// Control address for explicit control channels
    [[nodiscard]] bool has_explicit_control() const noexcept
    {
        return has_explicit_control_;
    }

    [[nodiscard]] const struct sockaddr_storage* current_control_address() const
    {
        return has_explicit_control_ ? &current_control_address_ : nullptr;
    }

    void set_current_control_address(const struct sockaddr_storage& new_address)
    {
        current_control_address_ = new_address;
        has_explicit_control_ = true;
    }

    [[nodiscard]] const UdpChannel& udp_channel() const noexcept
    {
        return udp_channel_;
    }

private:
    struct sockaddr_storage current_control_address_{};
    i64 time_of_last_activity_ns_ = 0;
    bool has_explicit_control_ = false;
};

} // namespace caeron::driver::media
