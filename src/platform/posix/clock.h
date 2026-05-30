#pragma once

#include "caeron/common/types.h"

#include <time.h>

namespace caeron::platform {

/// Returns the current time from CLOCK_MONOTONIC in nanoseconds.
[[nodiscard]] inline i64 nano_time()
{
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<i64>(ts.tv_sec) * 1'000'000'000LL + static_cast<i64>(ts.tv_nsec);
}

/// Returns the current time from CLOCK_REALTIME in milliseconds since epoch.
[[nodiscard]] inline i64 epoch_time()
{
    struct timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    return static_cast<i64>(ts.tv_sec) * 1'000LL + static_cast<i64>(ts.tv_nsec / 1'000'000);
}

} // namespace caeron::platform
