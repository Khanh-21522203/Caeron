#pragma once

#include "caeron/common/types.h"

#include <arpa/inet.h>
#include <cstring>
#include <net/if.h>
#include <netinet/in.h>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <stdexcept>

namespace caeron::driver::media {

/// Result of parsing a "host:port" or "[ipv6]:port" string.
struct ParsedSocketAddress
{
    std::string host;
    u16 port = 0;
};

/// Helpers for parsing "host:port" strings into structured socket addresses.
namespace socket_address_parser {

/// Parse a port number string. Throws on invalid or out-of-range.
inline u16 parse_port(std::string_view port_str)
{
    if (port_str.empty())
        throw std::invalid_argument("empty port number");

    unsigned long val = 0;
    for (char c : port_str)
    {
        if (c < '0' || c > '9')
            throw std::invalid_argument("non-numeric port: " + std::string(port_str));
        val = val * 10 + static_cast<unsigned long>(c - '0');
        if (val > 65535)
            throw std::invalid_argument("port out of range: " + std::string(port_str));
    }

    return static_cast<u16>(val);
}

/// Parse "host:port" or "[ipv6%scope]:port" into host string + port number.
/// Throws std::invalid_argument on malformed input.
[[nodiscard]] inline ParsedSocketAddress parse(std::string_view value)
{
    if (value.empty())
        throw std::invalid_argument("empty socket address");

    ParsedSocketAddress result;

    if (value.front() == '[')
    {
        // IPv6 bracket notation: [host]:port or [host%scope]:port
        const auto close_bracket = value.find(']');
        if (close_bracket == std::string_view::npos)
            throw std::invalid_argument("missing closing bracket in IPv6 address");

        result.host = std::string(value.substr(1, close_bracket - 1));

        if (close_bracket + 1 >= value.size() || value[close_bracket + 1] != ':')
            throw std::invalid_argument("missing port separator after IPv6 address");

        result.port = parse_port(value.substr(close_bracket + 2));
    }
    else
    {
        // IPv4: host:port -- find last ':'
        const auto colon_pos = value.rfind(':');
        if (colon_pos == std::string_view::npos)
            throw std::invalid_argument("missing port separator in socket address");

        result.host = std::string(value.substr(0, colon_pos));
        result.port = parse_port(value.substr(colon_pos + 1));
    }

    if (result.host.empty())
        throw std::invalid_argument("empty host in socket address");

    return result;
}

/// Check whether the address portion of "host:port" is a multicast address.
/// IPv4: first octet & 0xF0 == 0xE0 (224-239).
/// IPv6: first two hex chars == "ff".
[[nodiscard]] inline bool is_multicast_address(std::string_view host_and_port)
{
    auto parsed = parse(host_and_port);
    const auto& host = parsed.host;

    struct in_addr addr4{};
    if (::inet_pton(AF_INET, host.c_str(), &addr4) == 1)
    {
        const auto first_octet = reinterpret_cast<const u8*>(&addr4.s_addr)[0];
        return (first_octet & 0xF0) == 0xE0;
    }

    // Strip scope ID (e.g., "%eth0") before inet_pton for IPv6
    std::string host_clean(host);
    auto pct = host_clean.find('%');
    if (pct != std::string::npos)
        host_clean = host_clean.substr(0, pct);

    struct in6_addr addr6{};
    if (::inet_pton(AF_INET6, host_clean.c_str(), &addr6) == 1)
    {
        return addr6.s6_addr[0] == 0xFF;
    }

    throw std::invalid_argument(
        "cannot determine multicast status of unparseable address: " + std::string(host));
}

/// Resolve a host string to a sockaddr_storage.
/// Supports IPv4 and IPv6 literal addresses (not DNS names).
/// Handles scoped IPv6 addresses (e.g., "fe80::1%eth0") by stripping the scope ID
/// before calling inet_pton() and setting sin6_scope_id on the result.
inline struct sockaddr_storage resolve_host(const std::string& host, u16 port)
{
    struct sockaddr_storage addr{};
    std::memset(&addr, 0, sizeof(addr));

    auto* addr4 = reinterpret_cast<struct sockaddr_in*>(&addr);
    if (::inet_pton(AF_INET, host.c_str(), &addr4->sin_addr) == 1)
    {
        addr4->sin_family = AF_INET;
        addr4->sin_port = htons(port);
        return addr;
    }

    // For IPv6, strip scope ID (everything from '%' to end) before inet_pton()
    std::string ipv6_addr = host;
    std::string scope_id_str;
    auto pct_pos = host.find('%');
    if (pct_pos != std::string::npos)
    {
        ipv6_addr = host.substr(0, pct_pos);
        scope_id_str = host.substr(pct_pos + 1);
    }

    auto* addr6 = reinterpret_cast<struct sockaddr_in6*>(&addr);
    if (::inet_pton(AF_INET6, ipv6_addr.c_str(), &addr6->sin6_addr) == 1)
    {
        addr6->sin6_family = AF_INET6;
        addr6->sin6_port = htons(port);

        // Resolve scope ID to interface index
        if (!scope_id_str.empty())
        {
            unsigned int scope_id = 0;
            // Check if it's a numeric scope ID
            bool is_numeric = true;
            for (char c : scope_id_str)
            {
                if (c < '0' || c > '9')
                {
                    is_numeric = false;
                    break;
                }
            }
            if (is_numeric && !scope_id_str.empty())
            {
                scope_id = static_cast<unsigned int>(std::stoul(scope_id_str));
            }
            else
            {
                // It's an interface name -- resolve via if_nametoindex()
                scope_id = ::if_nametoindex(scope_id_str.c_str());
                if (scope_id == 0)
                    throw std::invalid_argument(
                        "unknown interface name for scope ID: " + scope_id_str);
            }
            addr6->sin6_scope_id = scope_id;
        }

        return addr;
    }

    throw std::invalid_argument("cannot resolve address: " + host);
}

/// Return the port stored in a sockaddr_storage.
[[nodiscard]] inline u16 get_port(const struct sockaddr_storage& addr)
{
    if (addr.ss_family == AF_INET)
        return ntohs(reinterpret_cast<const struct sockaddr_in*>(&addr)->sin_port);
    if (addr.ss_family == AF_INET6)
        return ntohs(reinterpret_cast<const struct sockaddr_in6*>(&addr)->sin6_port);
    return 0;
}

/// Set the port in a sockaddr_storage.
inline void set_port(struct sockaddr_storage& addr, u16 port)
{
    if (addr.ss_family == AF_INET)
        reinterpret_cast<struct sockaddr_in*>(&addr)->sin_port = htons(port);
    else if (addr.ss_family == AF_INET6)
        reinterpret_cast<struct sockaddr_in6*>(&addr)->sin6_port = htons(port);
}

/// Format address and port as "ip:port" or "[ipv6]:port".
[[nodiscard]] inline std::string format_address_and_port(const struct sockaddr_storage& addr)
{
    char buf[INET6_ADDRSTRLEN]{};

    if (addr.ss_family == AF_INET)
    {
        const auto* a4 = reinterpret_cast<const struct sockaddr_in*>(&addr);
        ::inet_ntop(AF_INET, &a4->sin_addr, buf, sizeof(buf));
        return std::string(buf) + ":" + std::to_string(ntohs(a4->sin_port));
    }
    if (addr.ss_family == AF_INET6)
    {
        const auto* a6 = reinterpret_cast<const struct sockaddr_in6*>(&addr);
        ::inet_ntop(AF_INET6, &a6->sin6_addr, buf, sizeof(buf));
        std::string result = "[" + std::string(buf);
        if (a6->sin6_scope_id != 0)
            result += "%" + std::to_string(a6->sin6_scope_id);
        result += "]:" + std::to_string(ntohs(a6->sin6_port));
        return result;
    }
    return "<unknown>";
}

/// Check if a sockaddr_storage represents any-local address (0.0.0.0 or ::).
[[nodiscard]] inline bool is_any_address(const struct sockaddr_storage& addr)
{
    if (addr.ss_family == AF_INET)
    {
        const auto* a4 = reinterpret_cast<const struct sockaddr_in*>(&addr);
        return a4->sin_addr.s_addr == INADDR_ANY;
    }
    if (addr.ss_family == AF_INET6)
    {
        const auto* a6 = reinterpret_cast<const struct sockaddr_in6*>(&addr);
        return IN6_IS_ADDR_UNSPECIFIED(&a6->sin6_addr);
    }
    return false;
}

/// Compare two sockaddr_storage for equality (address only, ignoring port).
[[nodiscard]] inline bool addresses_equal(const struct sockaddr_storage& a,
                                          const struct sockaddr_storage& b)
{
    if (a.ss_family != b.ss_family)
        return false;

    if (a.ss_family == AF_INET)
    {
        const auto* a4 = reinterpret_cast<const struct sockaddr_in*>(&a);
        const auto* b4 = reinterpret_cast<const struct sockaddr_in*>(&b);
        return a4->sin_addr.s_addr == b4->sin_addr.s_addr;
    }
    if (a.ss_family == AF_INET6)
    {
        const auto* a6 = reinterpret_cast<const struct sockaddr_in6*>(&a);
        const auto* b6 = reinterpret_cast<const struct sockaddr_in6*>(&b);
        return std::memcmp(&a6->sin6_addr, &b6->sin6_addr, 16) == 0 &&
               a6->sin6_scope_id == b6->sin6_scope_id;
    }
    return false;
}

/// Compare two sockaddr_storage for equality including port.
[[nodiscard]] inline bool addresses_and_ports_equal(const struct sockaddr_storage& a,
                                                    const struct sockaddr_storage& b)
{
    return addresses_equal(a, b) && get_port(a) == get_port(b);
}

} // namespace socket_address_parser
} // namespace caeron::driver::media
