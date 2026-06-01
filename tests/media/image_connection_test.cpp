#include "caeron/driver/media/image_connection.h"

#include <gtest/gtest.h>
#include <vector>

using namespace caeron::driver::media;

TEST(ImageConnection, DefaultConstruction)
{
    ImageConnection conn;
    EXPECT_EQ(conn.time_of_last_activity_ns, 0);
    EXPECT_EQ(conn.time_of_last_frame_ns, 0);
    EXPECT_EQ(conn.eos_position, INT64_MAX);
    EXPECT_FALSE(conn.is_eos);
}

TEST(ImageConnection, ParameterizedConstruction)
{
    struct sockaddr_storage addr{};
    addr.ss_family = AF_INET;

    ImageConnection conn(12345, addr);
    EXPECT_EQ(conn.time_of_last_activity_ns, 12345);
    EXPECT_EQ(conn.time_of_last_frame_ns, 0);
    EXPECT_FALSE(conn.is_eos);
    EXPECT_EQ(conn.control_address.ss_family, AF_INET);
}

TEST(ImageConnection, SizeConstraint)
{
    // The static_assert in the header enforces this at compile time,
    // but verify at runtime too
    EXPECT_GE(sizeof(ImageConnection), 128);
}

TEST(ImageConnection, EosTracking)
{
    ImageConnection conn;
    conn.is_eos = true;
    conn.eos_position = 1024;
    EXPECT_TRUE(conn.is_eos);
    EXPECT_EQ(conn.eos_position, 1024);
}

// HIGH-2: Verify that atomic fields work correctly with copy and move semantics.
TEST(ImageConnection, AtomicFieldsCopyAndMove)
{
    struct sockaddr_storage addr{};
    addr.ss_family = AF_INET;

    ImageConnection original(100, addr);
    original.time_of_last_frame_ns.store(200, std::memory_order_relaxed);

    // Copy construction
    ImageConnection copy(original);
    EXPECT_EQ(copy.time_of_last_activity_ns.load(std::memory_order_relaxed), 100);
    EXPECT_EQ(copy.time_of_last_frame_ns.load(std::memory_order_relaxed), 200);

    // Copy assignment
    ImageConnection assigned;
    assigned = original;
    EXPECT_EQ(assigned.time_of_last_activity_ns.load(std::memory_order_relaxed), 100);
    EXPECT_EQ(assigned.time_of_last_frame_ns.load(std::memory_order_relaxed), 200);

    // Move construction
    ImageConnection moved(std::move(original));
    EXPECT_EQ(moved.time_of_last_activity_ns.load(std::memory_order_relaxed), 100);
    EXPECT_EQ(moved.time_of_last_frame_ns.load(std::memory_order_relaxed), 200);
}

// HIGH-2: Verify atomic fields can be stored and loaded independently.
TEST(ImageConnection, AtomicFieldsStoreLoad)
{
    ImageConnection conn;
    conn.time_of_last_activity_ns.store(12345, std::memory_order_relaxed);
    conn.time_of_last_frame_ns.store(67890, std::memory_order_relaxed);

    EXPECT_EQ(conn.time_of_last_activity_ns.load(std::memory_order_relaxed), 12345);
    EXPECT_EQ(conn.time_of_last_frame_ns.load(std::memory_order_relaxed), 67890);
}

// HIGH-2: Verify that vector<ImageConnection> works with the atomic fields
// (since atomic is not trivially copyable, the copy/move constructors are needed).
TEST(ImageConnection, VectorOfImageConnections)
{
    struct sockaddr_storage addr{};
    addr.ss_family = AF_INET;

    std::vector<ImageConnection> connections;
    connections.emplace_back(100, addr);
    connections.emplace_back(200, addr);

    EXPECT_EQ(connections.size(), 2u);
    EXPECT_EQ(connections[0].time_of_last_activity_ns.load(std::memory_order_relaxed), 100);
    EXPECT_EQ(connections[1].time_of_last_activity_ns.load(std::memory_order_relaxed), 200);
}
