#include "caeron/protocol/setup_flyweight.h"
#include "caeron/protocol/header_flyweight.h"

#include <gtest/gtest.h>
#include <array>

using namespace caeron;
using namespace caeron::concurrent;
using namespace caeron::protocol;

TEST(SetupFlyweight, HeaderLength)
{
    EXPECT_EQ(SetupFlyweight::HEADER_LENGTH, 40);
}

TEST(SetupFlyweight, AllFieldAccessors)
{
    std::array<std::byte, 256> storage{};
    UnsafeBuffer buf{storage};
    SetupFlyweight sf{buf};

    sf.set_frame_length(40)
      .set_version(0)
      .set_flags(0x40)
      .set_type(HeaderFlyweight::HDR_TYPE_SETUP)
      .set_term_offset(0)
      .set_session_id(0x12345678)
      .set_stream_id(100)
      .set_initial_term_id(5)
      .set_active_term_id(5)
      .set_term_length(65536)
      .set_mtu_length(1408)
      .set_ttl(0);

    EXPECT_EQ(sf.frame_length(), 40);
    EXPECT_EQ(sf.type(), HeaderFlyweight::HDR_TYPE_SETUP);
    EXPECT_EQ(sf.session_id(), 0x12345678);
    EXPECT_EQ(sf.stream_id(), 100);
    EXPECT_EQ(sf.initial_term_id(), 5);
    EXPECT_EQ(sf.active_term_id(), 5);
    EXPECT_EQ(sf.term_length(), 65536);
    EXPECT_EQ(sf.mtu_length(), 1408);
    EXPECT_EQ(sf.ttl(), 0);
}

TEST(SetupFlyweight, BinaryLayoutCompatibility)
{
    std::array<std::byte, 256> storage{};
    UnsafeBuffer buf{storage};
    SetupFlyweight sf{buf};

    // Write at raw offsets matching Java Aeron
    buf.put_i32(20, 10);   // initial_term_id at offset 20
    buf.put_i32(24, 10);   // active_term_id at offset 24
    buf.put_i32(28, 4096); // term_length at offset 28
    buf.put_i32(32, 1408); // mtu_length at offset 32
    buf.put_i32(36, 2);    // ttl at offset 36

    EXPECT_EQ(sf.initial_term_id(), 10);
    EXPECT_EQ(sf.active_term_id(), 10);
    EXPECT_EQ(sf.term_length(), 4096);
    EXPECT_EQ(sf.mtu_length(), 1408);
    EXPECT_EQ(sf.ttl(), 2);
}
