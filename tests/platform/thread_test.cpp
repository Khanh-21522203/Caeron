#include "platform/posix/thread.h"

#include <gtest/gtest.h>

#include <atomic>

using namespace caeron::platform;

TEST(Thread, RunsFunction)
{
    std::atomic<bool> ran{false};

    Thread t([&]() { ran.store(true); });
    t.join();

    EXPECT_TRUE(ran.load());
}

TEST(Thread, JoinWaitsForCompletion)
{
    std::atomic<int> counter{0};

    Thread t([&]() {
        for (int i = 0; i < 1000; ++i)
            counter.fetch_add(1);
    });
    t.join();

    EXPECT_EQ(counter.load(), 1000);
}

TEST(Thread, SetNameDoesNotCrash)
{
    Thread t([&]() {});
    t.set_name("test-thread");
    t.join();
}

TEST(Thread, SetAffinityDoesNotCrash)
{
    Thread t([&]() {});
    t.set_affinity(0);
    t.join();
}

TEST(Thread, MoveConstruct)
{
    // Note: moving a Thread while running is unsafe — the thread's self->func_()
    // will access the moved-from object. Move is only safe after join().
    std::atomic<bool> ran{false};

    Thread t1([&]() { ran.store(true); });
    t1.join();
    EXPECT_TRUE(ran.load());

    // Move a completed thread — tests that move semantics work
    Thread t2 = std::move(t1);
    EXPECT_FALSE(t2.joinable());  // already joined
}

TEST(Thread, DestructorAfterJoinDoesNotCrash)
{
    // After join(), destructor should not try to detach again
    Thread t([]() {});
    t.join();
    // Destructor runs here — should be safe since already joined
}
