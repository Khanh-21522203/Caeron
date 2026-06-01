#pragma once

#include "caeron/common/types.h"
#include "caeron/driver/media/network_util.h"
#include "caeron/driver/media/resolved_interface.h"
#include "caeron/driver/media/socket_address_parser.h"

#include <cstring>
#include <netinet/in.h>
#include <string>
#include <string_view>
#include <stdexcept>

namespace caeron::driver::media {

/// Model an interface search specification like "192.168.1.0/24" or "0.0.0.0:40456/16".
///
/// Combines an address, optional prefix length, and optional port. Used to specify
/// which network interface to bind to for publication or subscription.
class InterfaceSearchAddress
{
public:
    /// Parse "address:port", "address/prefix", "address:port/prefix", or
    /// "[ipv6]:port/prefix". Sets prefix_length to addr_len*8 if no /prefix.
    [[nodiscard]] static InterfaceSearchAddress parse(std::string_view str)
    {
        InterfaceSearchAddress result;
        std::string_view remaining = str;

        // Extract optional /prefix suffix
        auto slash_pos = remaining.rfind('/');
        if (slash_pos != std::string_view::npos)
        {
            auto prefix_str = remaining.substr(slash_pos + 1);
            result.prefix_length_ = parse_prefix_length(prefix_str);
            remaining = remaining.substr(0, slash_pos);
        }

        // Parse host:port. Handle three cases:
        // 1. IPv6 bracket notation: [::1] or [::1]:port
        // 2. No colon (plain IPv4 host): port defaults to 0
        // 3. Has colon (host:port or [ipv6]:port): delegate to socket_address_parser
        ParsedSocketAddress parsed;
        if (!remaining.empty() && remaining.front() == '[')
        {
            // IPv6 bracket notation — find closing bracket
            auto close_bracket = remaining.find(']');
            if (close_bracket == std::string_view::npos)
                throw std::invalid_argument("missing closing bracket in: " + std::string(remaining));

            parsed.host = std::string(remaining.substr(1, close_bracket - 1));

            // Check for port after ']'
            if (close_bracket + 1 < remaining.size())
            {
                auto after = remaining.substr(close_bracket + 1);
                if (after.empty() || after.front() != ':')
                    throw std::invalid_argument("expected ':port' after ']': " + std::string(remaining));
                parsed.port = socket_address_parser::parse_port(after.substr(1));
            }
            else
            {
                parsed.port = 0;
            }
        }
        else
        {
            auto colon_pos = remaining.find(':');
            if (colon_pos == std::string_view::npos)
            {
                parsed.host = std::string(remaining);
                parsed.port = 0;
            }
            else
            {
                parsed = socket_address_parser::parse(remaining);
            }
        }
        result.port_ = parsed.port;
        result.address_ = socket_address_parser::resolve_host(parsed.host, result.port_);

        // If no prefix specified, default to full mask
        if (slash_pos == std::string_view::npos)
        {
            result.prefix_length_ = (result.address_.ss_family == AF_INET) ? 32 : 128;
        }

        // Validate prefix length against address family
        if (result.address_.ss_family == AF_INET && result.prefix_length_ > 32)
        {
            throw std::invalid_argument(
                "IPv4 prefix length " + std::to_string(result.prefix_length_) +
                " exceeds maximum of 32");
        }

        return result;
    }

    /// Return the 0.0.0.0/0 (IPv4) wildcard search address.
    [[nodiscard]] static InterfaceSearchAddress wildcard()
    {
        InterfaceSearchAddress result;
        std::memset(&result.address_, 0, sizeof(result.address_));
        result.address_.ss_family = AF_INET;
        result.prefix_length_ = 0;
        result.port_ = 0;
        return result;
    }

    [[nodiscard]] const struct sockaddr_storage& address() const noexcept { return address_; }
    [[nodiscard]] int prefix_length() const noexcept { return prefix_length_; }
    [[nodiscard]] u16 port() const noexcept { return port_; }

    /// Resolve to a concrete network interface + bound local address.
    /// For unicast with any-local address, returns null interface.
    /// For multicast, finds an interface matching the subnet.
    [[nodiscard]] ResolvedInterface resolve(bool multicast, int protocol_family) const
    {
        (void)multicast;

        struct sockaddr_storage local_addr = address_;
        socket_address_parser::set_port(local_addr, port_);

        if (socket_address_parser::is_any_address(address_))
        {
            // Any-address: bind to any, no specific interface.
            // Ensure the returned address family matches the requested protocol family.
            // E.g., wildcard() returns AF_INET, but AF_INET6 channel needs :: not 0.0.0.0.
            local_addr.ss_family = static_cast<sa_family_t>(protocol_family);
            if (protocol_family == AF_INET6)
            {
                auto* a6 = reinterpret_cast<struct sockaddr_in6*>(&local_addr);
                std::memset(a6, 0, sizeof(*a6));
                a6->sin6_family = AF_INET6;
                a6->sin6_port = htons(port_);
            }
            else
            {
                auto* a4 = reinterpret_cast<struct sockaddr_in*>(&local_addr);
                std::memset(a4, 0, sizeof(*a4));
                a4->sin_family = AF_INET;
                a4->sin_port = htons(port_);
            }
            return ResolvedInterface{0, "", local_addr, false};
        }

        // Validate that the search address family matches the requested protocol family
        if (static_cast<int>(address_.ss_family) != protocol_family)
        {
            throw std::invalid_argument(
                "search address family does not match requested protocol family");
        }

        // Try to find a matching local interface
        auto match = network_util::find_first_matching_local_address(address_, prefix_length_);
        if (match)
        {
            auto interfaces = network_util::filter_by_subnet(address_, prefix_length_);
            // Filter by protocol_family to avoid socket family mismatch
            for (const auto& iface : interfaces)
            {
                if (static_cast<int>(iface.addr.ss_family) == protocol_family)
                {
                    return ResolvedInterface{iface.index, iface.name, local_addr, true};
                }
            }

            // Matching subnet found but no interface with the right protocol family
            throw std::invalid_argument(
                "no interface found with matching protocol family for address");
        }

        return ResolvedInterface{0, "", local_addr, false};
    }

private:
    static int parse_prefix_length(std::string_view str)
    {
        if (str.empty())
            throw std::invalid_argument("empty prefix length");

        unsigned int val = 0;
        for (char c : str)
        {
            if (c < '0' || c > '9')
                throw std::invalid_argument("non-numeric prefix length: " + std::string(str));
            val = val * 10 + static_cast<unsigned int>(c - '0');
            if (val > 128)
                throw std::invalid_argument("prefix length out of range: " + std::string(str));
        }
        return static_cast<int>(val);
    }

    struct sockaddr_storage address_{};
    int prefix_length_ = 0;
    u16 port_ = 0;
};

} // namespace caeron::driver::media
