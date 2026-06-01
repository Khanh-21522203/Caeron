#pragma once

#include "caeron/common/types.h"
#include "caeron/driver/media/port_manager.h"
#include "caeron/driver/media/socket_address_parser.h"
#include "caeron/driver/media/udp_channel.h"

#include <array>
#include <cstring>
#include <netinet/in.h>
#include <stdexcept>
#include <string_view>
#include <sys/socket.h>
#include <unordered_set>

namespace caeron::driver::media {

/// Dynamic port allocation within a configured range.
///
/// When port_range is empty ({0,0}), uses OS wildcard port mode (bind port 0).
/// Otherwise, cycles through ports in the configured range, skipping in-use ports.
class WildcardPortManager final : public PortManager
{
public:
    static constexpr std::array<int, 2> EMPTY_PORT_RANGE = {0, 0};

    /// port_range[0] = low, port_range[1] = high. If both 0, OS wildcard mode.
    WildcardPortManager(const std::array<int, 2>& port_range, bool is_sender)
        : low_port_{port_range[0]}
        , high_port_{port_range[1]}
        , next_port_{port_range[0]}
        , is_os_wildcard_{port_range[0] == 0 && port_range[1] == 0}
        , is_sender_{is_sender}
    {}

    struct sockaddr_storage get_managed_port(
        const UdpChannel& udp_channel,
        const struct sockaddr_storage& bind_address) override
    {
        // OS wildcard mode: just return bind address as-is (port 0)
        if (is_os_wildcard_)
            return bind_address;

        // Sender without explicit control: pass through
        if (is_sender_ && !udp_channel.has_explicit_control())
            return bind_address;

        // If bind address already has a non-zero port, track it and pass through
        const auto port = socket_address_parser::get_port(bind_address);
        if (port != 0)
        {
            port_set_.insert(port);
            return bind_address;
        }

        // Allocate a port from the range
        auto result = bind_address;
        int allocated = allocate_open_port();
        socket_address_parser::set_port(result, static_cast<u16>(allocated));
        return result;
    }

    void free_managed_port(const struct sockaddr_storage& bind_address) override
    {
        const auto port = socket_address_parser::get_port(bind_address);
        if (port >= low_port_ && port <= high_port_)
            port_set_.erase(port);
    }

    /// Parse "low high" string into port range array.
    [[nodiscard]] static std::array<int, 2> parse_port_range(std::string_view value)
    {
        if (value.empty())
            return EMPTY_PORT_RANGE;

        auto space = value.find(' ');
        if (space == std::string_view::npos)
            throw std::invalid_argument("port range must be 'low high'");

        auto low_str = value.substr(0, space);
        auto high_str = value.substr(space + 1);

        int low = std::stoi(std::string(low_str));
        int high = std::stoi(std::string(high_str));

        if (low < 0 || high < 0 || low > high || low > 65535 || high > 65535)
            throw std::invalid_argument("invalid port range");

        return {low, high};
    }

private:
    [[nodiscard]] int find_open_port() const
    {
        int port = next_port_;
        for (int i = low_port_; i <= high_port_; ++i)
        {
            if (port_set_.count(port) == 0)
                return port;
            ++port;
            if (port > high_port_)
                port = low_port_;
        }
        return -1; // all ports in use
    }

    [[nodiscard]] int allocate_open_port()
    {
        int port = find_open_port();
        if (port < 0)
            throw std::runtime_error("all ports in range [" +
                                     std::to_string(low_port_) + ", " +
                                     std::to_string(high_port_) + "] are in use");
        port_set_.insert(port);
        next_port_ = port + 1;
        if (next_port_ > high_port_)
            next_port_ = low_port_;
        return port;
    }

    std::unordered_set<int> port_set_;
    int low_port_;
    int high_port_;
    int next_port_;
    bool is_os_wildcard_;
    bool is_sender_;
};

} // namespace caeron::driver::media
