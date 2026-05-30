#pragma once

#include "caeron/common/types.h"
#include "platform/posix/clock.h"

namespace caeron::concurrent {

/// Caches the current epoch time (milliseconds) for the duration of a duty cycle.
///
/// The driver calls update() once per duty cycle. All subsequent calls to
/// time() return the cached value without hitting clock_gettime().
class CachedEpochClock
{
public:
    /// Returns the cached epoch time (milliseconds since Unix epoch).
    [[nodiscard]] i64 time() const noexcept { return cached_time_; }

    /// Update the cached time from the system clock.
    /// Should be called once per duty cycle (e.g., at the top of the agent loop).
    void update() noexcept { cached_time_ = platform::epoch_time(); }

    /// Reset the cache to 0.
    void reset() noexcept { cached_time_ = 0; }

private:
    i64 cached_time_ = 0;
};

} // namespace caeron::concurrent
