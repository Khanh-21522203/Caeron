#include "caeron/protocol/rtt_measurement_flyweight.h"
#include "caeron/protocol/header_flyweight.h"

#include <gtest/gtest.h>
#include <array>

using namespace caeron;
using namespace caeron::concurrent;
using namespace caeron::protocol;

TEST(RttMeasurementFlyweight, HeaderLength)
{
    EXPECT_EQ(RttMeasurementFlyweight::HEADER_LENGTH, 40);
}

TEST(RttMeasurementFlyweight, AllFieldAccessors)
{
    std::array<std::byte, 256> storage{};
    UnsafeBuffer buf{storage};
    RttMeasurementFlyweight rtt{buf};

    rtt.set_frame_length(40)
       .set_version(0)
       .set_flags(RttMeasurementFlyweight::REPLY_FLAG)
       .set_type(HeaderFlyweight::HDR_TYPE_RTTM)
       .set_session_id(200)
       .set_stream_id(10)
       .set_echo_timestamp(1234567890LL)
       .set_reception_delta(500LL)
       .set_receiver_id(0x9988776655443322LL);

    EXPECT_EQ(rtt.frame_length(), 40);
    EXPECT_EQ(rtt.flags(), RttMeasurementFlyweight::REPLY_FLAG);
    EXPECT_EQ(rtt.type(), HeaderFlyweight::HDR_TYPE_RTTM);
    EXPECT_EQ(rtt.session_id(), 200);
    EXPECT_EQ(rtt.stream_id(), 10);
    EXPECT_EQ(rtt.echo_timestamp(), 1234567890LL);
    EXPECT_EQ(rtt.reception_delta(), 500LL);
    EXPECT_EQ(rtt.receiver_id(), 0x9988776655443322LL);
}
