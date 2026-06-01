#pragma once

#include "caeron/common/types.h"

#include <cstring>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>

namespace caeron::driver::media {

/// Value type holding a resolved network interface index and bound local address.
///
/// Used to pass around a concrete interface binding after address/interface
/// resolution. The interface_index is suitable for IP_MULTICAST_IF / IPV6_MULTICAST_IF
/// socket options.
struct ResolvedInterface
{
    int interface_index = 0;            // 0 means no specific interface
    std::string interface_name;         // e.g. "eth0", empty if none
    struct sockaddr_storage address{};  // resolved local address (port is 0 or set)
    bool has_interface = false;

    ResolvedInterface() = default;

    ResolvedInterface(int index, std::string name,
                      const struct sockaddr_storage& addr, bool has_iface)
        : interface_index{index}
        , interface_name{std::move(name)}
        , address{addr}
        , has_interface{has_iface}
    {}
};

} // namespace caeron::driver::media
