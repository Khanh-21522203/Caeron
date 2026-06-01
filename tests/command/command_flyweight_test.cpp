#include "caeron/command/publication_message_flyweight.h"
#include "caeron/command/subscription_message_flyweight.h"
#include "caeron/command/remove_publication_flyweight.h"
#include "caeron/command/remove_subscription_flyweight.h"
#include "caeron/command/remove_message_flyweight.h"
#include "caeron/command/correlated_message_flyweight.h"
#include "caeron/command/destination_message_flyweight.h"
#include "caeron/command/destination_by_id_message_flyweight.h"
#include "caeron/command/counter_message_flyweight.h"
#include "caeron/command/terminate_driver_flyweight.h"
#include "caeron/command/reject_image_flyweight.h"
#include "caeron/command/get_next_available_session_id_message_flyweight.h"
#include "caeron/command/static_counter_message_flyweight.h"
#include "caeron/command/error_response_flyweight.h"
#include "caeron/command/publication_buffers_ready_flyweight.h"
#include "caeron/command/subscription_ready_flyweight.h"
#include "caeron/command/image_buffers_ready_flyweight.h"
#include "caeron/command/image_message_flyweight.h"
#include "caeron/command/operation_succeeded_flyweight.h"
#include "caeron/command/counter_update_flyweight.h"
#include "caeron/command/client_timeout_flyweight.h"
#include "caeron/command/static_counter_flyweight.h"
#include "caeron/command/next_available_session_id_flyweight.h"
#include "caeron/command/publication_error_frame_flyweight.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string_view>

using namespace caeron;
using namespace caeron::command;
using namespace caeron::concurrent;

// ===========================================================================
// PublicationMessageFlyweight
// ===========================================================================

TEST(PublicationMessageFlyweightTest, RoundTrip)
{
    std::byte buf[128]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    PublicationMessageFlyweight fw{buffer, 0};

    fw.set_client_id(42);
    fw.set_correlation_id(100);
    fw.set_stream_id(1001);
    const char* channel = "aeron:udp?endpoint=localhost:40456";
    const i32 channel_len = static_cast<i32>(std::strlen(channel));
    fw.set_channel(channel, channel_len);

    EXPECT_EQ(fw.client_id(), 42);
    EXPECT_EQ(fw.correlation_id(), 100);
    EXPECT_EQ(fw.stream_id(), 1001);
    EXPECT_EQ(fw.channel_length(), channel_len);
    EXPECT_EQ(std::string_view(fw.channel(), fw.channel_length()),
              "aeron:udp?endpoint=localhost:40456");
    EXPECT_EQ(fw.length(), 24 + channel_len);
    EXPECT_EQ(PublicationMessageFlyweight::compute_length(channel_len), 24 + channel_len);
    EXPECT_EQ(PUBLICATION_MSG_MINIMUM_LENGTH, 24);
}

// ===========================================================================
// SubscriptionMessageFlyweight
// ===========================================================================

TEST(SubscriptionMessageFlyweightTest, RoundTrip)
{
    std::byte buf[128]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    SubscriptionMessageFlyweight fw{buffer, 0};

    fw.set_client_id(42);
    fw.set_correlation_id(100);
    fw.set_registration_correlation_id(-1);
    fw.set_stream_id(1001);
    fw.set_channel("aeron:ipc", 9);

    EXPECT_EQ(fw.client_id(), 42);
    EXPECT_EQ(fw.correlation_id(), 100);
    EXPECT_EQ(fw.registration_correlation_id(), -1);
    EXPECT_EQ(fw.stream_id(), 1001);
    EXPECT_EQ(fw.channel_length(), 9);
    EXPECT_EQ(fw.length(), 32 + 9);
    EXPECT_EQ(SUBSCRIPTION_MSG_MINIMUM_LENGTH, 32);
}

// ===========================================================================
// RemovePublicationFlyweight
// ===========================================================================

TEST(RemovePublicationFlyweightTest, RoundTripWithFlags)
{
    std::byte buf[REMOVE_PUBLICATION_LENGTH]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    RemovePublicationFlyweight fw{buffer, 0};

    fw.set_client_id(42);
    fw.set_correlation_id(100);
    fw.set_registration_id(12345);
    fw.set_revoke(true);

    EXPECT_EQ(fw.client_id(), 42);
    EXPECT_EQ(fw.correlation_id(), 100);
    EXPECT_EQ(fw.registration_id(), 12345);
    EXPECT_TRUE(fw.revoke());
    EXPECT_TRUE(fw.revoke(REMOVE_PUBLICATION_LENGTH));
}

TEST(RemovePublicationFlyweightTest, LegacyWithoutFlags)
{
    std::byte buf[REMOVE_PUBLICATION_LEGACY_LENGTH]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    RemovePublicationFlyweight fw{buffer, 0};

    fw.set_client_id(42);
    fw.set_correlation_id(100);
    fw.set_registration_id(12345);

    EXPECT_EQ(fw.client_id(), 42);
    EXPECT_EQ(fw.correlation_id(), 100);
    EXPECT_EQ(fw.registration_id(), 12345);
    // Legacy messages don't have the flags field, so revoke should return false
    EXPECT_FALSE(fw.revoke(REMOVE_PUBLICATION_LEGACY_LENGTH));
    EXPECT_EQ(REMOVE_PUBLICATION_LEGACY_LENGTH, 24);
}

// ===========================================================================
// RemoveSubscriptionFlyweight
// ===========================================================================

TEST(RemoveSubscriptionFlyweightTest, RoundTrip)
{
    std::byte buf[REMOVE_SUBSCRIPTION_LENGTH]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    RemoveSubscriptionFlyweight fw{buffer, 0};

    fw.set_client_id(42);
    fw.set_correlation_id(100);
    fw.set_registration_id(54321);

    EXPECT_EQ(fw.client_id(), 42);
    EXPECT_EQ(fw.correlation_id(), 100);
    EXPECT_EQ(fw.registration_id(), 54321);
    EXPECT_EQ(REMOVE_SUBSCRIPTION_LENGTH, 24);
}

// ===========================================================================
// RemoveMessageFlyweight (also used as RemoveCounterFlyweight)
// ===========================================================================

TEST(RemoveMessageFlyweightTest, RoundTrip)
{
    std::byte buf[REMOVE_MSG_LENGTH]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    RemoveMessageFlyweight fw{buffer, 0};

    fw.set_client_id(42);
    fw.set_correlation_id(100);
    fw.set_registration_id(999);

    EXPECT_EQ(fw.client_id(), 42);
    EXPECT_EQ(fw.correlation_id(), 100);
    EXPECT_EQ(fw.registration_id(), 999);
    EXPECT_EQ(REMOVE_MSG_LENGTH, 24);
}

// ===========================================================================
// CorrelatedMessageFlyweight
// ===========================================================================

TEST(CorrelatedMessageFlyweightTest, RoundTrip)
{
    std::byte buf[CORRELATED_MSG_LENGTH]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    CorrelatedMessageFlyweight fw{buffer, 0};

    fw.set_client_id(42);
    fw.set_correlation_id(-1);

    EXPECT_EQ(fw.client_id(), 42);
    EXPECT_EQ(fw.correlation_id(), -1);
    EXPECT_EQ(CORRELATED_MSG_LENGTH, 16);
}

// ===========================================================================
// DestinationMessageFlyweight
// ===========================================================================

TEST(DestinationMessageFlyweightTest, RoundTrip)
{
    std::byte buf[128]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    DestinationMessageFlyweight fw{buffer, 0};

    fw.set_client_id(42);
    fw.set_correlation_id(100);
    fw.set_registration_correlation_id(111);
    fw.set_channel("aeron:udp?endpoint=localhost:40457", 34);

    EXPECT_EQ(fw.client_id(), 42);
    EXPECT_EQ(fw.correlation_id(), 100);
    EXPECT_EQ(fw.registration_correlation_id(), 111);
    EXPECT_EQ(fw.channel_length(), 34);
    EXPECT_EQ(DESTINATION_MSG_MINIMUM_LENGTH, 28);
}

// ===========================================================================
// DestinationByIdMessageFlyweight
// ===========================================================================

TEST(DestinationByIdMessageFlyweightTest, RoundTrip)
{
    std::byte buf[DESTINATION_BY_ID_MSG_LENGTH]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    DestinationByIdMessageFlyweight fw{buffer, 0};

    fw.set_client_id(42);
    fw.set_correlation_id(100);
    fw.set_resource_registration_id(111);
    fw.set_destination_registration_id(222);

    EXPECT_EQ(fw.client_id(), 42);
    EXPECT_EQ(fw.correlation_id(), 100);
    EXPECT_EQ(fw.resource_registration_id(), 111);
    EXPECT_EQ(fw.destination_registration_id(), 222);
    EXPECT_EQ(DESTINATION_BY_ID_MSG_LENGTH, 32);
}

// ===========================================================================
// CounterMessageFlyweight
// ===========================================================================

TEST(CounterMessageFlyweightTest, RoundTrip)
{
    std::byte buf[128]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    CounterMessageFlyweight fw{buffer, 0};

    fw.set_client_id(42);
    fw.set_correlation_id(100);
    fw.set_counter_type(10);
    fw.set_key_buffer("key", 3);
    fw.set_label("my label", 8);

    EXPECT_EQ(fw.client_id(), 42);
    EXPECT_EQ(fw.correlation_id(), 100);
    EXPECT_EQ(fw.counter_type(), 10);
    EXPECT_EQ(fw.key_buffer_length(), 3);
    EXPECT_EQ(fw.label_length(), 8);
    EXPECT_EQ(fw.length(), 24 + 3 + 4 + 8);
}

TEST(CounterMessageFlyweightTest, ComputeLength)
{
    EXPECT_EQ(CounterMessageFlyweight::compute_length(0, 0), 24 + 0 + 4 + 0);
    EXPECT_EQ(CounterMessageFlyweight::compute_length(4, 10), 24 + 4 + 4 + 10);
}

// ===========================================================================
// TerminateDriverFlyweight
// ===========================================================================

TEST(TerminateDriverFlyweightTest, RoundTrip)
{
    std::byte buf[128]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    TerminateDriverFlyweight fw{buffer, 0};

    fw.set_client_id(42);
    fw.set_correlation_id(-1);
    fw.set_token_buffer("auth-token", 10);

    EXPECT_EQ(fw.client_id(), 42);
    EXPECT_EQ(fw.correlation_id(), -1);
    EXPECT_EQ(fw.token_buffer_length(), 10);
    EXPECT_EQ(fw.length(), 20 + 10);
}

// ===========================================================================
// RejectImageFlyweight
// ===========================================================================

TEST(RejectImageFlyweightTest, RoundTrip)
{
    std::byte buf[128]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    RejectImageFlyweight fw{buffer, 0};

    fw.set_client_id(42);
    fw.set_correlation_id(100);
    fw.set_image_correlation_id(5555);
    fw.set_position(1024);
    fw.set_reason("stale image", 11);

    EXPECT_EQ(fw.client_id(), 42);
    EXPECT_EQ(fw.correlation_id(), 100);
    EXPECT_EQ(fw.image_correlation_id(), 5555);
    EXPECT_EQ(fw.position(), 1024);
    EXPECT_EQ(fw.reason_length(), 11);
    EXPECT_EQ(std::string_view(fw.reason(), fw.reason_length()), "stale image");
    EXPECT_EQ(fw.length(), 36 + 11);
}

TEST(RejectImageFlyweightTest, MinimumSize)
{
    EXPECT_EQ(REJECT_IMAGE_MINIMUM_SIZE, 36);
    EXPECT_EQ(RejectImageFlyweight::compute_length(0), 36);
}

// ===========================================================================
// GetNextAvailableSessionIdMessageFlyweight
// ===========================================================================

TEST(GetNextAvailableSessionIdMessageFlyweightTest, RoundTrip)
{
    std::byte buf[GET_NEXT_SESSION_ID_MSG_LENGTH]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    GetNextAvailableSessionIdMessageFlyweight fw{buffer, 0};

    fw.set_client_id(42);
    fw.set_correlation_id(100);
    fw.set_stream_id(1001);

    EXPECT_EQ(fw.client_id(), 42);
    EXPECT_EQ(fw.correlation_id(), 100);
    EXPECT_EQ(fw.stream_id(), 1001);
    EXPECT_EQ(GET_NEXT_SESSION_ID_MSG_LENGTH, 20);
}

