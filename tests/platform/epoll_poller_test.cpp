#include "platform/posix/epoll_poller.h"

#include <gtest/gtest.h>

#include <sys/eventfd.h>
#include <unistd.h>

using namespace caeron::platform;

// Helper: RAII eventfd wrapper for testing
class EventFd
{
public:
    EventFd() : fd_(::eventfd(0, EFD_NONBLOCK)) {}
    ~EventFd() { if (fd_ >= 0) ::close(fd_); }

    EventFd(const EventFd&) = delete;
    EventFd& operator=(const EventFd&) = delete;

    void signal()
    {
        uint64_t val = 1;
        ::write(fd_, &val, sizeof(val));
    }

    int fd() const { return fd_; }

private:
    int fd_;
};

TEST(EpollPoller, ConstructorCreatesEpoll)
{
    EpollPoller poller;
    EXPECT_GE(poller.fd(), 0);
}

TEST(EpollPoller, AddAndPollReceivesEvent)
{
    EpollPoller poller;
    EventFd efd;

    poller.add(efd.fd(), EPOLLIN);

    // Signal the eventfd
    efd.signal();

    std::vector<struct epoll_event> events(16);
    auto n = poller.poll(events, 100);
    EXPECT_EQ(n, 1);
    EXPECT_EQ(events[0].data.fd, efd.fd());
}

TEST(EpollPoller, PollTimeoutReturnsZero)
{
    EpollPoller poller;
    EventFd efd;

    poller.add(efd.fd(), EPOLLIN);

    // Don't signal — should timeout
    std::vector<struct epoll_event> events(16);
    auto n = poller.poll(events, 0);
    EXPECT_EQ(n, 0);
}

TEST(EpollPoller, ModifyChangesEvents)
{
    EpollPoller poller;
    EventFd efd;

    poller.add(efd.fd(), EPOLLIN);

    // Modify to use a pointer instead of fd
    int context = 42;
    poller.modify(efd.fd(), EPOLLIN, &context);

    efd.signal();

    std::vector<struct epoll_event> events(16);
    auto n = poller.poll(events, 100);
    EXPECT_EQ(n, 1);
    EXPECT_EQ(events[0].data.ptr, &context);
}

TEST(EpollPoller, RemoveStopsReceivingEvents)
{
    EpollPoller poller;
    EventFd efd;

    poller.add(efd.fd(), EPOLLIN);
    poller.remove(efd.fd());

    efd.signal();

    std::vector<struct epoll_event> events(16);
    auto n = poller.poll(events, 0);
    EXPECT_EQ(n, 0);
}

TEST(EpollPoller, MoveConstruct)
{
    EpollPoller poller1;
    int fd = poller1.fd();

    EpollPoller poller2 = std::move(poller1);

    EXPECT_EQ(poller2.fd(), fd);
    EXPECT_EQ(poller1.fd(), -1);
}

TEST(EpollPoller, MoveAssign)
{
    EpollPoller poller1;
    EpollPoller poller2;

    int fd1 = poller1.fd();
    poller2 = std::move(poller1);

    EXPECT_EQ(poller2.fd(), fd1);
    EXPECT_EQ(poller1.fd(), -1);
}

TEST(EpollPoller, AddMultipleFds)
{
    EpollPoller poller;
    EventFd efd1, efd2;

    poller.add(efd1.fd(), EPOLLIN);
    poller.add(efd2.fd(), EPOLLIN);

    efd1.signal();
    efd2.signal();

    std::vector<struct epoll_event> events(16);
    auto n = poller.poll(events, 100);
    EXPECT_EQ(n, 2);
}
