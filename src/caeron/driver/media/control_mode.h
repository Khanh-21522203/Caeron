#pragma once

#include "caeron/common/types.h"

#include <string_view>
#include <stdexcept>

namespace caeron::driver::media {

/// Channel control mode for multi-destination-cast (MDC) channels.
enum class ControlMode : u8
{
    NONE     = 0,
    DYNAMIC  = 1,
    MANUAL   = 2,
    RESPONSE = 3,
};

/// Returns true if the control mode supports multi-destination-cast.
[[nodiscard]] constexpr bool is_multi_destination(ControlMode mode) noexcept
{
    return mode == ControlMode::DYNAMIC || mode == ControlMode::MANUAL;
}

/// Parse a string to a ControlMode. Throws std::invalid_argument on unknown values.
[[nodiscard]] inline ControlMode parse_control_mode(std::string_view value)
{
    if (value.empty() || value == "none")
        return ControlMode::NONE;
    if (value == "dynamic")
        return ControlMode::DYNAMIC;
    if (value == "manual")
        return ControlMode::MANUAL;
    if (value == "response")
        return ControlMode::RESPONSE;
    throw std::invalid_argument(std::string("unknown control mode: ").append(value));
}

} // namespace caeron::driver::media