// ===========================================================================
// StaticCounterMessageFlyweight
// ===========================================================================

TEST(StaticCounterMessageFlyweightTest, RoundTrip)
{
    std::byte buf[256]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    StaticCounterMessageFlyweight fw{buffer, 0};

    fw.set_client_id(42);
    fw.set_correlation_id(100);
    fw.set_registration_id(777);
    fw.set_counter_type_id(10);
    fw.set_key_buffer("key", 3);
    fw.set_label("my label", 8);

    EXPECT_EQ(fw.client_id(), 42);
    EXPECT_EQ(fw.correlation_id(), 100);
    EXPECT_EQ(fw.registration_id(), 777);
    EXPECT_EQ(fw.counter_type_id(), 10);
    EXPECT_EQ(fw.key_length(), 3);
    EXPECT_EQ(fw.label_length(), 8);
}

TEST(StaticCounterMessageFlyweightTest, PaddingZeroed)
{
    std::byte buf[256]{};
    // Fill with 0xFF to detect stale padding
    std::memset(buf, 0xFF, sizeof(buf));
    UnsafeBuffer buffer{buf, sizeof(buf)};
    StaticCounterMessageFlyweight fw{buffer, 0};

    fw.set_key_buffer("ab", 2);  // 2 bytes, needs 2 bytes padding to align to 4

    // Padding bytes at offset 32+2 and 32+3 should be zero
    EXPECT_EQ(buf[32 + 2], std::byte{0});
    EXPECT_EQ(buf[32 + 3], std::byte{0});
}

TEST(StaticCounterMessageFlyweightTest, MinimumLength)
{
    EXPECT_EQ(STATIC_COUNTER_MSG_MINIMUM_LENGTH, 36);
}

// ===========================================================================
// ErrorResponseFlyweight
// ===========================================================================

TEST(ErrorResponseFlyweightTest, RoundTrip)
{
    std::byte buf[128]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    ErrorResponseFlyweight fw{buffer, 0};

    fw.set_offending_correlation_id(100);
    fw.set_error_code(1);
    fw.set_error_message("something went wrong", 20);

    EXPECT_EQ(fw.offending_correlation_id(), 100);
    EXPECT_EQ(fw.error_code(), 1);
    EXPECT_EQ(fw.error_message_length(), 20);
    EXPECT_EQ(std::string_view(fw.error_message(), fw.error_message_length()),
              "something went wrong");
    EXPECT_EQ(fw.length(), 16 + 20);
}

// ===========================================================================
// PublicationBuffersReadyFlyweight
// ===========================================================================

TEST(PublicationBuffersReadyFlyweightTest, RoundTrip)
{
    std::byte buf[256]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    PublicationBuffersReadyFlyweight fw{buffer, 0};

    fw.set_correlation_id(200);
    fw.set_registration_id(300);
    fw.set_session_id(42);
    fw.set_stream_id(1001);
    fw.set_pub_limit_counter_id(5);
    fw.set_channel_status_counter_id(6);
    fw.set_log_file_name("/tmp/test.log", 13);

    EXPECT_EQ(fw.correlation_id(), 200);
    EXPECT_EQ(fw.registration_id(), 300);
    EXPECT_EQ(fw.session_id(), 42);
    EXPECT_EQ(fw.stream_id(), 1001);
    EXPECT_EQ(fw.pub_limit_counter_id(), 5);
    EXPECT_EQ(fw.channel_status_counter_id(), 6);
    EXPECT_EQ(fw.log_file_name_length(), 13);
    EXPECT_EQ(fw.length(), 36 + 13);
}

// ===========================================================================
// SubscriptionReadyFlyweight
// ===========================================================================

TEST(SubscriptionReadyFlyweightTest, RoundTrip)
{
    std::byte buf[SUBSCRIPTION_READY_LENGTH]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    SubscriptionReadyFlyweight fw{buffer, 0};

    fw.set_correlation_id(300);
    fw.set_channel_status_counter_id(10);

    EXPECT_EQ(fw.correlation_id(), 300);
    EXPECT_EQ(fw.channel_status_counter_id(), 10);
    EXPECT_EQ(SUBSCRIPTION_READY_LENGTH, 12);
}

// ===========================================================================
// ImageBuffersReadyFlyweight
// ===========================================================================

TEST(ImageBuffersReadyFlyweightTest, RoundTrip)
{
    std::byte buf[256]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    ImageBuffersReadyFlyweight fw{buffer, 0};

    fw.set_correlation_id(1000);
    fw.set_session_id(42);
    fw.set_stream_id(1001);
    fw.set_subscription_registration_id(2000);
    fw.set_subscriber_position_id(55);
    fw.set_log_file_name("/tmp/test.log", 13);
    fw.set_source_identity("localhost:40456", 15);

    EXPECT_EQ(fw.correlation_id(), 1000);
    EXPECT_EQ(fw.session_id(), 42);
    EXPECT_EQ(fw.stream_id(), 1001);
    EXPECT_EQ(fw.subscription_registration_id(), 2000);
    EXPECT_EQ(fw.subscriber_position_id(), 55);
    EXPECT_EQ(fw.log_file_name_length(), 13);
    EXPECT_EQ(fw.source_identity_length(), 15);
    EXPECT_EQ(IMAGE_BUFFERS_READY_MINIMUM_LENGTH, 32);
}

// ===========================================================================
// ImageMessageFlyweight
// ===========================================================================

TEST(ImageMessageFlyweightTest, RoundTrip)
{
    std::byte buf[64]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    ImageMessageFlyweight fw{buffer, 0};

    fw.set_correlation_id(1000);
    fw.set_subscription_registration_id(2000);
    fw.set_stream_id(1001);

    EXPECT_EQ(fw.correlation_id(), 1000);
    EXPECT_EQ(fw.subscription_registration_id(), 2000);
    EXPECT_EQ(fw.stream_id(), 1001);
    EXPECT_EQ(IMAGE_MESSAGE_MINIMUM_LENGTH, 20);
}

// ===========================================================================
// OperationSucceededFlyweight
// ===========================================================================

TEST(OperationSucceededFlyweightTest, RoundTrip)
{
    std::byte buf[OPERATION_SUCCEEDED_LENGTH]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    OperationSucceededFlyweight fw{buffer, 0};

    fw.set_correlation_id(400);

    EXPECT_EQ(fw.correlation_id(), 400);
    EXPECT_EQ(OPERATION_SUCCEEDED_LENGTH, 8);
}

// ===========================================================================
// CounterUpdateFlyweight
// ===========================================================================

TEST(CounterUpdateFlyweightTest, RoundTrip)
{
    std::byte buf[COUNTER_UPDATE_LENGTH]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    CounterUpdateFlyweight fw{buffer, 0};

    fw.set_correlation_id(500);
    fw.set_counter_id(42);

    EXPECT_EQ(fw.correlation_id(), 500);
    EXPECT_EQ(fw.counter_id(), 42);
    EXPECT_EQ(COUNTER_UPDATE_LENGTH, 12);
}

// ===========================================================================
// ClientTimeoutFlyweight
// ===========================================================================

TEST(ClientTimeoutFlyweightTest, RoundTrip)
{
    std::byte buf[CLIENT_TIMEOUT_LENGTH]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    ClientTimeoutFlyweight fw{buffer, 0};

    fw.set_client_id(42);

    EXPECT_EQ(fw.client_id(), 42);
    EXPECT_EQ(CLIENT_TIMEOUT_LENGTH, 8);
}

// ===========================================================================
// StaticCounterFlyweight
// ===========================================================================

TEST(StaticCounterFlyweightTest, RoundTrip)
{
    std::byte buf[STATIC_COUNTER_LENGTH]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    StaticCounterFlyweight fw{buffer, 0};

    fw.set_correlation_id(700);
    fw.set_counter_id(88);

    EXPECT_EQ(fw.correlation_id(), 700);
    EXPECT_EQ(fw.counter_id(), 88);
    EXPECT_EQ(STATIC_COUNTER_LENGTH, 12);
}

// ===========================================================================
// NextAvailableSessionIdFlyweight
// ===========================================================================

TEST(NextAvailableSessionIdFlyweightTest, RoundTrip)
{
    std::byte buf[NEXT_AVAILABLE_SESSION_ID_LENGTH]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    NextAvailableSessionIdFlyweight fw{buffer, 0};

    fw.set_correlation_id(800);
    fw.set_next_session_id(12345);

    EXPECT_EQ(fw.correlation_id(), 800);
    EXPECT_EQ(fw.next_session_id(), 12345);
    EXPECT_EQ(NEXT_AVAILABLE_SESSION_ID_LENGTH, 12);
}

// ===========================================================================
// PublicationErrorFrameFlyweight
// ===========================================================================

TEST(PublicationErrorFrameFlyweightTest, RoundTrip)
{
    std::byte buf[256]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    PublicationErrorFrameFlyweight fw{buffer, 0};

    fw.set_registration_id(100);
    fw.set_destination_registration_id(200);
    fw.set_session_id(42);
    fw.set_stream_id(1001);
    fw.set_receiver_id(500);
    fw.set_group_tag(600);
    fw.set_address_type(1);
    fw.set_udp_port(4045);
    u8 addr[16] = {127, 0, 0, 1};
    fw.set_address(addr, 16);
    fw.set_error_code(2);
    fw.set_error_message("test error", 10);

    EXPECT_EQ(fw.registration_id(), 100);
    EXPECT_EQ(fw.destination_registration_id(), 200);
    EXPECT_EQ(fw.session_id(), 42);
    EXPECT_EQ(fw.stream_id(), 1001);
    EXPECT_EQ(fw.receiver_id(), 500);
    EXPECT_EQ(fw.group_tag(), 600);
    EXPECT_EQ(fw.address_type(), 1);
    EXPECT_EQ(fw.udp_port(), 4045);
    EXPECT_EQ(fw.error_code(), 2);
    EXPECT_EQ(fw.error_message_length(), 10);
    EXPECT_EQ(PUBLICATION_ERROR_FRAME_LENGTH, 68);
}

TEST(PublicationErrorFrameFlyweightTest, SetAddressClamped)
{
    std::byte buf[PUBLICATION_ERROR_FRAME_LENGTH]{};
    std::memset(buf, 0xFF, sizeof(buf));
    UnsafeBuffer buffer{buf, sizeof(buf)};
    PublicationErrorFrameFlyweight fw{buffer, 0};

    // Try to write 20 bytes into a 16-byte field -- should be clamped
    u8 oversized[20];
    std::memset(oversized, 0xAB, sizeof(oversized));
    fw.set_address(oversized, 20);

    // Only first 16 bytes should be written
    const u8* read_addr = fw.address();
    for (int i = 0; i < 16; ++i)
        EXPECT_EQ(read_addr[i], 0xAB);
}

// ===========================================================================
// Security hardening: compute_length overflow guards
// ===========================================================================

TEST(PublicationMessageFlyweightTest, ComputeLengthNegativeReturnsMinusOne)
{
    EXPECT_EQ(PublicationMessageFlyweight::compute_length(-1), -1);
    EXPECT_EQ(PublicationMessageFlyweight::compute_length(-100), -1);
    EXPECT_EQ(PublicationMessageFlyweight::compute_length(0), 24);
    EXPECT_EQ(PublicationMessageFlyweight::compute_length(10), 34);
}

TEST(CounterMessageFlyweightTest, ComputeLengthNegativeReturnsMinusOne)
{
    EXPECT_EQ(CounterMessageFlyweight::compute_length(-1, 0), -1);
    EXPECT_EQ(CounterMessageFlyweight::compute_length(0, -1), -1);
    EXPECT_EQ(CounterMessageFlyweight::compute_length(-1, -1), -1);
    EXPECT_EQ(CounterMessageFlyweight::compute_length(0, 0), 28);
}

