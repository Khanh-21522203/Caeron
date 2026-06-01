#pragma once

#include "caeron/common/types.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cstring>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <optional>
#include <string>
#include <sys/socket.h>
#include <vector>

namespace caeron::driver::media {

/// Information about a local network interface.
struct InterfaceInfo
{
    std::string name;
    int index = 0;
    struct sockaddr_storage addr{};
    socklen_t addr_len = 0;
    int prefix_length = 0;
    bool is_loopback = false;
    bool is_multicast_capable = false;
    bool is_up = false;
};

/// Network interface enumeration and address matching utilities.
namespace network_util {

/// Enumerate all local interfaces with their addresses.
[[nodiscard]] inline std::vector<InterfaceInfo> get_network_interfaces()
{
    std::vector<InterfaceInfo> result;
    struct ifaddrs* ifaddr = nullptr;

    if (::getifaddrs(&ifaddr) == -1)
        return result;

    for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_addr == nullptr)
            continue;

        const auto family = ifa->ifa_addr->sa_family;
        if (family != AF_INET && family != AF_INET6)
            continue;

        InterfaceInfo info;
        info.name = ifa->ifa_name ? ifa->ifa_name : "";
        info.index = static_cast<int>(::if_nametoindex(ifa->ifa_name));
        info.is_loopback = (ifa->ifa_flags & IFF_LOOPBACK) != 0;
        info.is_multicast_capable = (ifa->ifa_flags & IFF_MULTICAST) != 0;
        info.is_up = (ifa->ifa_flags & IFF_UP) != 0;

        if (family == AF_INET)
        {
            std::memcpy(&info.addr, ifa->ifa_addr, sizeof(struct sockaddr_in));
            info.addr_len = sizeof(struct sockaddr_in);
            info.addr.ss_family = AF_INET;

            // Compute prefix length from netmask
            if (ifa->ifa_netmask != nullptr)
            {
                const auto* mask = reinterpret_cast<const struct sockaddr_in*>(ifa->ifa_netmask);
                info.prefix_length = __builtin_popcount(mask->sin_addr.s_addr);
            }
        }
        else if (family == AF_INET6)
        {
            std::memcpy(&info.addr, ifa->ifa_addr, sizeof(struct sockaddr_in6));
            info.addr_len = sizeof(struct sockaddr_in6);
            info.addr.ss_family = AF_INET6;

            // Compute prefix length from netmask
            if (ifa->ifa_netmask != nullptr)
            {
                const auto* mask = reinterpret_cast<const struct sockaddr_in6*>(ifa->ifa_netmask);
                int count = 0;
                for (int i = 0; i < 16; ++i)
                    count += __builtin_popcount(mask->sin6_addr.s6_addr[i]);
                info.prefix_length = count;
            }
        }

        result.push_back(std::move(info));
    }

    ::freeifaddrs(ifaddr);
    return result;
}

/// Check if candidate address matches expected address for prefix_length bits.
/// Supports AF_INET (4-byte) and AF_INET6 (16-byte).
[[nodiscard]] inline bool is_match_with_prefix(
    const std::byte* candidate, const std::byte* expected,
    int addr_len, int prefix_length)
{
    if (prefix_length <= 0)
        return true;
    if (prefix_length > addr_len * 8)
        prefix_length = addr_len * 8;

    const auto full_bytes = static_cast<size_t>(prefix_length / 8);
    const auto remaining_bits = static_cast<unsigned>(prefix_length % 8);

    if (full_bytes > 0)
    {
        if (std::memcmp(candidate, expected, full_bytes) != 0)
            return false;
    }

    if (remaining_bits > 0 && full_bytes < static_cast<size_t>(addr_len))
    {
        const auto mask = static_cast<u8>(0xFF << (8 - remaining_bits));
        auto c = static_cast<u8>(candidate[full_bytes]);
        auto e = static_cast<u8>(expected[full_bytes]);
        if ((c & mask) != (e & mask))
            return false;
    }

    return true;
}

/// Filter interfaces whose addresses match the given address/prefix.
/// Results ordered by prefix_length descending (most specific first).
[[nodiscard]] inline std::vector<InterfaceInfo> filter_by_subnet(
    const struct sockaddr_storage& address, int subnet_prefix)
{
    auto all = get_network_interfaces();
    std::vector<InterfaceInfo> result;

    const auto family = address.ss_family;
    const int addr_len = (family == AF_INET) ? 4 : 16;

    const std::byte* target = nullptr;
    if (family == AF_INET)
        target = reinterpret_cast<const std::byte*>(
            &reinterpret_cast<const struct sockaddr_in*>(&address)->sin_addr);
    else
        target = reinterpret_cast<const std::byte*>(
            &reinterpret_cast<const struct sockaddr_in6*>(&address)->sin6_addr);

    for (auto& iface : all)
    {
        if (iface.addr.ss_family != family)
            continue;

        const std::byte* iface_addr = nullptr;
        if (family == AF_INET)
            iface_addr = reinterpret_cast<const std::byte*>(
                &reinterpret_cast<const struct sockaddr_in*>(&iface.addr)->sin_addr);
        else
            iface_addr = reinterpret_cast<const std::byte*>(
                &reinterpret_cast<const struct sockaddr_in6*>(&iface.addr)->sin6_addr);

        if (is_match_with_prefix(iface_addr, target, addr_len, subnet_prefix))
            result.push_back(std::move(iface));
    }

    // Sort by prefix_length descending (most specific first)
    std::sort(result.begin(), result.end(),
              [](const InterfaceInfo& a, const InterfaceInfo& b)
              { return a.prefix_length > b.prefix_length; });

    return result;
}

/// Convert prefix length to IPv4 netmask.
[[nodiscard]] inline u32 prefix_length_to_ipv4_mask(int prefix_length)
{
    if (prefix_length <= 0)
        return 0;
    if (prefix_length >= 32)
        return 0xFFFFFFFF;
    return ~((1u << (32 - prefix_length)) - 1);
}

/// Get the protocol family (AF_INET or AF_INET6) from a sockaddr.
[[nodiscard]] inline int get_protocol_family(const struct sockaddr_storage& addr)
{
    return static_cast<int>(addr.ss_family);
}

/// Find first local address matching the given address/subnet.
[[nodiscard]] inline std::optional<struct sockaddr_storage>
find_first_matching_local_address(
    const struct sockaddr_storage& address, int subnet_prefix)
{
    auto matches = filter_by_subnet(address, subnet_prefix);
    for (const auto& iface : matches)
    {
        if (iface.is_up)
            return iface.addr;
    }
    return std::nullopt;
}

/// Create a sockaddr_storage from a sockaddr pointer (copies the correct size).
[[nodiscard]] inline struct sockaddr_storage sockaddr_to_storage(
    const struct sockaddr* sa)
{
    struct sockaddr_storage result{};
    std::memset(&result, 0, sizeof(result));

    if (sa->sa_family == AF_INET)
        std::memcpy(&result, sa, sizeof(struct sockaddr_in));
    else if (sa->sa_family == AF_INET6)
        std::memcpy(&result, sa, sizeof(struct sockaddr_in6));

    return result;
}

} // namespace network_util
} // namespace caeron::driver::media
