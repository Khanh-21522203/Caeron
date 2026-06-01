#pragma once

#include "caeron/common/types.h"

#include <netinet/in.h>
#include <sys/socket.h>

namespace caeron::driver::media {

class UdpChannel;  // forward declaration to avoid circular dependency

/// Abstract interface for dynamic port allocation within a configured range.
class PortManager
{
public:
    virtual ~PortManager() = default;

    /// Called before bind. Returns the address to actually bind to.
    /// Throws std::runtime_error if bind should not proceed.
    virtual struct sockaddr_storage get_managed_port(
        const UdpChannel& udp_channel,
        const struct sockaddr_storage& bind_address) = 0;

    /// Called after the socket is closed.
    virtual void free_managed_port(const struct sockaddr_storage& bind_address) = 0;
};

} // namespace caeron::driver::media