TEST(StaticCounterMessageFlyweightTest, ComputeLengthNegativeReturnsMinusOne)
{
    EXPECT_EQ(StaticCounterMessageFlyweight::compute_length(-1, 0), -1);
    EXPECT_EQ(StaticCounterMessageFlyweight::compute_length(0, -1), -1);
    EXPECT_EQ(StaticCounterMessageFlyweight::compute_length(-1, -1), -1);
    EXPECT_EQ(StaticCounterMessageFlyweight::compute_length(0, 0), 36);
    EXPECT_EQ(StaticCounterMessageFlyweight::compute_length(3, 5), 36 + 4 + 5);
}

TEST(ImageBuffersReadyFlyweightTest, ComputeLengthNegativeReturnsMinusOne)
{
    EXPECT_EQ(ImageBuffersReadyFlyweight::compute_length(-1, 0), -1);
    EXPECT_EQ(ImageBuffersReadyFlyweight::compute_length(0, -1), -1);
    EXPECT_EQ(ImageBuffersReadyFlyweight::compute_length(-1, -1), -1);
    EXPECT_EQ(ImageBuffersReadyFlyweight::compute_length(0, 0), 36);
}

TEST(PublicationErrorFrameFlyweightTest, ComputeLengthNegativeReturnsMinusOne)
{
    EXPECT_EQ(PublicationErrorFrameFlyweight::compute_length(-1), -1);
    EXPECT_EQ(PublicationErrorFrameFlyweight::compute_length(-100), -1);
    EXPECT_EQ(PublicationErrorFrameFlyweight::compute_length(0), 68);
}

TEST(ImageMessageFlyweightTest, ComputeLengthNegativeReturnsMinusOne)
{
    EXPECT_EQ(ImageMessageFlyweight::compute_length(-1), -1);
    EXPECT_EQ(ImageMessageFlyweight::compute_length(-100), -1);
    EXPECT_EQ(ImageMessageFlyweight::compute_length(0), 20);
}

TEST(SubscriptionMessageFlyweightTest, ComputeLengthRejectsInvalidLengths)
{
    EXPECT_EQ(SubscriptionMessageFlyweight::compute_length(-1), -1);
    EXPECT_EQ(SubscriptionMessageFlyweight::compute_length(std::numeric_limits<i32>::max()), -1);
}

TEST(DestinationMessageFlyweightTest, ComputeLengthRejectsInvalidLengths)
{
    EXPECT_EQ(DestinationMessageFlyweight::compute_length(-1), -1);
    EXPECT_EQ(DestinationMessageFlyweight::compute_length(std::numeric_limits<i32>::max()), -1);
}

TEST(TerminateDriverFlyweightTest, ComputeLengthRejectsInvalidLengths)
{
    EXPECT_EQ(TerminateDriverFlyweight::compute_length(-1), -1);
    EXPECT_EQ(TerminateDriverFlyweight::compute_length(std::numeric_limits<i32>::max()), -1);
}

TEST(RejectImageFlyweightTest, ComputeLengthRejectsInvalidLengths)
{
    EXPECT_EQ(RejectImageFlyweight::compute_length(-1), -1);
    EXPECT_EQ(RejectImageFlyweight::compute_length(std::numeric_limits<i32>::max()), -1);
}

TEST(PublicationMessageFlyweightTest, ComputeLengthRejectsPositiveOverflow)
{
    EXPECT_EQ(PublicationMessageFlyweight::compute_length(std::numeric_limits<i32>::max()), -1);
}

TEST(CounterMessageFlyweightTest, ComputeLengthRejectsPositiveOverflow)
{
    EXPECT_EQ(CounterMessageFlyweight::compute_length(std::numeric_limits<i32>::max(), 0), -1);
    EXPECT_EQ(CounterMessageFlyweight::compute_length(0, std::numeric_limits<i32>::max()), -1);
}

TEST(StaticCounterMessageFlyweightTest, ComputeLengthRejectsPositiveOverflow)
{
    EXPECT_EQ(StaticCounterMessageFlyweight::compute_length(std::numeric_limits<i32>::max(), 0), -1);
    EXPECT_EQ(StaticCounterMessageFlyweight::compute_length(0, std::numeric_limits<i32>::max()), -1);
}

TEST(ImageBuffersReadyFlyweightTest, ComputeLengthRejectsPositiveOverflow)
{
    EXPECT_EQ(ImageBuffersReadyFlyweight::compute_length(std::numeric_limits<i32>::max(), 0), -1);
    EXPECT_EQ(ImageBuffersReadyFlyweight::compute_length(0, std::numeric_limits<i32>::max()), -1);
}

TEST(PublicationErrorFrameFlyweightTest, ComputeLengthRejectsPositiveOverflow)
{
    EXPECT_EQ(PublicationErrorFrameFlyweight::compute_length(std::numeric_limits<i32>::max()), -1);
}

TEST(ImageMessageFlyweightTest, ComputeLengthRejectsPositiveOverflow)
{
    EXPECT_EQ(ImageMessageFlyweight::compute_length(std::numeric_limits<i32>::max()), -1);
}

// ===========================================================================
// Security hardening: ImageMessageFlyweight invalid setter guards
// ===========================================================================

TEST(ImageMessageFlyweightTest, SetChannelNegativeLengthIsNoop)
{
    std::byte buf[64]{};
    std::memset(buf, 0xAA, sizeof(buf));
    UnsafeBuffer buffer{buf, sizeof(buf)};
    ImageMessageFlyweight fw{buffer, 0};

    std::byte snapshot[64];
    std::memcpy(snapshot, buf, sizeof(buf));

    // Negative length should be a no-op -- buffer should remain untouched
    fw.set_channel("hello", -1);

    // Verify the entire buffer (header + payload) is unchanged
    EXPECT_EQ(std::memcmp(buf, snapshot, sizeof(buf)), 0);
}

TEST(ImageMessageFlyweightTest, SetChannelNullptrIsNoop)
{
    std::byte buf[64]{};
    std::memset(buf, 0xBB, sizeof(buf));
    UnsafeBuffer buffer{buf, sizeof(buf)};
    ImageMessageFlyweight fw{buffer, 0};

    std::byte snapshot[64];
    std::memcpy(snapshot, buf, sizeof(buf));

    // nullptr data should be a no-op
    fw.set_channel(nullptr, 5);

    // Verify the entire buffer (header + payload) is unchanged
    EXPECT_EQ(std::memcmp(buf, snapshot, sizeof(buf)), 0);
}

TEST(ImageMessageFlyweightTest, SetChannelZeroLengthIsNoop)
{
    std::byte buf[64]{};
    std::memset(buf, 0xCC, sizeof(buf));
    UnsafeBuffer buffer{buf, sizeof(buf)};
    ImageMessageFlyweight fw{buffer, 0};

    std::byte snapshot[64];
    std::memcpy(snapshot, buf, sizeof(buf));

    // Zero length should be a no-op
    fw.set_channel("hello", 0);

    // Verify the entire buffer (header + payload) is unchanged
    EXPECT_EQ(std::memcmp(buf, snapshot, sizeof(buf)), 0);
}

// ===========================================================================
// Security hardening: length() negative field guards
// ===========================================================================

TEST(PublicationMessageFlyweightTest, LengthNegativeChannelReturnsMinusOne)
{
    std::byte buf[PUBLICATION_MSG_MINIMUM_LENGTH]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    PublicationMessageFlyweight fw{buffer, 0};

    // Write a negative channel_length directly into the buffer
    buffer.put_i32(PublicationMessageFlyweight::CHANNEL_LENGTH_OFFSET, -5);

    EXPECT_EQ(fw.length(), -1);
}

TEST(CounterMessageFlyweightTest, LengthNegativeKeyReturnsMinusOne)
{
    std::byte buf[64]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    CounterMessageFlyweight fw{buffer, 0};

    buffer.put_i32(CounterMessageFlyweight::KEY_LENGTH_OFFSET, -3);

    EXPECT_EQ(fw.length(), -1);
}

TEST(ImageBuffersReadyFlyweightTest, LengthNegativeLogReturnsMinusOne)
{
    std::byte buf[64]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    ImageBuffersReadyFlyweight fw{buffer, 0};

    buffer.put_i32(ImageBuffersReadyFlyweight::LOG_FILE_NAME_LENGTH_OFFSET, -1);

    EXPECT_EQ(fw.length(), -1);
}

TEST(SubscriptionMessageFlyweightTest, LengthNegativeChannelReturnsMinusOne)
{
    std::byte buf[SUBSCRIPTION_MSG_MINIMUM_LENGTH]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    SubscriptionMessageFlyweight fw{buffer, 0};

    buffer.put_i32(SubscriptionMessageFlyweight::CHANNEL_LENGTH_OFFSET, -5);

    EXPECT_EQ(fw.length(), -1);
}

TEST(DestinationMessageFlyweightTest, LengthNegativeChannelReturnsMinusOne)
{
    std::byte buf[DESTINATION_MSG_MINIMUM_LENGTH]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    DestinationMessageFlyweight fw{buffer, 0};

    buffer.put_i32(DestinationMessageFlyweight::CHANNEL_LENGTH_OFFSET, -5);

    EXPECT_EQ(fw.length(), -1);
}

TEST(TerminateDriverFlyweightTest, LengthNegativeTokenReturnsMinusOne)
{
    std::byte buf[TerminateDriverFlyweight::TOKEN_OFFSET]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    TerminateDriverFlyweight fw{buffer, 0};

    buffer.put_i32(TerminateDriverFlyweight::TOKEN_LENGTH_OFFSET, -5);

    EXPECT_EQ(fw.length(), -1);
}

TEST(RejectImageFlyweightTest, LengthNegativeReasonReturnsMinusOne)
{
    std::byte buf[REJECT_IMAGE_MINIMUM_SIZE]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    RejectImageFlyweight fw{buffer, 0};

    buffer.put_i32(RejectImageFlyweight::REASON_LENGTH_OFFSET, -5);

    EXPECT_EQ(fw.length(), -1);
}

TEST(PublicationBuffersReadyFlyweightTest, LengthNegativeLogReturnsMinusOne)
{
    std::byte buf[PublicationBuffersReadyFlyweight::LOG_FILE_NAME_OFFSET]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    PublicationBuffersReadyFlyweight fw{buffer, 0};

    buffer.put_i32(PublicationBuffersReadyFlyweight::LOG_FILE_NAME_LENGTH_OFFSET, -5);

    EXPECT_EQ(fw.length(), -1);
}

TEST(ErrorResponseFlyweightTest, LengthNegativeErrorMessageReturnsMinusOne)
{
    std::byte buf[ErrorResponseFlyweight::ERROR_MESSAGE_OFFSET]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    ErrorResponseFlyweight fw{buffer, 0};

    buffer.put_i32(ErrorResponseFlyweight::ERROR_MESSAGE_LENGTH_OFFSET, -5);

    EXPECT_EQ(fw.length(), -1);
}

// ===========================================================================
// Security hardening: raw-byte layout assertions
// ===========================================================================

TEST(PublicationMessageFlyweightTest, RawByteLayout)
{
    std::byte buf[PUBLICATION_MSG_MINIMUM_LENGTH]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    PublicationMessageFlyweight fw{buffer, 0};

    fw.set_client_id(0x0102030405060708LL);
    fw.set_correlation_id(0x1112131415161718LL);
    fw.set_stream_id(0x21222324);

    // Verify raw little-endian bytes at each offset
    EXPECT_EQ(buf[0], std::byte{0x08});
    EXPECT_EQ(buf[7], std::byte{0x01});
    EXPECT_EQ(buf[8], std::byte{0x18});
    EXPECT_EQ(buf[15], std::byte{0x11});
    EXPECT_EQ(buf[16], std::byte{0x24});
    EXPECT_EQ(buf[19], std::byte{0x21});
    EXPECT_EQ(buf[20], std::byte{0x00}); // channel_length = 0
}

