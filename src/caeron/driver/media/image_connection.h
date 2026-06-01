#pragma once

#include "caeron/common/types.h"

#include <atomic>
#include <climits>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>

namespace caeron::driver::media {

/// Per-source connection state tracking for liveness and control address.
///
/// Hot fields (time_of_last_activity_ns, time_of_last_frame_ns) are placed on
/// separate cache lines via alignas(64) to prevent false sharing when accessed
/// from different threads (receiver agent vs. conductor agent).
/// These fields are atomic to prevent data races on concurrent access.
struct ImageConnection
{
    alignas(64) std::atomic<i64> time_of_last_activity_ns{0};
    alignas(64) std::atomic<i64> time_of_last_frame_ns{0};
    i64 eos_position = INT64_MAX;
    bool is_eos = false;
    struct sockaddr_storage control_address{};

    ImageConnection() = default;

    ImageConnection(i64 time_of_last_activity,
                    const struct sockaddr_storage& control_addr)
        : time_of_last_activity_ns{time_of_last_activity}
        , control_address{control_addr}
    {}

    // Copy and move semantics for std::atomic
    ImageConnection(const ImageConnection& other)
        : time_of_last_activity_ns{other.time_of_last_activity_ns.load(std::memory_order_relaxed)}
        , time_of_last_frame_ns{other.time_of_last_frame_ns.load(std::memory_order_relaxed)}
        , eos_position{other.eos_position}
        , is_eos{other.is_eos}
        , control_address{other.control_address}
    {}

    ImageConnection& operator=(const ImageConnection& other)
    {
        if (this != &other)
        {
            time_of_last_activity_ns.store(
                other.time_of_last_activity_ns.load(std::memory_order_relaxed),
                std::memory_order_relaxed);
            time_of_last_frame_ns.store(
                other.time_of_last_frame_ns.load(std::memory_order_relaxed),
                std::memory_order_relaxed);
            eos_position = other.eos_position;
            is_eos = other.is_eos;
            control_address = other.control_address;
        }
        return *this;
    }

    ImageConnection(ImageConnection&& other) noexcept
        : time_of_last_activity_ns{
              other.time_of_last_activity_ns.load(std::memory_order_relaxed)}
        , time_of_last_frame_ns{
              other.time_of_last_frame_ns.load(std::memory_order_relaxed)}
        , eos_position{other.eos_position}
        , is_eos{other.is_eos}
        , control_address{other.control_address}
    {}

    ImageConnection& operator=(ImageConnection&& other) noexcept
    {
        if (this != &other)
        {
            time_of_last_activity_ns.store(
                other.time_of_last_activity_ns.load(std::memory_order_relaxed),
                std::memory_order_relaxed);
            time_of_last_frame_ns.store(
                other.time_of_last_frame_ns.load(std::memory_order_relaxed),
                std::memory_order_relaxed);
            eos_position = other.eos_position;
            is_eos = other.is_eos;
            control_address = other.control_address;
        }
        return *this;
    }
};

static_assert(sizeof(ImageConnection) >= 128,
    "ImageConnection must span at least 2 cache lines for padding");

} // namespace caeron::driver::media
