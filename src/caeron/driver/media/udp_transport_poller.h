#pragma once

#include "caeron/common/types.h"

namespace caeron::driver::media {

/// Abstract base for transport pollers.
///
/// This is a thin abstraction; concrete pollers (DataTransportPoller,
/// ControlTransportPoller) compose an EpollPoller rather than inheriting
/// through this base. Included for interface documentation.
class UdpTransportPoller
{
public:
    virtual ~UdpTransportPoller() = default;

    /// Poll all registered transports. Returns total bytes received.
    [[nodiscard]] virtual i32 poll_transports() = 0;
};

} // namespace caeron::driver::media