TEST(StaticCounterMessageFlyweightTest, RawByteLayout)
{
    std::byte buf[STATIC_COUNTER_MSG_MINIMUM_LENGTH]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    StaticCounterMessageFlyweight fw{buffer, 0};

    fw.set_client_id(0x0102030405060708LL);
    fw.set_correlation_id(0x1112131415161718LL);
    fw.set_registration_id(0x2122232425262728LL);
    fw.set_counter_type_id(0x31323334);
    fw.set_key_length(0x41424344);

    EXPECT_EQ(buf[0], std::byte{0x08});   // client_id low byte
    EXPECT_EQ(buf[8], std::byte{0x18});   // correlation_id low byte
    EXPECT_EQ(buf[16], std::byte{0x28});  // registration_id low byte
    EXPECT_EQ(buf[24], std::byte{0x34});  // counter_type_id low byte
    EXPECT_EQ(buf[28], std::byte{0x44});  // key_length low byte
}

TEST(ImageMessageFlyweightTest, RawByteLayout)
{
    std::byte buf[IMAGE_MESSAGE_MINIMUM_LENGTH]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    ImageMessageFlyweight fw{buffer, 0};

    fw.set_correlation_id(0x0102030405060708LL);
    fw.set_subscription_registration_id(0x1112131415161718LL);
    fw.set_stream_id(0x21222324);

    EXPECT_EQ(buf[0], std::byte{0x08});   // correlation_id low byte
    EXPECT_EQ(buf[8], std::byte{0x18});   // subscription_registration_id low byte
    EXPECT_EQ(buf[16], std::byte{0x24});  // stream_id low byte
}

// ===========================================================================
// Security hardening: derived-offset validation (HIGH-1 regression tests)
// ===========================================================================

TEST(CounterMessageFlyweightTest, LabelLengthReturnsMinusOneWhenKeyNegative)
{
    std::byte buf[64]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    CounterMessageFlyweight fw{buffer, 0};

    buffer.put_i32(CounterMessageFlyweight::KEY_LENGTH_OFFSET, -3);

    // label_length() must not read at a negative-derived offset
    EXPECT_EQ(fw.label_length(), -1);
}

TEST(CounterMessageFlyweightTest, LabelBufferOffsetReturnsMinusOneWhenKeyNegative)
{
    std::byte buf[64]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    CounterMessageFlyweight fw{buffer, 0};

    buffer.put_i32(CounterMessageFlyweight::KEY_LENGTH_OFFSET, -3);

    EXPECT_EQ(fw.label_buffer_offset(), -1);
}

TEST(CounterMessageFlyweightTest, LabelReturnsNullptrWhenKeyNegative)
{
    std::byte buf[64]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    CounterMessageFlyweight fw{buffer, 0};

    buffer.put_i32(CounterMessageFlyweight::KEY_LENGTH_OFFSET, -3);

    EXPECT_EQ(fw.label(), nullptr);
}

TEST(StaticCounterMessageFlyweightTest, LabelLengthOffsetReturnsMinusOneWhenKeyNegative)
{
    std::byte buf[64]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    StaticCounterMessageFlyweight fw{buffer, 0};

    buffer.put_i32(StaticCounterMessageFlyweight::KEY_LENGTH_OFFSET, -3);

    EXPECT_EQ(fw.label_length_offset(), -1);
}

TEST(StaticCounterMessageFlyweightTest, LabelBufferOffsetReturnsMinusOneWhenKeyNegative)
{
    std::byte buf[64]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    StaticCounterMessageFlyweight fw{buffer, 0};

    buffer.put_i32(StaticCounterMessageFlyweight::KEY_LENGTH_OFFSET, -3);

    EXPECT_EQ(fw.label_buffer_offset(), -1);
}

TEST(StaticCounterMessageFlyweightTest, LabelReturnsNullptrWhenKeyNegative)
{
    std::byte buf[64]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    StaticCounterMessageFlyweight fw{buffer, 0};

    buffer.put_i32(StaticCounterMessageFlyweight::KEY_LENGTH_OFFSET, -3);

    EXPECT_EQ(fw.label(), nullptr);
}

TEST(StaticCounterMessageFlyweightTest, LengthReturnsMinusOneWhenKeyNegative)
{
    std::byte buf[64]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    StaticCounterMessageFlyweight fw{buffer, 0};

    buffer.put_i32(StaticCounterMessageFlyweight::KEY_LENGTH_OFFSET, -3);

    EXPECT_EQ(fw.length(), -1);
}

TEST(ImageBuffersReadyFlyweightTest, SourceIdentityLengthReturnsMinusOneWhenLogNegative)
{
    std::byte buf[64]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    ImageBuffersReadyFlyweight fw{buffer, 0};

    buffer.put_i32(ImageBuffersReadyFlyweight::LOG_FILE_NAME_LENGTH_OFFSET, -1);

    EXPECT_EQ(fw.source_identity_length(), -1);
}

TEST(ImageBuffersReadyFlyweightTest, SourceIdentityReturnsNullptrWhenLogNegative)
{
    std::byte buf[64]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    ImageBuffersReadyFlyweight fw{buffer, 0};

    buffer.put_i32(ImageBuffersReadyFlyweight::LOG_FILE_NAME_LENGTH_OFFSET, -1);

    EXPECT_EQ(fw.source_identity(), nullptr);
}

// ===========================================================================
// Security hardening: extreme positive overflow in compute_length()
// ===========================================================================

TEST(SubscriptionMessageFlyweightTest, ComputeLengthRejectsExtremePositive)
{
    // Near INT_MAX but still positive -- should overflow
    EXPECT_EQ(SubscriptionMessageFlyweight::compute_length(std::numeric_limits<i32>::max() - 10), -1);
    EXPECT_EQ(SubscriptionMessageFlyweight::compute_length(std::numeric_limits<i32>::max()), -1);
    EXPECT_EQ(SubscriptionMessageFlyweight::compute_length(std::numeric_limits<i32>::max() - SUBSCRIPTION_MSG_MINIMUM_LENGTH + 1), -1);
}

TEST(DestinationMessageFlyweightTest, ComputeLengthRejectsExtremePositive)
{
    EXPECT_EQ(DestinationMessageFlyweight::compute_length(std::numeric_limits<i32>::max() - 10), -1);
    EXPECT_EQ(DestinationMessageFlyweight::compute_length(std::numeric_limits<i32>::max()), -1);
}

TEST(TerminateDriverFlyweightTest, ComputeLengthRejectsExtremePositive)
{
    EXPECT_EQ(TerminateDriverFlyweight::compute_length(std::numeric_limits<i32>::max() - 10), -1);
    EXPECT_EQ(TerminateDriverFlyweight::compute_length(std::numeric_limits<i32>::max()), -1);
}

TEST(RejectImageFlyweightTest, ComputeLengthRejectsExtremePositive)
{
    EXPECT_EQ(RejectImageFlyweight::compute_length(std::numeric_limits<i32>::max() - 10), -1);
    EXPECT_EQ(RejectImageFlyweight::compute_length(std::numeric_limits<i32>::max()), -1);
}

// ===========================================================================
// Security hardening: negative/overflow length() for all flyweight classes
// ===========================================================================

TEST(SubscriptionMessageFlyweightTest, SetNegativeChannelIsNoop)
{
    std::byte buf[64]{};
    std::memset(buf, 0xAA, sizeof(buf));
    UnsafeBuffer buffer{buf, sizeof(buf)};
    SubscriptionMessageFlyweight fw{buffer, 0};

    std::byte snapshot[64];
    std::memcpy(snapshot, buf, sizeof(buf));

    fw.set_channel("hello", -1);

    // Verify the entire buffer (header + payload) is unchanged
    EXPECT_EQ(std::memcmp(buf, snapshot, sizeof(buf)), 0);
}

TEST(DestinationMessageFlyweightTest, SetNegativeChannelIsNoop)
{
    std::byte buf[64]{};
    std::memset(buf, 0xAA, sizeof(buf));
    UnsafeBuffer buffer{buf, sizeof(buf)};
    DestinationMessageFlyweight fw{buffer, 0};

    std::byte snapshot[64];
    std::memcpy(snapshot, buf, sizeof(buf));

    fw.set_channel("hello", -1);

    // Verify the entire buffer (header + payload) is unchanged
    EXPECT_EQ(std::memcmp(buf, snapshot, sizeof(buf)), 0);
}

TEST(TerminateDriverFlyweightTest, SetNegativeTokenIsNoop)
{
    std::byte buf[64]{};
    std::memset(buf, 0xAA, sizeof(buf));
    UnsafeBuffer buffer{buf, sizeof(buf)};
    TerminateDriverFlyweight fw{buffer, 0};

    std::byte snapshot[64];
    std::memcpy(snapshot, buf, sizeof(buf));

    fw.set_token_buffer("hello", -1);

    // Verify the entire buffer (header + payload) is unchanged
    EXPECT_EQ(std::memcmp(buf, snapshot, sizeof(buf)), 0);
}

TEST(RejectImageFlyweightTest, SetNegativeReasonIsNoop)
{
    std::byte buf[64]{};
    std::memset(buf, 0xAA, sizeof(buf));
    UnsafeBuffer buffer{buf, sizeof(buf)};
    RejectImageFlyweight fw{buffer, 0};

    std::byte snapshot[64];
    std::memcpy(snapshot, buf, sizeof(buf));

    fw.set_reason("hello", -1);

    // Verify the entire buffer (header + payload) is unchanged
    EXPECT_EQ(std::memcmp(buf, snapshot, sizeof(buf)), 0);
}

TEST(PublicationBuffersReadyFlyweightTest, SetNegativeLogFileNameIsNoop)
{
    std::byte buf[64]{};
    std::memset(buf, 0xAA, sizeof(buf));
    UnsafeBuffer buffer{buf, sizeof(buf)};
    PublicationBuffersReadyFlyweight fw{buffer, 0};

    std::byte snapshot[64];
    std::memcpy(snapshot, buf, sizeof(buf));

    fw.set_log_file_name("hello", -1);

    // Verify the entire buffer (header + payload) is unchanged
    EXPECT_EQ(std::memcmp(buf, snapshot, sizeof(buf)), 0);
}

TEST(ErrorResponseFlyweightTest, SetNegativeErrorMessageIsNoop)
{
    std::byte buf[64]{};
    std::memset(buf, 0xAA, sizeof(buf));
    UnsafeBuffer buffer{buf, sizeof(buf)};
    ErrorResponseFlyweight fw{buffer, 0};

    std::byte snapshot[64];
    std::memcpy(snapshot, buf, sizeof(buf));

    fw.set_error_message("hello", -1);

    // Verify the entire buffer (header + payload) is unchanged
    EXPECT_EQ(std::memcmp(buf, snapshot, sizeof(buf)), 0);
}

TEST(PublicationErrorFrameFlyweightTest, SetNegativeErrorMessageIsNoop)
{
    std::byte buf[PUBLICATION_ERROR_FRAME_LENGTH]{};
    std::memset(buf, 0xAA, sizeof(buf));
    UnsafeBuffer buffer{buf, sizeof(buf)};
    PublicationErrorFrameFlyweight fw{buffer, 0};

    std::byte snapshot[PUBLICATION_ERROR_FRAME_LENGTH];
    std::memcpy(snapshot, buf, sizeof(buf));

    fw.set_error_message("hello", -1);

    // Verify the entire buffer (header + payload) is unchanged
    EXPECT_EQ(std::memcmp(buf, snapshot, sizeof(buf)), 0);
}

TEST(ImageBuffersReadyFlyweightTest, SetNegativeLogFileNameIsNoop)
{
    std::byte buf[64]{};
    std::memset(buf, 0xAA, sizeof(buf));
    UnsafeBuffer buffer{buf, sizeof(buf)};
    ImageBuffersReadyFlyweight fw{buffer, 0};

    std::byte snapshot[64];
    std::memcpy(snapshot, buf, sizeof(buf));

    fw.set_log_file_name("hello", -1);

    // Verify the entire buffer (header + payload) is unchanged
    EXPECT_EQ(std::memcmp(buf, snapshot, sizeof(buf)), 0);
}

TEST(ImageBuffersReadyFlyweightTest, SetNegativeSourceIdentityIsNoop)
{
    std::byte buf[64]{};
    std::memset(buf, 0xAA, sizeof(buf));
    UnsafeBuffer buffer{buf, sizeof(buf)};
    ImageBuffersReadyFlyweight fw{buffer, 0};

    std::byte snapshot[64];
    std::memcpy(snapshot, buf, sizeof(buf));

    fw.set_source_identity("hello", -1);

    // Verify the entire buffer (header + payload) is unchanged
    EXPECT_EQ(std::memcmp(buf, snapshot, sizeof(buf)), 0);
}

