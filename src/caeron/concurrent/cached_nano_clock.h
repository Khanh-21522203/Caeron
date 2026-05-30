#pragma once

#include "caeron/common/types.h"
#include "caeron/platform/posix/clock.h"

namespace caeron::concurrent {

/// Caches the current nano time (nanoseconds, monotonic) for the duration of a duty cycle.
///
/// The driver calls update() once per duty cycle. All subsequent calls to
/// nano() return the cached value without hitting clock_gettime().
class CachedNanoClock
{
public:
    /// Returns the cached nano time (CLOCK_MONOTONIC nanoseconds).
    [[nodiscard]] i64 nano() const noexcept { return cached_nano_; }

    /// Update the cached time from the system clock.
    /// Should be called once per duty cycle (e.g., at the top of the agent loop).
    void update() noexcept { cached_nano_ = caeron::platform::nano_time(); }

    /// Reset the cache to 0.
    void reset() noexcept { cached_nano_ = 0; }

private:
    i64 cached_nano_ = 0;
};

} // namespace caeron::concurrent
