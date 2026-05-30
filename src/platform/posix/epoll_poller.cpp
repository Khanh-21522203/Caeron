#include "epoll_poller.h"

#include <unistd.h>

#include <stdexcept>

namespace caeron::platform {

EpollPoller::EpollPoller()
{
    epoll_fd_ = ::epoll_create1(0);
    if (epoll_fd_ < 0)
        throw std::runtime_error("Failed to create epoll instance");
}

EpollPoller::~EpollPoller()
{
    if (epoll_fd_ >= 0)
        ::close(epoll_fd_);
}

EpollPoller::EpollPoller(EpollPoller&& other) noexcept
    : epoll_fd_{other.epoll_fd_}
{
    other.epoll_fd_ = -1;
}

EpollPoller& EpollPoller::operator=(EpollPoller&& other) noexcept
{
    if (this != &other)
    {
        if (epoll_fd_ >= 0)
            ::close(epoll_fd_);
        epoll_fd_ = other.epoll_fd_;
        other.epoll_fd_ = -1;
    }
    return *this;
}

void EpollPoller::add(int fd, u32 events, void* ptr)
{
    struct epoll_event ev{};
    ev.events = events;
    if (ptr)
        ev.data.ptr = ptr;
    else
        ev.data.fd = fd;

    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) < 0)
        throw std::runtime_error("Failed to add fd to epoll");
}

void EpollPoller::modify(int fd, u32 events, void* ptr)
{
    struct epoll_event ev{};
    ev.events = events;
    if (ptr)
        ev.data.ptr = ptr;
    else
        ev.data.fd = fd;

    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) < 0)
        throw std::runtime_error("Failed to modify fd in epoll");
}

void EpollPoller::remove(int fd)
{
    ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
}

i32 EpollPoller::poll(std::vector<struct epoll_event>& events, i32 timeout_ms)
{
    auto n = ::epoll_wait(epoll_fd_, events.data(),
                          static_cast<int>(events.size()), timeout_ms);
    if (n < 0)
    {
        if (errno == EINTR)
            return 0;
        throw std::runtime_error("epoll_wait failed");
    }
    return static_cast<i32>(n);
}

} // namespace caeron::platform