TEST(CounterMessageFlyweightTest, SetNegativeKeyBufferIsNoop)
{
    std::byte buf[64]{};
    std::memset(buf, 0xAA, sizeof(buf));
    UnsafeBuffer buffer{buf, sizeof(buf)};
    CounterMessageFlyweight fw{buffer, 0};

    std::byte snapshot[64];
    std::memcpy(snapshot, buf, sizeof(buf));

    fw.set_key_buffer("hello", -1);

    // Verify the entire buffer (header + payload) is unchanged
    EXPECT_EQ(std::memcmp(buf, snapshot, sizeof(buf)), 0);
}

TEST(CounterMessageFlyweightTest, SetNegativeLabelIsNoop)
{
    std::byte buf[64]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    CounterMessageFlyweight fw{buffer, 0};

    fw.set_key_buffer("key", 3);
    // Snapshot the buffer state before the no-op call
    std::byte snapshot[64];
    std::memcpy(snapshot, buf, sizeof(buf));

    fw.set_label("hello", -1);

    // Buffer should be completely unchanged after a no-op
    EXPECT_EQ(std::memcmp(buf, snapshot, sizeof(buf)), 0);
}

TEST(StaticCounterMessageFlyweightTest, SetNegativeKeyBufferIsNoop)
{
    std::byte buf[64]{};
    std::memset(buf, 0xAA, sizeof(buf));
    UnsafeBuffer buffer{buf, sizeof(buf)};
    StaticCounterMessageFlyweight fw{buffer, 0};

    std::byte snapshot[64];
    std::memcpy(snapshot, buf, sizeof(buf));

    fw.set_key_buffer("hello", -1);

    // Verify the entire buffer (header + payload) is unchanged
    EXPECT_EQ(std::memcmp(buf, snapshot, sizeof(buf)), 0);
}

TEST(StaticCounterMessageFlyweightTest, SetNegativeLabelIsNoop)
{
    std::byte buf[64]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    StaticCounterMessageFlyweight fw{buffer, 0};

    fw.set_key_buffer("key", 3);
    // Snapshot the buffer state before the no-op call
    std::byte snapshot[64];
    std::memcpy(snapshot, buf, sizeof(buf));

    fw.set_label("hello", -1);

    // Buffer should be completely unchanged after a no-op
    EXPECT_EQ(std::memcmp(buf, snapshot, sizeof(buf)), 0);
}

TEST(PublicationMessageFlyweightTest, SetNegativeChannelIsNoop)
{
    std::byte buf[64]{};
    std::memset(buf, 0xAA, sizeof(buf));
    UnsafeBuffer buffer{buf, sizeof(buf)};
    PublicationMessageFlyweight fw{buffer, 0};

    std::byte snapshot[64];
    std::memcpy(snapshot, buf, sizeof(buf));

    fw.set_channel("hello", -1);

    // Verify the entire buffer (header + payload) is unchanged
    EXPECT_EQ(std::memcmp(buf, snapshot, sizeof(buf)), 0);
}

// ===========================================================================
// Security hardening: align() overflow (bit_util)
// ===========================================================================

TEST(BitUtil, AlignOverflowThrows)
{
    // i32 max value aligned to 4 should overflow
    EXPECT_THROW((void)align(std::numeric_limits<i32>::max(), 4), std::overflow_error);
    EXPECT_THROW((void)align(std::numeric_limits<i32>::max() - 1, 4), std::overflow_error);
    EXPECT_THROW((void)align(std::numeric_limits<i32>::max() - 2, 4), std::overflow_error);
}

TEST(BitUtil, AlignNearMaxDoesNotThrow)
{
    // Values just under the overflow boundary should work
    EXPECT_EQ(align(std::numeric_limits<i32>::max() - 4, 4), std::numeric_limits<i32>::max() - 3);
}

// ===========================================================================
// Capacity-bounds validation: flyweight accessors and setters with positive
// embedded lengths that exceed the backing buffer capacity.
// These tests observe that the capacity checks return (-1, nullptr)
// or silently skip writes, even in release builds. No crash/no mutation
// observed; not a proof of UB-freedom.
// ===========================================================================

TEST(CounterMessageFlyweightTest, OversizedKeyLengthReturnsMinusOne)
{
    std::byte buf[64]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    CounterMessageFlyweight fw{buffer, 0};

    buffer.put_i32(CounterMessageFlyweight::KEY_LENGTH_OFFSET, 128);

    // label_length() must not read out of bounds -- returns -1
    EXPECT_EQ(fw.label_length(), -1);
    EXPECT_EQ(fw.label(), nullptr);
    EXPECT_EQ(fw.label_buffer_offset(), -1);
    EXPECT_EQ(fw.length(), -1);
}

TEST(StaticCounterMessageFlyweightTest, OversizedKeyLengthReturnsMinusOne)
{
    std::byte buf[64]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    StaticCounterMessageFlyweight fw{buffer, 0};

    buffer.put_i32(StaticCounterMessageFlyweight::KEY_LENGTH_OFFSET, 128);

    EXPECT_EQ(fw.label_length(), -1);
    EXPECT_EQ(fw.label(), nullptr);
    EXPECT_EQ(fw.label_buffer_offset(), -1);
    EXPECT_EQ(fw.label_length_offset(), -1);
    EXPECT_EQ(fw.length(), -1);
}

TEST(ImageBuffersReadyFlyweightTest, OversizedLogLengthReturnsMinusOne)
{
    std::byte buf[64]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    ImageBuffersReadyFlyweight fw{buffer, 0};

    buffer.put_i32(ImageBuffersReadyFlyweight::LOG_FILE_NAME_LENGTH_OFFSET, 128);

    EXPECT_EQ(fw.source_identity_length(), -1);
    EXPECT_EQ(fw.source_identity(), nullptr);
    EXPECT_EQ(fw.length(), -1);
}

TEST(PublicationMessageFlyweightTest, OversizedSetChannelIsNoop)
{
    std::byte buf[PUBLICATION_MSG_MINIMUM_LENGTH]{};
    std::memset(buf, 0xAA, sizeof(buf));
    UnsafeBuffer buffer{buf, sizeof(buf)};
    PublicationMessageFlyweight fw{buffer, 0};

    std::byte snapshot[PUBLICATION_MSG_MINIMUM_LENGTH];
    std::memcpy(snapshot, buf, sizeof(buf));

    const char payload[] = "payload";
    fw.set_channel(payload, 128);

    // Buffer must be completely unchanged -- capacity check prevents overrun
    EXPECT_EQ(std::memcmp(buf, snapshot, sizeof(buf)), 0);
}

TEST(TerminateDriverFlyweightTest, OversizedSetTokenIsNoop)
{
    std::byte buf[TerminateDriverFlyweight::TOKEN_OFFSET]{};
    std::memset(buf, 0xAA, sizeof(buf));
    UnsafeBuffer buffer{buf, sizeof(buf)};
    TerminateDriverFlyweight fw{buffer, 0};

    std::byte snapshot[TerminateDriverFlyweight::TOKEN_OFFSET];
    std::memcpy(snapshot, buf, sizeof(buf));

    const char payload[] = "payload";
    fw.set_token_buffer(payload, 128);

    // Buffer must be completely unchanged -- capacity check prevents overrun
    EXPECT_EQ(std::memcmp(buf, snapshot, sizeof(buf)), 0);
}

// ===========================================================================
// Remaining review findings: guard arithmetic and invalid offsets.
// These tests observe the desired no-crash/no-mutation behavior. EXPECT_EXIT
// keeps assertion failures isolated in a child process so the rest of the
// suite can continue.
// ===========================================================================

namespace {

void overflowing_offset_set_channel_child()
{
    std::byte buf[PUBLICATION_MSG_MINIMUM_LENGTH]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    PublicationMessageFlyweight fw{buffer, std::numeric_limits<i32>::max() - 10};
    fw.set_channel("x", 1);
    std::_Exit(0);
}

void overflowing_set_label_length_child()
{
    std::byte buf[64]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    CounterMessageFlyweight fw{buffer, 0};
    fw.set_key_buffer(nullptr, 0);
    fw.set_label("x", std::numeric_limits<i32>::max());
    std::_Exit(0);
}

void set_address_too_small_buffer_child()
{
    std::byte buf[PublicationErrorFrameFlyweight::ADDRESS_OFFSET]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    PublicationErrorFrameFlyweight fw{buffer, 0};
    const u8 address[PublicationErrorFrameFlyweight::ADDRESS_LENGTH]{};
    fw.set_address(address, PublicationErrorFrameFlyweight::ADDRESS_LENGTH);
    std::_Exit(0);
}

} // namespace

TEST(PublicationMessageFlyweightTest, ChannelReturnsNullptrWhenOffsetOutOfBounds)
{
    std::byte buf[PUBLICATION_MSG_MINIMUM_LENGTH]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    PublicationMessageFlyweight fw{buffer, 128};

    EXPECT_EQ(fw.channel(), nullptr);
}

TEST(PublicationErrorFrameFlyweightTest, AddressReturnsNullptrWhenOffsetOutOfBounds)
{
    std::byte buf[PUBLICATION_ERROR_FRAME_LENGTH]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    PublicationErrorFrameFlyweight fw{buffer, 128};

    EXPECT_EQ(fw.address(), nullptr);
}

TEST(PublicationMessageFlyweightTest, OverflowingOffsetSetChannelDoesNotAbort)
{
    EXPECT_EXIT(overflowing_offset_set_channel_child(), ::testing::ExitedWithCode(0), "");
}

TEST(CounterMessageFlyweightTest, OverflowingSetLabelLengthDoesNotAbort)
{
    EXPECT_EXIT(overflowing_set_label_length_child(), ::testing::ExitedWithCode(0), "");
}

TEST(PublicationErrorFrameFlyweightTest, SetAddressTooSmallBufferDoesNotAbort)
{
    EXPECT_EXIT(set_address_too_small_buffer_child(), ::testing::ExitedWithCode(0), "");
}

// ===========================================================================
// No-crash/no-mutation observations: overflow inside guard for all variable-length flyweights
// ===========================================================================

TEST(TerminateDriverFlyweightTest, OverflowingOffsetSetTokenDoesNotAbort)
{
    std::byte buf[TerminateDriverFlyweight::TOKEN_OFFSET]{};
    std::memset(buf, 0xAA, sizeof(buf));
    UnsafeBuffer buffer{buf, sizeof(buf)};
    TerminateDriverFlyweight fw{buffer, std::numeric_limits<i32>::max() - 10};

    std::byte snapshot[TerminateDriverFlyweight::TOKEN_OFFSET];
    std::memcpy(snapshot, buf, sizeof(buf));

    // set_token_buffer should no-op when offset overflows (no crash/no mutation observed)
    fw.set_token_buffer("x", 1);

    // Buffer should be completely unchanged
    EXPECT_EQ(std::memcmp(buf, snapshot, sizeof(buf)), 0);
}

TEST(SubscriptionMessageFlyweightTest, OverflowingOffsetSetChannelDoesNotAbort)
{
    std::byte buf[SUBSCRIPTION_MSG_MINIMUM_LENGTH]{};
    std::memset(buf, 0xAA, sizeof(buf));
    UnsafeBuffer buffer{buf, sizeof(buf)};
    SubscriptionMessageFlyweight fw{buffer, std::numeric_limits<i32>::max() - 10};

    std::byte snapshot[SUBSCRIPTION_MSG_MINIMUM_LENGTH];
    std::memcpy(snapshot, buf, sizeof(buf));

    fw.set_channel("x", 1);

    // Buffer should be completely unchanged
    EXPECT_EQ(std::memcmp(buf, snapshot, sizeof(buf)), 0);
}

