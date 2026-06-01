#pragma once

#include "caeron/driver/media/interface_search_address.h"
#include "caeron/driver/media/named_interface.h"
#include "caeron/driver/media/resolved_interface.h"

#include <string_view>
#include <variant>

namespace caeron::driver::media {

/// Sealed interface representing either an InterfaceSearchAddress or a NamedInterface.
/// Uses std::variant as the C++ idiom for sum types.
using UnresolvedInterface = std::variant<InterfaceSearchAddress, NamedInterface>;

/// Factory: parse "{name}:port" -> NamedInterface, otherwise -> InterfaceSearchAddress.
[[nodiscard]] inline UnresolvedInterface parse_interface(std::string_view str)
{
    if (!str.empty() && str.front() == NamedInterface::OPENING_CHAR)
        return NamedInterface::parse(str);
    return InterfaceSearchAddress::parse(str);
}

/// Resolve the variant to a concrete interface.
[[nodiscard]] inline ResolvedInterface resolve_interface(
    const UnresolvedInterface& iface, bool multicast, int protocol_family)
{
    return std::visit(
        [&](const auto& v) -> ResolvedInterface
        {
            return v.resolve(multicast, protocol_family);
        },
        iface);
}

} // namespace caeron::driver::media
