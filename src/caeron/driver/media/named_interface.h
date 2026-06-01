#pragma once

#include "caeron/common/types.h"
#include "caeron/driver/media/network_util.h"
#include "caeron/driver/media/resolved_interface.h"
#include "caeron/driver/media/socket_address_parser.h"

#include <cstring>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <string>
#include <string_view>
#include <stdexcept>

namespace caeron::driver::media {

/// Interface specification by name, e.g. "{eth0}:40456".
class NamedInterface
{
public:
    static constexpr char OPENING_CHAR = '{';

    /// Parse "{name}:port" or "{name}" (port defaults to 0).
    [[nodiscard]] static NamedInterface parse(std::string_view str)
    {
        if (str.empty() || str.front() != OPENING_CHAR)
            throw std::invalid_argument("NamedInterface must start with '{'");

        const auto close = str.find('}');
        if (close == std::string_view::npos)
            throw std::invalid_argument("missing closing '}' in named interface");

        auto name = std::string(str.substr(1, close - 1));
        if (name.empty())
            throw std::invalid_argument("empty interface name");

        u16 port = 0;
        if (close + 1 < str.size())
        {
            if (str[close + 1] != ':')
                throw std::invalid_argument("expected ':' after '}' in named interface");
            auto port_str = str.substr(close + 2);
            port = socket_address_parser::parse_port(port_str);
        }

        return NamedInterface(std::move(name), port);
    }

    /// Resolve by looking up the interface by name via if_nametoindex(),
    /// then finding the first address of the requested protocol family.
    [[nodiscard]] ResolvedInterface resolve(bool multicast, int protocol_family) const
    {
        const auto index = static_cast<int>(::if_nametoindex(name_.c_str()));
        if (index == 0)
            throw std::runtime_error("interface not found: " + name_);

        auto all = network_util::get_network_interfaces();
        for (const auto& iface : all)
        {
            if (iface.name != name_)
                continue;
            if (iface.addr.ss_family != static_cast<sa_family_t>(protocol_family))
                continue;

            auto addr = iface.addr;
            socket_address_parser::set_port(addr, port_);

            return ResolvedInterface{index, name_, addr, true};
        }

        throw std::runtime_error("no address found for interface " + name_ +
                                 " with family " + std::to_string(protocol_family));
    }

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] u16 port() const noexcept { return port_; }

private:
    NamedInterface(std::string name, u16 port)
        : name_{std::move(name)}, port_{port}
    {}

    std::string name_;
    u16 port_ = 0;
};

} // namespace caeron::driver::media