TEST(DestinationMessageFlyweightTest, OverflowingOffsetSetChannelDoesNotAbort)
{
    std::byte buf[DESTINATION_MSG_MINIMUM_LENGTH]{};
    std::memset(buf, 0xAA, sizeof(buf));
    UnsafeBuffer buffer{buf, sizeof(buf)};
    DestinationMessageFlyweight fw{buffer, std::numeric_limits<i32>::max() - 10};

    std::byte snapshot[DESTINATION_MSG_MINIMUM_LENGTH];
    std::memcpy(snapshot, buf, sizeof(buf));

    fw.set_channel("x", 1);

    // Buffer should be completely unchanged
    EXPECT_EQ(std::memcmp(buf, snapshot, sizeof(buf)), 0);
}

TEST(RejectImageFlyweightTest, OverflowingOffsetSetReasonDoesNotAbort)
{
    std::byte buf[REJECT_IMAGE_MINIMUM_SIZE]{};
    std::memset(buf, 0xAA, sizeof(buf));
    UnsafeBuffer buffer{buf, sizeof(buf)};
    RejectImageFlyweight fw{buffer, std::numeric_limits<i32>::max() - 10};

    std::byte snapshot[REJECT_IMAGE_MINIMUM_SIZE];
    std::memcpy(snapshot, buf, sizeof(buf));

    fw.set_reason("x", 1);

    // Buffer should be completely unchanged
    EXPECT_EQ(std::memcmp(buf, snapshot, sizeof(buf)), 0);
}

TEST(ErrorResponseFlyweightTest, OverflowingOffsetSetErrorMessageDoesNotAbort)
{
    std::byte buf[ErrorResponseFlyweight::ERROR_MESSAGE_OFFSET]{};
    std::memset(buf, 0xAA, sizeof(buf));
    UnsafeBuffer buffer{buf, sizeof(buf)};
    ErrorResponseFlyweight fw{buffer, std::numeric_limits<i32>::max() - 10};

    std::byte snapshot[ErrorResponseFlyweight::ERROR_MESSAGE_OFFSET];
    std::memcpy(snapshot, buf, sizeof(buf));

    fw.set_error_message("x", 1);

    // Buffer should be completely unchanged
    EXPECT_EQ(std::memcmp(buf, snapshot, sizeof(buf)), 0);
}

TEST(PublicationBuffersReadyFlyweightTest, OverflowingOffsetSetLogFileNameDoesNotAbort)
{
    std::byte buf[PublicationBuffersReadyFlyweight::LOG_FILE_NAME_OFFSET]{};
    std::memset(buf, 0xAA, sizeof(buf));
    UnsafeBuffer buffer{buf, sizeof(buf)};
    PublicationBuffersReadyFlyweight fw{buffer, std::numeric_limits<i32>::max() - 10};

    std::byte snapshot[PublicationBuffersReadyFlyweight::LOG_FILE_NAME_OFFSET];
    std::memcpy(snapshot, buf, sizeof(buf));

    fw.set_log_file_name("x", 1);

    // Buffer should be completely unchanged
    EXPECT_EQ(std::memcmp(buf, snapshot, sizeof(buf)), 0);
}

TEST(PublicationErrorFrameFlyweightTest, OverflowingOffsetSetErrorMessageDoesNotAbort)
{
    std::byte buf[PUBLICATION_ERROR_FRAME_LENGTH]{};
    std::memset(buf, 0xAA, sizeof(buf));
    UnsafeBuffer buffer{buf, sizeof(buf)};
    PublicationErrorFrameFlyweight fw{buffer, std::numeric_limits<i32>::max() - 10};

    std::byte snapshot[PUBLICATION_ERROR_FRAME_LENGTH];
    std::memcpy(snapshot, buf, sizeof(buf));

    fw.set_error_message("x", 1);

    // Buffer should be completely unchanged
    EXPECT_EQ(std::memcmp(buf, snapshot, sizeof(buf)), 0);
}

TEST(ImageBuffersReadyFlyweightTest, OverflowingOffsetSetLogFileNameDoesNotAbort)
{
    std::byte buf[IMAGE_BUFFERS_READY_MINIMUM_LENGTH]{};
    std::memset(buf, 0xAA, sizeof(buf));
    UnsafeBuffer buffer{buf, sizeof(buf)};
    ImageBuffersReadyFlyweight fw{buffer, std::numeric_limits<i32>::max() - 10};

    std::byte snapshot[IMAGE_BUFFERS_READY_MINIMUM_LENGTH];
    std::memcpy(snapshot, buf, sizeof(buf));

    fw.set_log_file_name("x", 1);

    // Buffer should be completely unchanged
    EXPECT_EQ(std::memcmp(buf, snapshot, sizeof(buf)), 0);
}

TEST(ImageMessageFlyweightTest, OverflowingOffsetSetChannelDoesNotAbort)
{
    std::byte buf[IMAGE_MESSAGE_MINIMUM_LENGTH]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    ImageMessageFlyweight fw{buffer, std::numeric_limits<i32>::max() - 10};

    fw.set_channel("x", 1);

    // ImageMessageFlyweight has no channel_length field; just verify no crash
}

TEST(StaticCounterMessageFlyweightTest, OverflowingOffsetSetKeyBufferDoesNotAbort)
{
    std::byte buf[STATIC_COUNTER_MSG_MINIMUM_LENGTH]{};
    std::memset(buf, 0xAA, sizeof(buf));
    UnsafeBuffer buffer{buf, sizeof(buf)};
    StaticCounterMessageFlyweight fw{buffer, std::numeric_limits<i32>::max() - 10};

    std::byte snapshot[STATIC_COUNTER_MSG_MINIMUM_LENGTH];
    std::memcpy(snapshot, buf, sizeof(buf));

    fw.set_key_buffer("x", 1);

    // Buffer should be completely unchanged
    EXPECT_EQ(std::memcmp(buf, snapshot, sizeof(buf)), 0);
}

// ===========================================================================
// Security hardening: pointer getters return nullptr for invalid non-zero offsets
// ===========================================================================

TEST(TerminateDriverFlyweightTest, TokenBufferReturnsNullptrWhenOffsetOutOfBounds)
{
    std::byte buf[TerminateDriverFlyweight::TOKEN_OFFSET]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    TerminateDriverFlyweight fw{buffer, 128};

    EXPECT_EQ(fw.token_buffer(), nullptr);
}

TEST(SubscriptionMessageFlyweightTest, ChannelReturnsNullptrWhenOffsetOutOfBounds)
{
    std::byte buf[SUBSCRIPTION_MSG_MINIMUM_LENGTH]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    SubscriptionMessageFlyweight fw{buffer, 128};

    EXPECT_EQ(fw.channel(), nullptr);
}

TEST(DestinationMessageFlyweightTest, ChannelReturnsNullptrWhenOffsetOutOfBounds)
{
    std::byte buf[DESTINATION_MSG_MINIMUM_LENGTH]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    DestinationMessageFlyweight fw{buffer, 128};

    EXPECT_EQ(fw.channel(), nullptr);
}

TEST(ImageMessageFlyweightTest, ChannelReturnsNullptrWhenOffsetOutOfBounds)
{
    std::byte buf[IMAGE_MESSAGE_MINIMUM_LENGTH]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    ImageMessageFlyweight fw{buffer, 128};

    EXPECT_EQ(fw.channel(), nullptr);
}

TEST(RejectImageFlyweightTest, ReasonReturnsNullptrWhenOffsetOutOfBounds)
{
    std::byte buf[REJECT_IMAGE_MINIMUM_SIZE]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    RejectImageFlyweight fw{buffer, 128};

    EXPECT_EQ(fw.reason(), nullptr);
}

TEST(ErrorResponseFlyweightTest, ErrorMessageReturnsNullptrWhenOffsetOutOfBounds)
{
    std::byte buf[ErrorResponseFlyweight::ERROR_MESSAGE_OFFSET]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    ErrorResponseFlyweight fw{buffer, 128};

    EXPECT_EQ(fw.error_message(), nullptr);
}

TEST(PublicationBuffersReadyFlyweightTest, LogFileNameReturnsNullptrWhenOffsetOutOfBounds)
{
    std::byte buf[PublicationBuffersReadyFlyweight::LOG_FILE_NAME_OFFSET]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    PublicationBuffersReadyFlyweight fw{buffer, 128};

    EXPECT_EQ(fw.log_file_name(), nullptr);
}

TEST(ImageBuffersReadyFlyweightTest, LogFileNameReturnsNullptrWhenOffsetOutOfBounds)
{
    std::byte buf[IMAGE_BUFFERS_READY_MINIMUM_LENGTH]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    ImageBuffersReadyFlyweight fw{buffer, 128};

    EXPECT_EQ(fw.log_file_name(), nullptr);
}

TEST(CounterMessageFlyweightTest, KeyBufferReturnsNullptrWhenOffsetOutOfBounds)
{
    std::byte buf[64]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    CounterMessageFlyweight fw{buffer, 128};

    EXPECT_EQ(fw.key_buffer(), nullptr);
}

TEST(StaticCounterMessageFlyweightTest, KeyBufferReturnsNullptrWhenOffsetOutOfBounds)
{
    std::byte buf[64]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    StaticCounterMessageFlyweight fw{buffer, 128};

    EXPECT_EQ(fw.key_buffer(), nullptr);
}

TEST(PublicationErrorFrameFlyweightTest, ErrorMessageReturnsNullptrWhenOffsetOutOfBounds)
{
    std::byte buf[PUBLICATION_ERROR_FRAME_LENGTH]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    PublicationErrorFrameFlyweight fw{buffer, 128};

    EXPECT_EQ(fw.error_message(), nullptr);
}

// ===========================================================================
// Security hardening: set_address() capacity behavior
// ===========================================================================

TEST(PublicationErrorFrameFlyweightTest, SetAddressOnTooSmallBufferIsNoop)
{
    std::byte buf[PUBLICATION_ERROR_FRAME_LENGTH]{};
    std::memset(buf, 0xAA, sizeof(buf));
    // Use a buffer smaller than ADDRESS_OFFSET + ADDRESS_LENGTH
    UnsafeBuffer small_buf{buf, PublicationErrorFrameFlyweight::ADDRESS_OFFSET};
    PublicationErrorFrameFlyweight fw{small_buf, 0};

    std::byte snapshot[PUBLICATION_ERROR_FRAME_LENGTH];
    std::memcpy(snapshot, buf, sizeof(buf));

    const u8 addr[16] = {127, 0, 0, 1};
    fw.set_address(addr, 16);

    // Buffer should be completely unchanged
    EXPECT_EQ(std::memcmp(buf, snapshot, PublicationErrorFrameFlyweight::ADDRESS_OFFSET), 0);
}

TEST(PublicationErrorFrameFlyweightTest, SetAddressNullptrZeroesField)
{
    std::byte buf[PUBLICATION_ERROR_FRAME_LENGTH]{};
    std::memset(buf, 0xFF, sizeof(buf));
    UnsafeBuffer buffer{buf, sizeof(buf)};
    PublicationErrorFrameFlyweight fw{buffer, 0};

    // nullptr src should zero the entire 16-byte address field
    fw.set_address(nullptr, 0);

    const u8* addr = fw.address();
    for (int i = 0; i < 16; ++i)
        EXPECT_EQ(addr[i], 0);
}

// ===========================================================================
// Finding 9: Wire-format layout tests for remaining flyweights
// ===========================================================================

TEST(SubscriptionMessageFlyweightTest, RawByteLayout)
{
    std::byte buf[SUBSCRIPTION_MSG_MINIMUM_LENGTH]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    SubscriptionMessageFlyweight fw{buffer, 0};

    fw.set_client_id(0x0102030405060708LL);
    fw.set_correlation_id(0x1112131415161718LL);
    fw.set_registration_correlation_id(0x2122232425262728LL);
    fw.set_stream_id(0x31323334);

    EXPECT_EQ(buf[0], std::byte{0x08});   // client_id low byte
    EXPECT_EQ(buf[7], std::byte{0x01});   // client_id high byte
    EXPECT_EQ(buf[8], std::byte{0x18});   // correlation_id low byte
    EXPECT_EQ(buf[15], std::byte{0x11});  // correlation_id high byte
    EXPECT_EQ(buf[16], std::byte{0x28});  // registration_correlation_id low byte
    EXPECT_EQ(buf[23], std::byte{0x21});  // registration_correlation_id high byte
    EXPECT_EQ(buf[24], std::byte{0x34});  // stream_id low byte
    EXPECT_EQ(buf[27], std::byte{0x31});  // stream_id high byte
    EXPECT_EQ(buf[28], std::byte{0x00});  // channel_length = 0
}

