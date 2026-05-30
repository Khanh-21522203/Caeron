#pragma once

#include "caeron/common/types.h"

#include <sys/epoll.h>
#include <vector>

namespace caeron::platform {

/// RAII wrapper for Linux epoll.
class EpollPoller
{
public:
    EpollPoller();
    ~EpollPoller();

    EpollPoller(EpollPoller&& other) noexcept;
    EpollPoller& operator=(EpollPoller&& other) noexcept;
    EpollPoller(const EpollPoller&) = delete;
    EpollPoller& operator=(const EpollPoller&) = delete;

    void add(int fd, u32 events, void* ptr = nullptr);
    void modify(int fd, u32 events, void* ptr = nullptr);
    void remove(int fd);

    /// Wait for events. Returns number of ready file descriptors.
    [[nodiscard]] i32 poll(std::vector<struct epoll_event>& events, i32 timeout_ms);

    [[nodiscard]] int fd() const noexcept { return epoll_fd_; }

private:
    int epoll_fd_ = -1;
};

} // namespace caeron::platform