TEST(DestinationMessageFlyweightTest, RawByteLayout)
{
    std::byte buf[DESTINATION_MSG_MINIMUM_LENGTH]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    DestinationMessageFlyweight fw{buffer, 0};

    fw.set_client_id(0x0102030405060708LL);
    fw.set_correlation_id(0x1112131415161718LL);
    fw.set_registration_correlation_id(0x2122232425262728LL);

    EXPECT_EQ(buf[0], std::byte{0x08});   // client_id low byte
    EXPECT_EQ(buf[7], std::byte{0x01});   // client_id high byte
    EXPECT_EQ(buf[8], std::byte{0x18});   // correlation_id low byte
    EXPECT_EQ(buf[15], std::byte{0x11});  // correlation_id high byte
    EXPECT_EQ(buf[16], std::byte{0x28});  // registration_correlation_id low byte
    EXPECT_EQ(buf[23], std::byte{0x21});  // registration_correlation_id high byte
    EXPECT_EQ(buf[24], std::byte{0x00});  // channel_length = 0
}

TEST(DestinationByIdMessageFlyweightTest, RawByteLayout)
{
    std::byte buf[DESTINATION_BY_ID_MSG_LENGTH]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    DestinationByIdMessageFlyweight fw{buffer, 0};

    fw.set_client_id(0x0102030405060708LL);
    fw.set_correlation_id(0x1112131415161718LL);
    fw.set_resource_registration_id(0x2122232425262728LL);
    fw.set_destination_registration_id(0x3132333435363738LL);

    EXPECT_EQ(buf[0], std::byte{0x08});   // client_id low byte
    EXPECT_EQ(buf[7], std::byte{0x01});   // client_id high byte
    EXPECT_EQ(buf[8], std::byte{0x18});   // correlation_id low byte
    EXPECT_EQ(buf[15], std::byte{0x11});  // correlation_id high byte
    EXPECT_EQ(buf[16], std::byte{0x28});  // resource_registration_id low byte
    EXPECT_EQ(buf[23], std::byte{0x21});  // resource_registration_id high byte
    EXPECT_EQ(buf[24], std::byte{0x38});  // destination_registration_id low byte
    EXPECT_EQ(buf[31], std::byte{0x31});  // destination_registration_id high byte
}

TEST(CounterMessageFlyweightTest, RawByteLayout)
{
    std::byte buf[64]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    CounterMessageFlyweight fw{buffer, 0};

    fw.set_client_id(0x0102030405060708LL);
    fw.set_correlation_id(0x1112131415161718LL);
    fw.set_counter_type(0x21222324);
    fw.set_key_buffer("\xAB\xCD", 2);

    EXPECT_EQ(buf[0], std::byte{0x08});   // client_id low byte
    EXPECT_EQ(buf[8], std::byte{0x18});   // correlation_id low byte
    EXPECT_EQ(buf[16], std::byte{0x24});  // counter_type low byte
    EXPECT_EQ(buf[20], std::byte{0x02});  // key_buffer_length = 2
    EXPECT_EQ(buf[24], std::byte{0xAB});  // key_buffer[0]
    EXPECT_EQ(buf[25], std::byte{0xCD});  // key_buffer[1]
}

TEST(ErrorResponseFlyweightTest, RawByteLayout)
{
    std::byte buf[64]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    ErrorResponseFlyweight fw{buffer, 0};

    fw.set_offending_correlation_id(0x0102030405060708LL);
    fw.set_error_code(0x11121314);
    fw.set_error_message("\xAB\xCD", 2);

    EXPECT_EQ(buf[0], std::byte{0x08});   // offending_correlation_id low byte
    EXPECT_EQ(buf[7], std::byte{0x01});   // offending_correlation_id high byte
    EXPECT_EQ(buf[8], std::byte{0x14});   // error_code low byte
    EXPECT_EQ(buf[12], std::byte{0x02});  // error_message_length = 2
    EXPECT_EQ(buf[16], std::byte{0xAB});  // error_message[0]
    EXPECT_EQ(buf[17], std::byte{0xCD});  // error_message[1]
}

TEST(PublicationBuffersReadyFlyweightTest, RawByteLayout)
{
    std::byte buf[64]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    PublicationBuffersReadyFlyweight fw{buffer, 0};

    fw.set_correlation_id(0x0102030405060708LL);
    fw.set_registration_id(0x1112131415161718LL);
    fw.set_session_id(0x21222324);
    fw.set_stream_id(0x31323334);
    fw.set_pub_limit_counter_id(0x41424344);
    fw.set_channel_status_counter_id(0x51525354);
    fw.set_log_file_name("\xAB\xCD", 2);

    EXPECT_EQ(buf[0], std::byte{0x08});   // correlation_id low byte
    EXPECT_EQ(buf[7], std::byte{0x01});   // correlation_id high byte
    EXPECT_EQ(buf[8], std::byte{0x18});   // registration_id low byte
    EXPECT_EQ(buf[15], std::byte{0x11});  // registration_id high byte
    EXPECT_EQ(buf[16], std::byte{0x24});  // session_id low byte
    EXPECT_EQ(buf[20], std::byte{0x34});  // stream_id low byte
    EXPECT_EQ(buf[24], std::byte{0x44});  // pub_limit_counter_id low byte
    EXPECT_EQ(buf[28], std::byte{0x54});  // channel_status_counter_id low byte
    EXPECT_EQ(buf[32], std::byte{0x02});  // log_file_name_length = 2
    EXPECT_EQ(buf[36], std::byte{0xAB});  // log_file_name[0]
    EXPECT_EQ(buf[37], std::byte{0xCD});  // log_file_name[1]
}

// ===========================================================================
// Finding 10: length() positive round-trip tests
// ===========================================================================

TEST(ErrorResponseFlyweightTest, LengthPositiveRoundTrip)
{
    std::byte buf[128]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    ErrorResponseFlyweight fw{buffer, 0};

    // With empty error message
    fw.set_error_message("", 0);
    EXPECT_EQ(fw.length(), ErrorResponseFlyweight::ERROR_MESSAGE_OFFSET);
    EXPECT_EQ(fw.length(), ErrorResponseFlyweight::compute_length(0));

    // With a real error message
    fw.set_error_message("hello", 5);
    EXPECT_EQ(fw.length(), ErrorResponseFlyweight::ERROR_MESSAGE_OFFSET + 5);
    EXPECT_EQ(fw.length(), ErrorResponseFlyweight::compute_length(5));

    // Round-trip: set then read
    EXPECT_EQ(fw.error_message_length(), 5);
    EXPECT_EQ(fw.length(), 21);
}

TEST(PublicationErrorFrameFlyweightTest, LengthPositiveRoundTrip)
{
    std::byte buf[256]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    PublicationErrorFrameFlyweight fw{buffer, 0};

    // With empty error message
    fw.set_error_message("", 0);
    EXPECT_EQ(fw.length(), PUBLICATION_ERROR_FRAME_LENGTH);
    EXPECT_EQ(fw.length(), PublicationErrorFrameFlyweight::compute_length(0));

    // With a real error message
    fw.set_error_message("test error msg", 14);
    EXPECT_EQ(fw.length(), PUBLICATION_ERROR_FRAME_LENGTH + 14);
    EXPECT_EQ(fw.length(), PublicationErrorFrameFlyweight::compute_length(14));

    // Round-trip: set then read
    EXPECT_EQ(fw.error_message_length(), 14);
    EXPECT_EQ(fw.length(), 82);
}

// ===========================================================================
// Finding 11: set_revoke(false) test
// ===========================================================================

TEST(RemovePublicationFlyweightTest, SetRevokeFalse)
{
    std::byte buf[REMOVE_PUBLICATION_LENGTH]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    RemovePublicationFlyweight fw{buffer, 0};

    fw.set_client_id(42);
    fw.set_correlation_id(100);
    fw.set_registration_id(12345);
    fw.set_revoke(true);
    EXPECT_TRUE(fw.revoke());
    EXPECT_TRUE(fw.revoke(REMOVE_PUBLICATION_LENGTH));

    // Clear the revoke flag
    fw.set_revoke(false);
    EXPECT_FALSE(fw.revoke());
    EXPECT_FALSE(fw.revoke(REMOVE_PUBLICATION_LENGTH));
}

// ===========================================================================
// Finding 4: ErrorResponseFlyweight compute_length tests
// ===========================================================================

TEST(ErrorResponseFlyweightTest, ComputeLengthNegativeReturnsMinusOne)
{
    EXPECT_EQ(ErrorResponseFlyweight::compute_length(-1), -1);
    EXPECT_EQ(ErrorResponseFlyweight::compute_length(-100), -1);
    EXPECT_EQ(ErrorResponseFlyweight::compute_length(0), 16);
}

TEST(ErrorResponseFlyweightTest, ComputeLengthRejectsPositiveOverflow)
{
    EXPECT_EQ(ErrorResponseFlyweight::compute_length(std::numeric_limits<i32>::max()), -1);
}

// ===========================================================================
// Finding 5: PublicationBuffersReadyFlyweight compute_length tests
// ===========================================================================

TEST(PublicationBuffersReadyFlyweightTest, ComputeLengthNegativeReturnsMinusOne)
{
    EXPECT_EQ(PublicationBuffersReadyFlyweight::compute_length(-1), -1);
    EXPECT_EQ(PublicationBuffersReadyFlyweight::compute_length(-100), -1);
    EXPECT_EQ(PublicationBuffersReadyFlyweight::compute_length(0), 36);
}

TEST(PublicationBuffersReadyFlyweightTest, ComputeLengthRejectsPositiveOverflow)
{
    EXPECT_EQ(PublicationBuffersReadyFlyweight::compute_length(std::numeric_limits<i32>::max()), -1);
}

// ===========================================================================
// Negative offset rejection tests (HIGH-1 / HIGH-2 regression)
// ===========================================================================

TEST(PublicationMessageFlyweightTest, NegativeOffsetChannelReturnsNullptr)
{
    std::byte buf[128]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    PublicationMessageFlyweight fw{buffer, -1};

    EXPECT_EQ(fw.channel(), nullptr);
}

TEST(PublicationMessageFlyweightTest, NegativeOffsetSetChannelIsNoop)
{
    std::byte buf[128]{};
    std::memset(buf, 0xAA, sizeof(buf));
    UnsafeBuffer buffer{buf, sizeof(buf)};
    PublicationMessageFlyweight fw{buffer, -1};

    std::byte snapshot[128];
    std::memcpy(snapshot, buf, sizeof(buf));

    fw.set_channel("hello", 5);

    EXPECT_EQ(std::memcmp(buf, snapshot, sizeof(buf)), 0);
}

TEST(SubscriptionMessageFlyweightTest, NegativeOffsetChannelReturnsNullptr)
{
    std::byte buf[128]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    SubscriptionMessageFlyweight fw{buffer, -1};

    EXPECT_EQ(fw.channel(), nullptr);
}

TEST(SubscriptionMessageFlyweightTest, NegativeOffsetSetChannelIsNoop)
{
    std::byte buf[128]{};
    std::memset(buf, 0xAA, sizeof(buf));
    UnsafeBuffer buffer{buf, sizeof(buf)};
    SubscriptionMessageFlyweight fw{buffer, -1};

    std::byte snapshot[128];
    std::memcpy(snapshot, buf, sizeof(buf));

    fw.set_channel("hello", 5);

    EXPECT_EQ(std::memcmp(buf, snapshot, sizeof(buf)), 0);
}

TEST(CounterMessageFlyweightTest, NegativeOffsetKeyBufferReturnsNullptr)
{
    std::byte buf[64]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    CounterMessageFlyweight fw{buffer, -1};

    EXPECT_EQ(fw.key_buffer(), nullptr);
}

TEST(CounterMessageFlyweightTest, NegativeOffsetLabelLengthReturnsMinusOne)
{
    std::byte buf[64]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    CounterMessageFlyweight fw{buffer, -1};

    EXPECT_EQ(fw.label_length(), -1);
}

TEST(CounterMessageFlyweightTest, NegativeOffsetLabelReturnsNullptr)
{
    std::byte buf[64]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    CounterMessageFlyweight fw{buffer, -1};

    EXPECT_EQ(fw.label(), nullptr);
}

TEST(CounterMessageFlyweightTest, NegativeOffsetSetKeyBufferIsNoop)
{
    std::byte buf[64]{};
    std::memset(buf, 0xAA, sizeof(buf));
    UnsafeBuffer buffer{buf, sizeof(buf)};
    CounterMessageFlyweight fw{buffer, -1};

    std::byte snapshot[64];
    std::memcpy(snapshot, buf, sizeof(buf));

    fw.set_key_buffer("hello", 5);

    EXPECT_EQ(std::memcmp(buf, snapshot, sizeof(buf)), 0);
}

TEST(CounterMessageFlyweightTest, NegativeOffsetSetLabelIsNoop)
{
    std::byte buf[64]{};
    std::memset(buf, 0xAA, sizeof(buf));
    UnsafeBuffer buffer{buf, sizeof(buf)};
    CounterMessageFlyweight fw{buffer, -1};

    std::byte snapshot[64];
    std::memcpy(snapshot, buf, sizeof(buf));

    fw.set_label("hello", 5);

    EXPECT_EQ(std::memcmp(buf, snapshot, sizeof(buf)), 0);
}

TEST(DestinationMessageFlyweightTest, NegativeOffsetChannelReturnsNullptr)
{
    std::byte buf[128]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    DestinationMessageFlyweight fw{buffer, -1};

    EXPECT_EQ(fw.channel(), nullptr);
}

TEST(DestinationMessageFlyweightTest, NegativeOffsetSetChannelIsNoop)
{
    std::byte buf[128]{};
    std::memset(buf, 0xAA, sizeof(buf));
    UnsafeBuffer buffer{buf, sizeof(buf)};
    DestinationMessageFlyweight fw{buffer, -1};

    std::byte snapshot[128];
    std::memcpy(snapshot, buf, sizeof(buf));

    fw.set_channel("hello", 5);

    EXPECT_EQ(std::memcmp(buf, snapshot, sizeof(buf)), 0);
}

TEST(TerminateDriverFlyweightTest, NegativeOffsetTokenBufferReturnsNullptr)
{
    std::byte buf[64]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    TerminateDriverFlyweight fw{buffer, -1};

    EXPECT_EQ(fw.token_buffer(), nullptr);
}

TEST(TerminateDriverFlyweightTest, NegativeOffsetSetTokenBufferIsNoop)
{
    std::byte buf[64]{};
    std::memset(buf, 0xAA, sizeof(buf));
    UnsafeBuffer buffer{buf, sizeof(buf)};
    TerminateDriverFlyweight fw{buffer, -1};

    std::byte snapshot[64];
    std::memcpy(snapshot, buf, sizeof(buf));

    fw.set_token_buffer("hello", 5);

    EXPECT_EQ(std::memcmp(buf, snapshot, sizeof(buf)), 0);
}

TEST(RejectImageFlyweightTest, NegativeOffsetReasonReturnsNullptr)
{
    std::byte buf[64]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    RejectImageFlyweight fw{buffer, -1};

    EXPECT_EQ(fw.reason(), nullptr);
}

TEST(RejectImageFlyweightTest, NegativeOffsetSetReasonIsNoop)
{
    std::byte buf[64]{};
    std::memset(buf, 0xAA, sizeof(buf));
    UnsafeBuffer buffer{buf, sizeof(buf)};
    RejectImageFlyweight fw{buffer, -1};

    std::byte snapshot[64];
    std::memcpy(snapshot, buf, sizeof(buf));

    fw.set_reason("hello", 5);

    EXPECT_EQ(std::memcmp(buf, snapshot, sizeof(buf)), 0);
}

TEST(ErrorResponseFlyweightTest, NegativeOffsetErrorMessageReturnsNullptr)
{
    std::byte buf[64]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    ErrorResponseFlyweight fw{buffer, -1};

    EXPECT_EQ(fw.error_message(), nullptr);
}

TEST(ErrorResponseFlyweightTest, NegativeOffsetSetErrorMessageIsNoop)
{
    std::byte buf[64]{};
    std::memset(buf, 0xAA, sizeof(buf));
    UnsafeBuffer buffer{buf, sizeof(buf)};
    ErrorResponseFlyweight fw{buffer, -1};

    std::byte snapshot[64];
    std::memcpy(snapshot, buf, sizeof(buf));

    fw.set_error_message("hello", 5);

    EXPECT_EQ(std::memcmp(buf, snapshot, sizeof(buf)), 0);
}

TEST(PublicationBuffersReadyFlyweightTest, NegativeOffsetLogFileNameReturnsNullptr)
{
    std::byte buf[64]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    PublicationBuffersReadyFlyweight fw{buffer, -1};

    EXPECT_EQ(fw.log_file_name(), nullptr);
}

TEST(PublicationBuffersReadyFlyweightTest, NegativeOffsetSetLogFileNameIsNoop)
{
    std::byte buf[64]{};
    std::memset(buf, 0xAA, sizeof(buf));
    UnsafeBuffer buffer{buf, sizeof(buf)};
    PublicationBuffersReadyFlyweight fw{buffer, -1};

    std::byte snapshot[64];
    std::memcpy(snapshot, buf, sizeof(buf));

    fw.set_log_file_name("hello", 5);

    EXPECT_EQ(std::memcmp(buf, snapshot, sizeof(buf)), 0);
}

TEST(ImageBuffersReadyFlyweightTest, NegativeOffsetLogFileNameReturnsNullptr)
{
    std::byte buf[64]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    ImageBuffersReadyFlyweight fw{buffer, -1};

    EXPECT_EQ(fw.log_file_name(), nullptr);
}

TEST(ImageBuffersReadyFlyweightTest, NegativeOffsetSourceIdentityReturnsNullptr)
{
    std::byte buf[64]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    ImageBuffersReadyFlyweight fw{buffer, -1};

    EXPECT_EQ(fw.source_identity(), nullptr);
}

TEST(ImageBuffersReadyFlyweightTest, NegativeOffsetSourceIdentityLengthReturnsMinusOne)
{
    std::byte buf[64]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    ImageBuffersReadyFlyweight fw{buffer, -1};

    EXPECT_EQ(fw.source_identity_length(), -1);
}

TEST(ImageBuffersReadyFlyweightTest, NegativeOffsetSetLogFileNameIsNoop)
{
    std::byte buf[64]{};
    std::memset(buf, 0xAA, sizeof(buf));
    UnsafeBuffer buffer{buf, sizeof(buf)};
    ImageBuffersReadyFlyweight fw{buffer, -1};

    std::byte snapshot[64];
    std::memcpy(snapshot, buf, sizeof(buf));

    fw.set_log_file_name("hello", 5);

    EXPECT_EQ(std::memcmp(buf, snapshot, sizeof(buf)), 0);
}

TEST(ImageBuffersReadyFlyweightTest, NegativeOffsetSetSourceIdentityIsNoop)
{
    std::byte buf[64]{};
    std::memset(buf, 0xAA, sizeof(buf));
    UnsafeBuffer buffer{buf, sizeof(buf)};
    ImageBuffersReadyFlyweight fw{buffer, -1};

    std::byte snapshot[64];
    std::memcpy(snapshot, buf, sizeof(buf));

    fw.set_source_identity("hello", 5);

    EXPECT_EQ(std::memcmp(buf, snapshot, sizeof(buf)), 0);
}

TEST(ImageMessageFlyweightTest, NegativeOffsetChannelReturnsNullptr)
{
    std::byte buf[64]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    ImageMessageFlyweight fw{buffer, -1};

    EXPECT_EQ(fw.channel(), nullptr);
}

TEST(ImageMessageFlyweightTest, NegativeOffsetSetChannelIsNoop)
{
    std::byte buf[64]{};
    std::memset(buf, 0xAA, sizeof(buf));
    UnsafeBuffer buffer{buf, sizeof(buf)};
    ImageMessageFlyweight fw{buffer, -1};

    std::byte snapshot[64];
    std::memcpy(snapshot, buf, sizeof(buf));

    fw.set_channel("hello", 5);

    EXPECT_EQ(std::memcmp(buf, snapshot, sizeof(buf)), 0);
}

TEST(PublicationErrorFrameFlyweightTest, NegativeOffsetAddressReturnsNullptr)
{
    std::byte buf[256]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    PublicationErrorFrameFlyweight fw{buffer, -1};

    EXPECT_EQ(fw.address(), nullptr);
}

TEST(PublicationErrorFrameFlyweightTest, NegativeOffsetErrorMessageReturnsNullptr)
{
    std::byte buf[256]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    PublicationErrorFrameFlyweight fw{buffer, -1};

    EXPECT_EQ(fw.error_message(), nullptr);
}

TEST(PublicationErrorFrameFlyweightTest, NegativeOffsetSetAddressIsNoop)
{
    std::byte buf[256]{};
    std::memset(buf, 0xAA, sizeof(buf));
    UnsafeBuffer buffer{buf, sizeof(buf)};
    PublicationErrorFrameFlyweight fw{buffer, -1};

    std::byte snapshot[256];
    std::memcpy(snapshot, buf, sizeof(buf));

    u8 addr[16] = {127, 0, 0, 1};
    fw.set_address(addr, 16);

    EXPECT_EQ(std::memcmp(buf, snapshot, sizeof(buf)), 0);
}

TEST(PublicationErrorFrameFlyweightTest, NegativeOffsetSetErrorMessageIsNoop)
{
    std::byte buf[256]{};
    std::memset(buf, 0xAA, sizeof(buf));
    UnsafeBuffer buffer{buf, sizeof(buf)};
    PublicationErrorFrameFlyweight fw{buffer, -1};

    std::byte snapshot[256];
    std::memcpy(snapshot, buf, sizeof(buf));

    fw.set_error_message("hello", 5);

    EXPECT_EQ(std::memcmp(buf, snapshot, sizeof(buf)), 0);
}

TEST(StaticCounterMessageFlyweightTest, NegativeOffsetKeyBufferReturnsNullptr)
{
    std::byte buf[64]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    StaticCounterMessageFlyweight fw{buffer, -1};

    EXPECT_EQ(fw.key_buffer(), nullptr);
}

TEST(StaticCounterMessageFlyweightTest, NegativeOffsetLabelLengthOffsetReturnsMinusOne)
{
    std::byte buf[64]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    StaticCounterMessageFlyweight fw{buffer, -1};

    EXPECT_EQ(fw.label_length_offset(), -1);
}

TEST(StaticCounterMessageFlyweightTest, NegativeOffsetLabelReturnsNullptr)
{
    std::byte buf[64]{};
    UnsafeBuffer buffer{buf, sizeof(buf)};
    StaticCounterMessageFlyweight fw{buffer, -1};

    EXPECT_EQ(fw.label(), nullptr);
}

TEST(StaticCounterMessageFlyweightTest, NegativeOffsetSetKeyBufferIsNoop)
{
    std::byte buf[64]{};
    std::memset(buf, 0xAA, sizeof(buf));
    UnsafeBuffer buffer{buf, sizeof(buf)};
    StaticCounterMessageFlyweight fw{buffer, -1};

    std::byte snapshot[64];
    std::memcpy(snapshot, buf, sizeof(buf));

    fw.set_key_buffer("hello", 5);

    EXPECT_EQ(std::memcmp(buf, snapshot, sizeof(buf)), 0);
}

TEST(StaticCounterMessageFlyweightTest, NegativeOffsetSetLabelIsNoop)
{
    std::byte buf[64]{};
    std::memset(buf, 0xAA, sizeof(buf));
    UnsafeBuffer buffer{buf, sizeof(buf)};
    StaticCounterMessageFlyweight fw{buffer, -1};

    std::byte snapshot[64];
    std::memcpy(snapshot, buf, sizeof(buf));

    fw.set_label("hello", 5);

    EXPECT_EQ(std::memcmp(buf, snapshot, sizeof(buf)), 0);
}
