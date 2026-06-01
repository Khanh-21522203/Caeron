#include "caeron/cnc/driver_proxy.h"
#include "caeron/cnc/client_command_adapter.h"
#include "caeron/cnc/client_proxy.h"
#include "caeron/cnc/driver_events_adapter.h"
#include "caeron/concurrent/many_to_one_ring_buffer.h"
#include "caeron/concurrent/broadcast_transmitter.h"
#include "caeron/concurrent/broadcast_receiver.h"
#include "caeron/concurrent/unsafe_buffer.h"
#include "caeron/command/control_protocol_events.h"

#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace caeron;
using namespace caeron::concurrent;
using namespace caeron::cnc;
using namespace caeron::command;

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

class CncTest : public ::testing::Test
{
protected:
    // Ring buffer (to-driver): 128 header + 1MB data
    static constexpr i32 RING_DATA_REGION = 1024 * 1024;
    static constexpr i32 RING_BUFFER_SIZE = ManyToOneRingBuffer::HEADER_LENGTH + RING_DATA_REGION;

    // Broadcast buffer (to-clients): 64 header + 1MB data
    static constexpr i32 BROADCAST_DATA_REGION = 1024 * 1024;
    static constexpr i32 BROADCAST_BUFFER_SIZE = BroadcastTransmitter::HEADER_LENGTH + BROADCAST_DATA_REGION;

    void SetUp() override
    {
        ring_storage_ = std::make_unique<std::byte[]>(RING_BUFFER_SIZE);
        std::memset(ring_storage_.get(), 0, RING_BUFFER_SIZE);
        ring_buffer_ = UnsafeBuffer{ring_storage_.get(), RING_BUFFER_SIZE};

        broadcast_storage_ = std::make_unique<std::byte[]>(BROADCAST_BUFFER_SIZE);
        std::memset(broadcast_storage_.get(), 0, BROADCAST_BUFFER_SIZE);
        broadcast_buffer_ = UnsafeBuffer{broadcast_storage_.get(), BROADCAST_BUFFER_SIZE};
    }

    std::unique_ptr<std::byte[]> ring_storage_;
    UnsafeBuffer ring_buffer_;

    std::unique_ptr<std::byte[]> broadcast_storage_;
    UnsafeBuffer broadcast_buffer_;
};

// ---------------------------------------------------------------------------
// Mock handler for ClientCommandAdapter (driver side)
// ---------------------------------------------------------------------------

struct RecordedCommand
{
    i32 type;
    i64 correlation_id;
    i64 client_id;
    i32 stream_id;
    i64 registration_id;
    bool is_exclusive;
    bool revoke;
    std::string channel;
    std::string reason;
    i32 counter_type;
    i32 counter_id;
    i64 image_correlation_id;
    i64 position;
};

struct MockDriverConductor
{
    std::vector<RecordedCommand> commands;

    void on_add_publication(std::string_view channel, i32 stream_id, i64 correlation_id, i64 client_id, bool is_exclusive)
    {
        RecordedCommand cmd{};
        cmd.type = is_exclusive ? ADD_EXCLUSIVE_PUBLICATION : ADD_PUBLICATION;
        cmd.correlation_id = correlation_id;
        cmd.client_id = client_id;
        cmd.stream_id = stream_id;
        cmd.is_exclusive = is_exclusive;
        cmd.channel = std::string(channel);
        commands.push_back(std::move(cmd));
    }

    void on_remove_publication(i64 registration_id, i64 correlation_id, bool revoke)
    {
        RecordedCommand cmd{};
        cmd.type = REMOVE_PUBLICATION;
        cmd.correlation_id = correlation_id;
        cmd.registration_id = registration_id;
        cmd.revoke = revoke;
        commands.push_back(std::move(cmd));
    }

    void on_add_subscription(std::string_view channel, i32 stream_id, i64 correlation_id, i64 client_id)
    {
        RecordedCommand cmd{};
        cmd.type = ADD_SUBSCRIPTION;
        cmd.correlation_id = correlation_id;
        cmd.client_id = client_id;
        cmd.stream_id = stream_id;
        cmd.channel = std::string(channel);
        commands.push_back(std::move(cmd));
    }

    void on_remove_subscription(i64 registration_id, i64 correlation_id)
    {
        RecordedCommand cmd{};
        cmd.type = REMOVE_SUBSCRIPTION;
        cmd.correlation_id = correlation_id;
        cmd.registration_id = registration_id;
        commands.push_back(std::move(cmd));
    }

    void on_add_send_destination(i64 registration_id, std::string_view channel, i64 correlation_id)
    {
        RecordedCommand cmd{};
        cmd.type = ADD_DESTINATION;
        cmd.correlation_id = correlation_id;
        cmd.registration_id = registration_id;
        cmd.channel = std::string(channel);
        commands.push_back(std::move(cmd));
    }

    void on_remove_send_destination(i64 registration_id, std::string_view channel, i64 correlation_id)
    {
        RecordedCommand cmd{};
        cmd.type = REMOVE_DESTINATION;
        cmd.correlation_id = correlation_id;
        cmd.registration_id = registration_id;
        cmd.channel = std::string(channel);
        commands.push_back(std::move(cmd));
    }

    void on_remove_send_destination_by_id(i64 resource_registration_id, i64 destination_registration_id, i64 correlation_id)
    {
        RecordedCommand cmd{};
        cmd.type = REMOVE_DESTINATION_BY_ID;
        cmd.correlation_id = correlation_id;
        cmd.registration_id = resource_registration_id;
        commands.push_back(std::move(cmd));
    }

    void on_add_rcv_destination(i64 registration_id, std::string_view channel, i64 correlation_id)
    {
        RecordedCommand cmd{};
        cmd.type = ADD_RCV_DESTINATION;
        cmd.correlation_id = correlation_id;
        cmd.registration_id = registration_id;
        cmd.channel = std::string(channel);
        commands.push_back(std::move(cmd));
    }

    void on_remove_rcv_destination(i64 registration_id, std::string_view channel, i64 correlation_id)
    {
        RecordedCommand cmd{};
        cmd.type = REMOVE_RCV_DESTINATION;
        cmd.correlation_id = correlation_id;
        cmd.registration_id = registration_id;
        cmd.channel = std::string(channel);
        commands.push_back(std::move(cmd));
    }

    void on_client_keepalive(i64 client_id)
    {
        RecordedCommand cmd{};
        cmd.type = CLIENT_KEEPALIVE;
        cmd.client_id = client_id;
        commands.push_back(std::move(cmd));
    }

    void on_client_close(i64 client_id)
    {
        RecordedCommand cmd{};
        cmd.type = CLIENT_CLOSE;
        cmd.client_id = client_id;
        commands.push_back(std::move(cmd));
    }

    void on_add_counter(i32 type_id,
                        const void* /*key*/, i32 /*key_offset*/, i32 /*key_length*/,
                        const void* label, i32 label_offset, i32 label_length,
                        i64 correlation_id, i64 client_id)
    {
        RecordedCommand cmd{};
        cmd.type = ADD_COUNTER;
        cmd.correlation_id = correlation_id;
        cmd.client_id = client_id;
        cmd.counter_type = type_id;
        if (label != nullptr && label_length > 0)
            cmd.channel = std::string(static_cast<const char*>(label) + label_offset, label_length);
        commands.push_back(std::move(cmd));
    }

    void on_remove_counter(i64 registration_id, i64 correlation_id)
    {
        RecordedCommand cmd{};
        cmd.type = REMOVE_COUNTER;
        cmd.correlation_id = correlation_id;
        cmd.registration_id = registration_id;
        commands.push_back(std::move(cmd));
    }

    void on_terminate_driver(const void* /*token*/, i32 /*token_offset*/, i32 /*token_length*/)
    {
        RecordedCommand cmd{};
        cmd.type = TERMINATE_DRIVER;
        commands.push_back(std::move(cmd));
    }

    void on_add_static_counter(i32 type_id,
                               const void* /*key*/, i32 /*key_offset*/, i32 /*key_length*/,
                               const void* label, i32 label_offset, i32 label_length,
                               i64 registration_id, i64 correlation_id, i64 client_id)
    {
        RecordedCommand cmd{};
        cmd.type = ADD_STATIC_COUNTER;
        cmd.correlation_id = correlation_id;
        cmd.client_id = client_id;
        cmd.registration_id = registration_id;
        cmd.counter_type = type_id;
        if (label != nullptr && label_length > 0)
            cmd.channel = std::string(static_cast<const char*>(label) + label_offset, label_length);
        commands.push_back(std::move(cmd));
    }

    void on_reject_image(i64 correlation_id, i64 image_correlation_id, i64 position, std::string_view reason)
    {
        RecordedCommand cmd{};
        cmd.type = REJECT_IMAGE;
        cmd.correlation_id = correlation_id;
        cmd.image_correlation_id = image_correlation_id;
        cmd.position = position;
        cmd.reason = std::string(reason);
        commands.push_back(std::move(cmd));
    }

    void on_next_available_session_id(i64 correlation_id, i32 stream_id)
    {
        RecordedCommand cmd{};
        cmd.type = GET_NEXT_AVAILABLE_SESSION_ID;
        cmd.correlation_id = correlation_id;
        cmd.stream_id = stream_id;
        commands.push_back(std::move(cmd));
    }
};

// ---------------------------------------------------------------------------
// Mock handler for DriverEventsAdapter (client side)
// ---------------------------------------------------------------------------

struct RecordedEvent
{
    u16 type;
    i64 correlation_id;
    i64 registration_id;
    i64 destination_registration_id;
    i32 stream_id;
    i32 session_id;
    i32 counter_id;
    i32 channel_status_counter_id;
    i32 position_counter_id;
    i64 client_id;
    i64 receiver_id;
    i64 group_tag;
    i16 address_type;
    i16 udp_port;
    u8 address[16]{};
    std::string log_file_name;
    std::string source_identity;
    std::string error_message;
    i32 error_code;
};

struct MockClientConductor
{
    std::vector<RecordedEvent> events;

    void on_error(i64 correlation_id, i32 error_code, std::string_view error_message)
    {
        RecordedEvent ev{};
        ev.type = ON_ERROR;
        ev.correlation_id = correlation_id;
        ev.error_code = error_code;
        ev.error_message = std::string(error_message);
        events.push_back(std::move(ev));
    }

    void on_channel_endpoint_error(i64 correlation_id, std::string_view error_message)
    {
        RecordedEvent ev{};
        ev.type = ON_ERROR;
        ev.correlation_id = correlation_id;
        ev.error_message = std::string(error_message);
        events.push_back(std::move(ev));
    }

    void on_publication_ready(i64 correlation_id, i64 registration_id, i32 stream_id,
                              i32 session_id, i32 position_counter_id,
                              i32 channel_status_counter_id, std::string_view log_file_name)
    {
        RecordedEvent ev{};
        ev.type = ON_PUBLICATION_READY;
        ev.correlation_id = correlation_id;
        ev.registration_id = registration_id;
        ev.stream_id = stream_id;
        ev.session_id = session_id;
        ev.position_counter_id = position_counter_id;
        ev.channel_status_counter_id = channel_status_counter_id;
        ev.log_file_name = std::string(log_file_name);
        events.push_back(std::move(ev));
    }

    void on_exclusive_publication_ready(i64 correlation_id, i64 registration_id, i32 stream_id,
                                        i32 session_id, i32 position_counter_id,
                                        i32 channel_status_counter_id, std::string_view log_file_name)
    {
        RecordedEvent ev{};
        ev.type = ON_EXCLUSIVE_PUBLICATION_READY;
        ev.correlation_id = correlation_id;
        ev.registration_id = registration_id;
        ev.stream_id = stream_id;
        ev.session_id = session_id;
        ev.position_counter_id = position_counter_id;
        ev.channel_status_counter_id = channel_status_counter_id;
        ev.log_file_name = std::string(log_file_name);
        events.push_back(std::move(ev));
    }

    void on_subscription_ready(i64 correlation_id, i32 channel_status_counter_id)
    {
        RecordedEvent ev{};
        ev.type = ON_SUBSCRIPTION_READY;
        ev.correlation_id = correlation_id;
        ev.channel_status_counter_id = channel_status_counter_id;
        events.push_back(std::move(ev));
    }

    void on_operation_success(i64 correlation_id)
    {
        RecordedEvent ev{};
        ev.type = ON_OPERATION_SUCCESS;
        ev.correlation_id = correlation_id;
        events.push_back(std::move(ev));
    }

    void on_available_image(i64 correlation_id, i64 registration_id, i32 session_id,
                            i32 position_counter_id, std::string_view log_file_name,
                            std::string_view source_identity)
    {
        RecordedEvent ev{};
        ev.type = ON_AVAILABLE_IMAGE;
        ev.correlation_id = correlation_id;
        ev.registration_id = registration_id;
        ev.session_id = session_id;
        ev.position_counter_id = position_counter_id;
        ev.log_file_name = std::string(log_file_name);
        ev.source_identity = std::string(source_identity);
        events.push_back(std::move(ev));
    }

    void on_unavailable_image(i64 correlation_id, i64 subscription_registration_id)
    {
        RecordedEvent ev{};
        ev.type = ON_UNAVAILABLE_IMAGE;
        ev.correlation_id = correlation_id;
        ev.registration_id = subscription_registration_id;
        events.push_back(std::move(ev));
    }

    void on_counter_ready(i64 correlation_id, i32 counter_id)
    {
        RecordedEvent ev{};
        ev.type = ON_COUNTER_READY;
        ev.correlation_id = correlation_id;
        ev.counter_id = counter_id;
        events.push_back(std::move(ev));
    }

    void on_unavailable_counter(i64 correlation_id, i32 counter_id)
    {
        RecordedEvent ev{};
        ev.type = ON_UNAVAILABLE_COUNTER;
        ev.correlation_id = correlation_id;
        ev.counter_id = counter_id;
        events.push_back(std::move(ev));
    }

    void on_client_timeout(i64 client_id)
    {
        RecordedEvent ev{};
        ev.type = ON_CLIENT_TIMEOUT;
        ev.client_id = client_id;
        events.push_back(std::move(ev));
    }

    void on_static_counter(i64 correlation_id, i32 counter_id)
    {
        RecordedEvent ev{};
        ev.type = ON_STATIC_COUNTER;
        ev.correlation_id = correlation_id;
        ev.counter_id = counter_id;
        events.push_back(std::move(ev));
    }

    void on_next_available_session_id(i64 correlation_id, i32 session_id)
    {
        RecordedEvent ev{};
        ev.type = ON_NEXT_AVAILABLE_SESSION_ID;
        ev.correlation_id = correlation_id;
        ev.session_id = session_id;
        events.push_back(std::move(ev));
    }

    void on_publication_error_frame(i64 registration_id, i64 destination_registration_id,
                                    i32 session_id, i32 stream_id,
                                    i64 receiver_id, i64 group_tag,
                                    i16 address_type, i16 udp_port,
                                    const u8* address,
                                    i32 error_code, std::string_view error_message)
    {
        RecordedEvent ev{};
        ev.type = ON_PUBLICATION_ERROR;
        ev.registration_id = registration_id;
        ev.destination_registration_id = destination_registration_id;
        ev.session_id = session_id;
        ev.stream_id = stream_id;
        ev.receiver_id = receiver_id;
        ev.group_tag = group_tag;
        ev.address_type = address_type;
        ev.udp_port = udp_port;
        if (address != nullptr)
            std::memcpy(ev.address, address, 16);
        ev.error_code = error_code;
        ev.error_message = std::string(error_message);
        events.push_back(std::move(ev));
    }
};

// ===========================================================================
// DriverProxy -> ClientCommandAdapter tests
// ===========================================================================

TEST_F(CncTest, AddPublicationRoundTrip)
{
    ManyToOneRingBuffer ring{ring_buffer_};
    constexpr i64 CLIENT_ID = 42;

    DriverProxy proxy{ring, CLIENT_ID};
    const i64 corr_id = proxy.add_publication("aeron:udp?endpoint=localhost:40456", 1001);

    EXPECT_GT(corr_id, 0);

    MockDriverConductor conductor;
    ClientCommandAdapter adapter{ring, conductor};
    const i32 count = adapter.receive();

    EXPECT_EQ(count, 1);
    ASSERT_EQ(conductor.commands.size(), 1u);
    EXPECT_EQ(conductor.commands[0].type, ADD_PUBLICATION);
    EXPECT_EQ(conductor.commands[0].correlation_id, corr_id);
    EXPECT_EQ(conductor.commands[0].client_id, CLIENT_ID);
    EXPECT_EQ(conductor.commands[0].stream_id, 1001);
    EXPECT_FALSE(conductor.commands[0].is_exclusive);
    EXPECT_EQ(conductor.commands[0].channel, "aeron:udp?endpoint=localhost:40456");
}

TEST_F(CncTest, AddExclusivePublicationRoundTrip)
{
    ManyToOneRingBuffer ring{ring_buffer_};
    constexpr i64 CLIENT_ID = 42;

    DriverProxy proxy{ring, CLIENT_ID};
    const i64 corr_id = proxy.add_exclusive_publication("aeron:ipc", 2001);

    MockDriverConductor conductor;
    ClientCommandAdapter adapter{ring, conductor};
    adapter.receive();

    ASSERT_EQ(conductor.commands.size(), 1u);
    EXPECT_EQ(conductor.commands[0].type, ADD_EXCLUSIVE_PUBLICATION);
    EXPECT_EQ(conductor.commands[0].correlation_id, corr_id);
    EXPECT_TRUE(conductor.commands[0].is_exclusive);
    EXPECT_EQ(conductor.commands[0].channel, "aeron:ipc");
}

TEST_F(CncTest, RemovePublicationRoundTrip)
{
    ManyToOneRingBuffer ring{ring_buffer_};
    constexpr i64 CLIENT_ID = 42;

    DriverProxy proxy{ring, CLIENT_ID};
    const i64 corr_id = proxy.remove_publication(12345, true);

    MockDriverConductor conductor;
    ClientCommandAdapter adapter{ring, conductor};
    adapter.receive();

    ASSERT_EQ(conductor.commands.size(), 1u);
    EXPECT_EQ(conductor.commands[0].type, REMOVE_PUBLICATION);
    EXPECT_EQ(conductor.commands[0].correlation_id, corr_id);
    EXPECT_EQ(conductor.commands[0].registration_id, 12345);
    EXPECT_TRUE(conductor.commands[0].revoke);
}

TEST_F(CncTest, AddSubscriptionRoundTrip)
{
    ManyToOneRingBuffer ring{ring_buffer_};
    constexpr i64 CLIENT_ID = 42;

    DriverProxy proxy{ring, CLIENT_ID};
    const i64 corr_id = proxy.add_subscription("aeron:udp?endpoint=localhost:40456", 1001);

    MockDriverConductor conductor;
    ClientCommandAdapter adapter{ring, conductor};
    adapter.receive();

    ASSERT_EQ(conductor.commands.size(), 1u);
    EXPECT_EQ(conductor.commands[0].type, ADD_SUBSCRIPTION);
    EXPECT_EQ(conductor.commands[0].correlation_id, corr_id);
    EXPECT_EQ(conductor.commands[0].client_id, CLIENT_ID);
    EXPECT_EQ(conductor.commands[0].stream_id, 1001);
    EXPECT_EQ(conductor.commands[0].channel, "aeron:udp?endpoint=localhost:40456");
}

TEST_F(CncTest, RemoveSubscriptionRoundTrip)
{
    ManyToOneRingBuffer ring{ring_buffer_};
    constexpr i64 CLIENT_ID = 42;

    DriverProxy proxy{ring, CLIENT_ID};
    const i64 corr_id = proxy.remove_subscription(54321);

    MockDriverConductor conductor;
    ClientCommandAdapter adapter{ring, conductor};
    adapter.receive();

    ASSERT_EQ(conductor.commands.size(), 1u);
    EXPECT_EQ(conductor.commands[0].type, REMOVE_SUBSCRIPTION);
    EXPECT_EQ(conductor.commands[0].correlation_id, corr_id);
    EXPECT_EQ(conductor.commands[0].registration_id, 54321);
}

TEST_F(CncTest, AddDestinationRoundTrip)
{
    ManyToOneRingBuffer ring{ring_buffer_};
    constexpr i64 CLIENT_ID = 42;

    DriverProxy proxy{ring, CLIENT_ID};
    const i64 corr_id = proxy.add_destination(111, "aeron:udp?endpoint=localhost:40457");

    MockDriverConductor conductor;
    ClientCommandAdapter adapter{ring, conductor};
    adapter.receive();

    ASSERT_EQ(conductor.commands.size(), 1u);
    EXPECT_EQ(conductor.commands[0].type, ADD_DESTINATION);
    EXPECT_EQ(conductor.commands[0].correlation_id, corr_id);
    EXPECT_EQ(conductor.commands[0].registration_id, 111);
    EXPECT_EQ(conductor.commands[0].channel, "aeron:udp?endpoint=localhost:40457");
}

TEST_F(CncTest, RemoveDestinationRoundTrip)
{
    ManyToOneRingBuffer ring{ring_buffer_};
    constexpr i64 CLIENT_ID = 42;

    DriverProxy proxy{ring, CLIENT_ID};
    const i64 corr_id = proxy.remove_destination(111, "aeron:udp?endpoint=localhost:40457");

    MockDriverConductor conductor;
    ClientCommandAdapter adapter{ring, conductor};
    adapter.receive();

    ASSERT_EQ(conductor.commands.size(), 1u);
    EXPECT_EQ(conductor.commands[0].type, REMOVE_DESTINATION);
    EXPECT_EQ(conductor.commands[0].correlation_id, corr_id);
    EXPECT_EQ(conductor.commands[0].registration_id, 111);
}

TEST_F(CncTest, RemoveDestinationByIdRoundTrip)
{
    ManyToOneRingBuffer ring{ring_buffer_};
    constexpr i64 CLIENT_ID = 42;

    DriverProxy proxy{ring, CLIENT_ID};
    const i64 corr_id = proxy.remove_destination_by_id(111, 222);

    MockDriverConductor conductor;
    ClientCommandAdapter adapter{ring, conductor};
    adapter.receive();

    ASSERT_EQ(conductor.commands.size(), 1u);
    EXPECT_EQ(conductor.commands[0].type, REMOVE_DESTINATION_BY_ID);
    EXPECT_EQ(conductor.commands[0].correlation_id, corr_id);
    EXPECT_EQ(conductor.commands[0].registration_id, 111);
}

TEST_F(CncTest, AddRcvDestinationRoundTrip)
{
    ManyToOneRingBuffer ring{ring_buffer_};
    constexpr i64 CLIENT_ID = 42;

    DriverProxy proxy{ring, CLIENT_ID};
    const i64 corr_id = proxy.add_rcv_destination(333, "aeron:udp?endpoint=localhost:40458");

    MockDriverConductor conductor;
    ClientCommandAdapter adapter{ring, conductor};
    adapter.receive();

    ASSERT_EQ(conductor.commands.size(), 1u);
    EXPECT_EQ(conductor.commands[0].type, ADD_RCV_DESTINATION);
    EXPECT_EQ(conductor.commands[0].correlation_id, corr_id);
    EXPECT_EQ(conductor.commands[0].registration_id, 333);
}

TEST_F(CncTest, RemoveRcvDestinationRoundTrip)
{
    ManyToOneRingBuffer ring{ring_buffer_};
    constexpr i64 CLIENT_ID = 42;

    DriverProxy proxy{ring, CLIENT_ID};
    const i64 corr_id = proxy.remove_rcv_destination(333, "aeron:udp?endpoint=localhost:40458");

    MockDriverConductor conductor;
    ClientCommandAdapter adapter{ring, conductor};
    adapter.receive();

    ASSERT_EQ(conductor.commands.size(), 1u);
    EXPECT_EQ(conductor.commands[0].type, REMOVE_RCV_DESTINATION);
    EXPECT_EQ(conductor.commands[0].correlation_id, corr_id);
}

TEST_F(CncTest, AddCounterWithLabelRoundTrip)
{
    ManyToOneRingBuffer ring{ring_buffer_};
    constexpr i64 CLIENT_ID = 42;

    DriverProxy proxy{ring, CLIENT_ID};
    const i64 corr_id = proxy.add_counter(10, "test counter");

    MockDriverConductor conductor;
    ClientCommandAdapter adapter{ring, conductor};
    adapter.receive();

    ASSERT_EQ(conductor.commands.size(), 1u);
    EXPECT_EQ(conductor.commands[0].type, ADD_COUNTER);
    EXPECT_EQ(conductor.commands[0].correlation_id, corr_id);
    EXPECT_EQ(conductor.commands[0].counter_type, 10);
    EXPECT_EQ(conductor.commands[0].channel, "test counter");
}

TEST_F(CncTest, AddCounterWithKeyAndLabelRoundTrip)
{
    ManyToOneRingBuffer ring{ring_buffer_};
    constexpr i64 CLIENT_ID = 42;

    DriverProxy proxy{ring, CLIENT_ID};
    const u8 key[] = {0x01, 0x02, 0x03, 0x04};
    const i64 corr_id = proxy.add_counter(10, key, 0, 4, "my label", 0, 8);

    MockDriverConductor conductor;
    ClientCommandAdapter adapter{ring, conductor};
    adapter.receive();

    ASSERT_EQ(conductor.commands.size(), 1u);
    EXPECT_EQ(conductor.commands[0].type, ADD_COUNTER);
    EXPECT_EQ(conductor.commands[0].correlation_id, corr_id);
    EXPECT_EQ(conductor.commands[0].counter_type, 10);
}

TEST_F(CncTest, RemoveCounterRoundTrip)
{
    ManyToOneRingBuffer ring{ring_buffer_};
    constexpr i64 CLIENT_ID = 42;

    DriverProxy proxy{ring, CLIENT_ID};
    const i64 corr_id = proxy.remove_counter(999);

    MockDriverConductor conductor;
    ClientCommandAdapter adapter{ring, conductor};
    adapter.receive();

    ASSERT_EQ(conductor.commands.size(), 1u);
    EXPECT_EQ(conductor.commands[0].type, REMOVE_COUNTER);
    EXPECT_EQ(conductor.commands[0].correlation_id, corr_id);
    EXPECT_EQ(conductor.commands[0].registration_id, 999);
}

TEST_F(CncTest, ClientCloseRoundTrip)
{
    ManyToOneRingBuffer ring{ring_buffer_};
    constexpr i64 CLIENT_ID = 42;

    DriverProxy proxy{ring, CLIENT_ID};
    proxy.client_close();

    MockDriverConductor conductor;
    ClientCommandAdapter adapter{ring, conductor};
    adapter.receive();

    ASSERT_EQ(conductor.commands.size(), 1u);
    EXPECT_EQ(conductor.commands[0].type, CLIENT_CLOSE);
    EXPECT_EQ(conductor.commands[0].client_id, CLIENT_ID);
}

TEST_F(CncTest, TerminateDriverRoundTrip)
{
    ManyToOneRingBuffer ring{ring_buffer_};
    constexpr i64 CLIENT_ID = 42;

    DriverProxy proxy{ring, CLIENT_ID};
    const char token[] = "secret-token";
    EXPECT_TRUE(proxy.terminate_driver(token, 0, static_cast<i32>(strlen(token))));

    MockDriverConductor conductor;
    ClientCommandAdapter adapter{ring, conductor};
    adapter.receive();

    ASSERT_EQ(conductor.commands.size(), 1u);
    EXPECT_EQ(conductor.commands[0].type, TERMINATE_DRIVER);
}

TEST_F(CncTest, RejectImageRoundTrip)
{
    ManyToOneRingBuffer ring{ring_buffer_};
    constexpr i64 CLIENT_ID = 42;

    DriverProxy proxy{ring, CLIENT_ID};
    const i64 corr_id = proxy.reject_image(5555, 1024, "stale image");

    MockDriverConductor conductor;
    ClientCommandAdapter adapter{ring, conductor};
    adapter.receive();

    ASSERT_EQ(conductor.commands.size(), 1u);
    EXPECT_EQ(conductor.commands[0].type, REJECT_IMAGE);
    EXPECT_EQ(conductor.commands[0].correlation_id, corr_id);
    EXPECT_EQ(conductor.commands[0].image_correlation_id, 5555);
    EXPECT_EQ(conductor.commands[0].position, 1024);
    EXPECT_EQ(conductor.commands[0].reason, "stale image");
}

TEST_F(CncTest, GetNextAvailableSessionIdRoundTrip)
{
    ManyToOneRingBuffer ring{ring_buffer_};
    constexpr i64 CLIENT_ID = 42;

    DriverProxy proxy{ring, CLIENT_ID};
    const i64 corr_id = proxy.next_available_session_id(1001);

    MockDriverConductor conductor;
    ClientCommandAdapter adapter{ring, conductor};
    adapter.receive();

    ASSERT_EQ(conductor.commands.size(), 1u);
    EXPECT_EQ(conductor.commands[0].type, GET_NEXT_AVAILABLE_SESSION_ID);
    EXPECT_EQ(conductor.commands[0].correlation_id, corr_id);
    EXPECT_EQ(conductor.commands[0].stream_id, 1001);
}

TEST_F(CncTest, KeepaliveRoundTrip)
{
    ManyToOneRingBuffer ring{ring_buffer_};
    constexpr i64 CLIENT_ID = 42;

    DriverProxy proxy{ring, CLIENT_ID};
    proxy.keepalive();

    MockDriverConductor conductor;
    ClientCommandAdapter adapter{ring, conductor};
    adapter.receive();

    ASSERT_EQ(conductor.commands.size(), 1u);
    EXPECT_EQ(conductor.commands[0].type, CLIENT_KEEPALIVE);
    EXPECT_EQ(conductor.commands[0].client_id, CLIENT_ID);
}

TEST_F(CncTest, MultipleCommandsInSequence)
{
    ManyToOneRingBuffer ring{ring_buffer_};
    constexpr i64 CLIENT_ID = 42;

    DriverProxy proxy{ring, CLIENT_ID};

    // Write multiple commands
    proxy.add_publication("aeron:ipc", 1);
    proxy.add_subscription("aeron:ipc", 1);
    proxy.keepalive();
    proxy.remove_publication(100, false);

    MockDriverConductor conductor;
    ClientCommandAdapter adapter{ring, conductor};
    const i32 count = adapter.receive();

    EXPECT_EQ(count, 4);
    ASSERT_EQ(conductor.commands.size(), 4u);
    EXPECT_EQ(conductor.commands[0].type, ADD_PUBLICATION);
    EXPECT_EQ(conductor.commands[1].type, ADD_SUBSCRIPTION);
    EXPECT_EQ(conductor.commands[2].type, CLIENT_KEEPALIVE);
    EXPECT_EQ(conductor.commands[3].type, REMOVE_PUBLICATION);
}

TEST_F(CncTest, CorrelationIdsAreMonotonicallyIncreasing)
{
    ManyToOneRingBuffer ring{ring_buffer_};
    constexpr i64 CLIENT_ID = 42;

    DriverProxy proxy{ring, CLIENT_ID};

    const i64 id1 = proxy.add_publication("aeron:ipc", 1);
    const i64 id2 = proxy.add_publication("aeron:ipc", 2);
    const i64 id3 = proxy.add_subscription("aeron:ipc", 1);

    EXPECT_LT(id1, id2);
    EXPECT_LT(id2, id3);
}

// ===========================================================================
// ClientProxy -> DriverEventsAdapter tests
// ===========================================================================

TEST_F(CncTest, OnErrorRoundTrip)
{
    BroadcastTransmitter tx{broadcast_buffer_};
    BroadcastReceiver rx{broadcast_buffer_};

    constexpr i64 CLIENT_ID = 42;

    ClientProxy proxy{tx};
    proxy.on_error(100, ERROR_CODE_GENERIC, "something went wrong");

    MockClientConductor conductor;
    DriverEventsAdapter adapter{rx, conductor, CLIENT_ID};
    adapter.receive(100);

    ASSERT_EQ(conductor.events.size(), 1u);
    EXPECT_EQ(conductor.events[0].type, ON_ERROR);
    EXPECT_EQ(conductor.events[0].correlation_id, 100);
    EXPECT_EQ(conductor.events[0].error_code, ERROR_CODE_GENERIC);
    EXPECT_EQ(conductor.events[0].error_message, "something went wrong");
    EXPECT_EQ(adapter.received_correlation_id(), 100);
}

TEST_F(CncTest, OnPublicationReadyRoundTrip)
{
    BroadcastTransmitter tx{broadcast_buffer_};
    BroadcastReceiver rx{broadcast_buffer_};

    constexpr i64 CLIENT_ID = 42;

    ClientProxy proxy{tx};
    proxy.on_publication_ready(200, 300, 1001, 42, "/tmp/test.log", 5, 6, false);

    MockClientConductor conductor;
    DriverEventsAdapter adapter{rx, conductor, CLIENT_ID};
    adapter.receive(200);

    ASSERT_EQ(conductor.events.size(), 1u);
    EXPECT_EQ(conductor.events[0].type, ON_PUBLICATION_READY);
    EXPECT_EQ(conductor.events[0].correlation_id, 200);
    EXPECT_EQ(conductor.events[0].registration_id, 300);
    EXPECT_EQ(conductor.events[0].stream_id, 1001);
    EXPECT_EQ(conductor.events[0].session_id, 42);
    EXPECT_EQ(conductor.events[0].position_counter_id, 5);
    EXPECT_EQ(conductor.events[0].channel_status_counter_id, 6);
    EXPECT_EQ(conductor.events[0].log_file_name, "/tmp/test.log");
}

TEST_F(CncTest, OnExclusivePublicationReadyRoundTrip)
{
    BroadcastTransmitter tx{broadcast_buffer_};
    BroadcastReceiver rx{broadcast_buffer_};

    constexpr i64 CLIENT_ID = 42;

    ClientProxy proxy{tx};
    proxy.on_publication_ready(201, 301, 1002, 43, "/tmp/test2.log", 7, 8, true);

    MockClientConductor conductor;
    DriverEventsAdapter adapter{rx, conductor, CLIENT_ID};
    adapter.receive(201);

    ASSERT_EQ(conductor.events.size(), 1u);
    EXPECT_EQ(conductor.events[0].type, ON_EXCLUSIVE_PUBLICATION_READY);
    EXPECT_EQ(conductor.events[0].correlation_id, 201);
}

TEST_F(CncTest, OnSubscriptionReadyRoundTrip)
{
    BroadcastTransmitter tx{broadcast_buffer_};
    BroadcastReceiver rx{broadcast_buffer_};

    constexpr i64 CLIENT_ID = 42;

    ClientProxy proxy{tx};
    proxy.on_subscription_ready(300, 10);

    MockClientConductor conductor;
    DriverEventsAdapter adapter{rx, conductor, CLIENT_ID};
    adapter.receive(300);

    ASSERT_EQ(conductor.events.size(), 1u);
    EXPECT_EQ(conductor.events[0].type, ON_SUBSCRIPTION_READY);
    EXPECT_EQ(conductor.events[0].correlation_id, 300);
    EXPECT_EQ(conductor.events[0].channel_status_counter_id, 10);
}

TEST_F(CncTest, OnOperationSuccessRoundTrip)
{
    BroadcastTransmitter tx{broadcast_buffer_};
    BroadcastReceiver rx{broadcast_buffer_};

    constexpr i64 CLIENT_ID = 42;

    ClientProxy proxy{tx};
    proxy.operation_succeeded(400);

    MockClientConductor conductor;
    DriverEventsAdapter adapter{rx, conductor, CLIENT_ID};
    adapter.receive(400);

    ASSERT_EQ(conductor.events.size(), 1u);
    EXPECT_EQ(conductor.events[0].type, ON_OPERATION_SUCCESS);
    EXPECT_EQ(conductor.events[0].correlation_id, 400);
    EXPECT_EQ(adapter.received_correlation_id(), 400);
}

TEST_F(CncTest, OnCounterReadyRoundTrip)
{
    BroadcastTransmitter tx{broadcast_buffer_};
    BroadcastReceiver rx{broadcast_buffer_};

    constexpr i64 CLIENT_ID = 42;

    ClientProxy proxy{tx};
    proxy.on_counter_ready(500, 42);

    MockClientConductor conductor;
    DriverEventsAdapter adapter{rx, conductor, CLIENT_ID};
    adapter.receive(500);

    ASSERT_EQ(conductor.events.size(), 1u);
    EXPECT_EQ(conductor.events[0].type, ON_COUNTER_READY);
    EXPECT_EQ(conductor.events[0].correlation_id, 500);
    EXPECT_EQ(conductor.events[0].counter_id, 42);
}

TEST_F(CncTest, OnUnavailableCounterRoundTrip)
{
    BroadcastTransmitter tx{broadcast_buffer_};
    BroadcastReceiver rx{broadcast_buffer_};

    constexpr i64 CLIENT_ID = 42;

    ClientProxy proxy{tx};
    proxy.on_unavailable_counter(600, 55);

    MockClientConductor conductor;
    DriverEventsAdapter adapter{rx, conductor, CLIENT_ID};
    adapter.receive(-1);

    ASSERT_EQ(conductor.events.size(), 1u);
    EXPECT_EQ(conductor.events[0].type, ON_UNAVAILABLE_COUNTER);
    EXPECT_EQ(conductor.events[0].correlation_id, 600);
    EXPECT_EQ(conductor.events[0].counter_id, 55);
}

TEST_F(CncTest, OnClientTimeoutRoundTrip)
{
    BroadcastTransmitter tx{broadcast_buffer_};
    BroadcastReceiver rx{broadcast_buffer_};

    constexpr i64 CLIENT_ID = 42;

    ClientProxy proxy{tx};
    proxy.on_client_timeout(CLIENT_ID);

    MockClientConductor conductor;
    DriverEventsAdapter adapter{rx, conductor, CLIENT_ID};
    adapter.receive(-1);

    ASSERT_EQ(conductor.events.size(), 1u);
    EXPECT_EQ(conductor.events[0].type, ON_CLIENT_TIMEOUT);
    EXPECT_EQ(conductor.events[0].client_id, CLIENT_ID);
}

TEST_F(CncTest, OnClientTimeoutFilteredByClientId)
{
    BroadcastTransmitter tx{broadcast_buffer_};
    BroadcastReceiver rx{broadcast_buffer_};

    constexpr i64 CLIENT_ID = 42;

    ClientProxy proxy{tx};
    proxy.on_client_timeout(999); // different client id

    MockClientConductor conductor;
    DriverEventsAdapter adapter{rx, conductor, CLIENT_ID};
    adapter.receive(-1);

    // Should be filtered out — the timeout is for a different client.
    EXPECT_EQ(conductor.events.size(), 0u);
}

TEST_F(CncTest, OnStaticCounterRoundTrip)
{
    BroadcastTransmitter tx{broadcast_buffer_};
    BroadcastReceiver rx{broadcast_buffer_};

    constexpr i64 CLIENT_ID = 42;

    ClientProxy proxy{tx};
    proxy.on_static_counter(700, 88);

    MockClientConductor conductor;
    DriverEventsAdapter adapter{rx, conductor, CLIENT_ID};
    adapter.receive(700);

    ASSERT_EQ(conductor.events.size(), 1u);
    EXPECT_EQ(conductor.events[0].type, ON_STATIC_COUNTER);
    EXPECT_EQ(conductor.events[0].correlation_id, 700);
    EXPECT_EQ(conductor.events[0].counter_id, 88);
}

TEST_F(CncTest, OnNextAvailableSessionIdRoundTrip)
{
    BroadcastTransmitter tx{broadcast_buffer_};
    BroadcastReceiver rx{broadcast_buffer_};

    constexpr i64 CLIENT_ID = 42;

    ClientProxy proxy{tx};
    proxy.on_next_available_session_id(800, 12345);

    MockClientConductor conductor;
    DriverEventsAdapter adapter{rx, conductor, CLIENT_ID};
    adapter.receive(800);

    ASSERT_EQ(conductor.events.size(), 1u);
    EXPECT_EQ(conductor.events[0].type, ON_NEXT_AVAILABLE_SESSION_ID);
    EXPECT_EQ(conductor.events[0].correlation_id, 800);
    EXPECT_EQ(conductor.events[0].session_id, 12345);
}

TEST_F(CncTest, OnPublicationErrorFrameRoundTrip)
{
    BroadcastTransmitter tx{broadcast_buffer_};
    BroadcastReceiver rx{broadcast_buffer_};

    constexpr i64 CLIENT_ID = 42;

    const u8 addr[16] = {127, 0, 0, 1};

    ClientProxy proxy{tx};
    proxy.on_publication_error_frame(
        900,  // registration_id
        800,  // destination_registration_id
        42,   // session_id
        1001, // stream_id
        500,  // receiver_id
        600,  // group_tag
        1,    // address_type
        4045, // udp_port
        addr, // address
        1,    // error_code (ERROR_CODE_GENERIC)
        "publication error message");

    MockClientConductor conductor;
    DriverEventsAdapter adapter{rx, conductor, CLIENT_ID};
    adapter.receive(-1);

    ASSERT_EQ(conductor.events.size(), 1u);
    EXPECT_EQ(conductor.events[0].type, ON_PUBLICATION_ERROR);
    EXPECT_EQ(conductor.events[0].registration_id, 900);
    EXPECT_EQ(conductor.events[0].destination_registration_id, 800);
    EXPECT_EQ(conductor.events[0].session_id, 42);
    EXPECT_EQ(conductor.events[0].stream_id, 1001);
    EXPECT_EQ(conductor.events[0].receiver_id, 500);
    EXPECT_EQ(conductor.events[0].group_tag, 600);
    EXPECT_EQ(conductor.events[0].address_type, 1);
    EXPECT_EQ(conductor.events[0].udp_port, 4045);
    EXPECT_EQ(conductor.events[0].error_code, 1);
    EXPECT_EQ(conductor.events[0].error_message, "publication error message");
}

TEST_F(CncTest, OnUnavailableImageRoundTrip)
{
    BroadcastTransmitter tx{broadcast_buffer_};
    BroadcastReceiver rx{broadcast_buffer_};

    constexpr i64 CLIENT_ID = 42;

    ClientProxy proxy{tx};
    proxy.on_unavailable_image(1000, 2000);

    MockClientConductor conductor;
    DriverEventsAdapter adapter{rx, conductor, CLIENT_ID};
    adapter.receive(-1);

    ASSERT_EQ(conductor.events.size(), 1u);
    EXPECT_EQ(conductor.events[0].type, ON_UNAVAILABLE_IMAGE);
    EXPECT_EQ(conductor.events[0].correlation_id, 1000);
    EXPECT_EQ(conductor.events[0].registration_id, 2000);
}

TEST_F(CncTest, MultipleEventsInSequence)
{
    BroadcastTransmitter tx{broadcast_buffer_};
    BroadcastReceiver rx{broadcast_buffer_};

    constexpr i64 CLIENT_ID = 42;

    ClientProxy proxy{tx};
    // Use events that are always dispatched (not filtered by active_correlation_id).
    proxy.on_unavailable_image(100, 200);
    proxy.on_unavailable_counter(300, 55);
    proxy.on_client_timeout(CLIENT_ID);
    proxy.on_publication_error_frame(400, 0, 0, 0, 0, 0, 0, 0, nullptr, 0, "");

    MockClientConductor conductor;
    DriverEventsAdapter adapter{rx, conductor, CLIENT_ID};
    const i32 count = adapter.receive(-1); // no active correlation id

    // All 4 events should be received and dispatched.
    EXPECT_EQ(count, 4);
    ASSERT_EQ(conductor.events.size(), 4u);
    EXPECT_EQ(conductor.events[0].type, ON_UNAVAILABLE_IMAGE);
    EXPECT_EQ(conductor.events[1].type, ON_UNAVAILABLE_COUNTER);
    EXPECT_EQ(conductor.events[2].type, ON_CLIENT_TIMEOUT);
    EXPECT_EQ(conductor.events[3].type, ON_PUBLICATION_ERROR);
}

// ===========================================================================
// Full round-trip integration test
// ===========================================================================

TEST_F(CncTest, FullRoundTripAddPublicationAndRespond)
{
    // Simulate the full flow:
    // 1. Client writes add_publication command via DriverProxy
    // 2. Driver reads command via ClientCommandAdapter
    // 3. Driver writes publication_ready response via ClientProxy
    // 4. Client reads response via DriverEventsAdapter

    ManyToOneRingBuffer ring{ring_buffer_};
    BroadcastTransmitter tx{broadcast_buffer_};
    BroadcastReceiver rx{broadcast_buffer_};

    constexpr i64 CLIENT_ID = 42;

    // Step 1: Client sends add_publication
    DriverProxy driver_proxy{ring, CLIENT_ID};
    const i64 corr_id = driver_proxy.add_publication("aeron:ipc", 1001);

    // Step 2: Driver reads and dispatches the command
    MockDriverConductor conductor;
    ClientCommandAdapter cmd_adapter{ring, conductor};
    const i32 cmd_count = cmd_adapter.receive();
    EXPECT_EQ(cmd_count, 1);
    ASSERT_EQ(conductor.commands.size(), 1u);
    EXPECT_EQ(conductor.commands[0].type, ADD_PUBLICATION);
    EXPECT_EQ(conductor.commands[0].correlation_id, corr_id);

    // Step 3: Driver sends publication_ready response
    ClientProxy client_proxy{tx};
    client_proxy.on_publication_ready(corr_id, 500, 1001, 42, "/tmp/test.log", 5, 6, false);

    // Step 4: Client reads the response
    MockClientConductor client_conductor;
    DriverEventsAdapter evt_adapter{rx, client_conductor, CLIENT_ID};
    const i32 evt_count = evt_adapter.receive(corr_id);
    EXPECT_EQ(evt_count, 1);
    ASSERT_EQ(client_conductor.events.size(), 1u);
    EXPECT_EQ(client_conductor.events[0].type, ON_PUBLICATION_READY);
    EXPECT_EQ(client_conductor.events[0].correlation_id, corr_id);
    EXPECT_EQ(client_conductor.events[0].stream_id, 1001);
    EXPECT_EQ(client_conductor.events[0].log_file_name, "/tmp/test.log");
    EXPECT_EQ(evt_adapter.received_correlation_id(), corr_id);
}

TEST_F(CncTest, FullRoundTripAddSubscriptionAndRespond)
{
    ManyToOneRingBuffer ring{ring_buffer_};
    BroadcastTransmitter tx{broadcast_buffer_};
    BroadcastReceiver rx{broadcast_buffer_};

    constexpr i64 CLIENT_ID = 42;

    // Client sends add_subscription
    DriverProxy driver_proxy{ring, CLIENT_ID};
    const i64 corr_id = driver_proxy.add_subscription("aeron:udp?endpoint=localhost:40456", 1001);

    // Driver reads and dispatches
    MockDriverConductor conductor;
    ClientCommandAdapter cmd_adapter{ring, conductor};
    cmd_adapter.receive();
    ASSERT_EQ(conductor.commands.size(), 1u);
    EXPECT_EQ(conductor.commands[0].type, ADD_SUBSCRIPTION);
    EXPECT_EQ(conductor.commands[0].correlation_id, corr_id);

    // Driver sends subscription_ready
    ClientProxy client_proxy{tx};
    client_proxy.on_subscription_ready(corr_id, 10);

    // Client reads the response
    MockClientConductor client_conductor;
    DriverEventsAdapter evt_adapter{rx, client_conductor, CLIENT_ID};
    evt_adapter.receive(corr_id);
    ASSERT_EQ(client_conductor.events.size(), 1u);
    EXPECT_EQ(client_conductor.events[0].type, ON_SUBSCRIPTION_READY);
    EXPECT_EQ(client_conductor.events[0].correlation_id, corr_id);
    EXPECT_EQ(client_conductor.events[0].channel_status_counter_id, 10);
}

TEST_F(CncTest, FullRoundTripError)
{
    ManyToOneRingBuffer ring{ring_buffer_};
    BroadcastTransmitter tx{broadcast_buffer_};
    BroadcastReceiver rx{broadcast_buffer_};

    constexpr i64 CLIENT_ID = 42;

    // Client sends add_publication
    DriverProxy driver_proxy{ring, CLIENT_ID};
    const i64 corr_id = driver_proxy.add_publication("aeron:ipc", 1001);

    // Driver reads the command
    MockDriverConductor conductor;
    ClientCommandAdapter cmd_adapter{ring, conductor};
    cmd_adapter.receive();

    // Driver sends error response
    ClientProxy client_proxy{tx};
    client_proxy.on_error(corr_id, ERROR_CODE_GENERIC, "publication failed");

    // Client reads the error
    MockClientConductor client_conductor;
    DriverEventsAdapter evt_adapter{rx, client_conductor, CLIENT_ID};
    evt_adapter.receive(corr_id);
    ASSERT_EQ(client_conductor.events.size(), 1u);
    EXPECT_EQ(client_conductor.events[0].type, ON_ERROR);
    EXPECT_EQ(client_conductor.events[0].correlation_id, corr_id);
    EXPECT_EQ(client_conductor.events[0].error_code, ERROR_CODE_GENERIC);
    EXPECT_EQ(client_conductor.events[0].error_message, "publication failed");
}

TEST_F(CncTest, FullRoundTripCounterLifecycle)
{
    ManyToOneRingBuffer ring{ring_buffer_};
    BroadcastTransmitter tx{broadcast_buffer_};
    BroadcastReceiver rx{broadcast_buffer_};

    constexpr i64 CLIENT_ID = 42;

    DriverProxy driver_proxy{ring, CLIENT_ID};
    ClientProxy client_proxy{tx};
    MockDriverConductor driver_conductor;
    MockClientConductor client_conductor;
    ClientCommandAdapter cmd_adapter{ring, driver_conductor};
    DriverEventsAdapter evt_adapter{rx, client_conductor, CLIENT_ID};

    // Add counter
    const i64 add_corr = driver_proxy.add_counter(10, "my counter");
    cmd_adapter.receive();
    ASSERT_EQ(driver_conductor.commands.size(), 1u);
    EXPECT_EQ(driver_conductor.commands[0].type, ADD_COUNTER);

    client_proxy.on_counter_ready(add_corr, 42);
    evt_adapter.receive(add_corr);
    ASSERT_EQ(client_conductor.events.size(), 1u);
    EXPECT_EQ(client_conductor.events[0].type, ON_COUNTER_READY);
    EXPECT_EQ(client_conductor.events[0].counter_id, 42);

    // Remove counter
    driver_conductor.commands.clear();
    client_conductor.events.clear();

    const i64 remove_corr = driver_proxy.remove_counter(999);
    cmd_adapter.receive();
    ASSERT_EQ(driver_conductor.commands.size(), 1u);
    EXPECT_EQ(driver_conductor.commands[0].type, REMOVE_COUNTER);
    EXPECT_EQ(driver_conductor.commands[0].registration_id, 999);

    client_proxy.operation_succeeded(remove_corr);
    evt_adapter.receive(remove_corr);
    ASSERT_EQ(client_conductor.events.size(), 1u);
    EXPECT_EQ(client_conductor.events[0].type, ON_OPERATION_SUCCESS);
}

// ===========================================================================
// Additional tests from Phase 5 review findings
// ===========================================================================

TEST_F(CncTest, AddStaticCounterRoundTrip)
{
    ManyToOneRingBuffer ring{ring_buffer_};
    constexpr i64 CLIENT_ID = 42;

    DriverProxy proxy{ring, CLIENT_ID};
    const i64 corr_id = proxy.add_static_counter(10, "static counter", 777);

    MockDriverConductor conductor;
    ClientCommandAdapter adapter{ring, conductor};
    adapter.receive();

    ASSERT_EQ(conductor.commands.size(), 1u);
    EXPECT_EQ(conductor.commands[0].type, ADD_STATIC_COUNTER);
    EXPECT_EQ(conductor.commands[0].correlation_id, corr_id);
    EXPECT_EQ(conductor.commands[0].client_id, CLIENT_ID);
    EXPECT_EQ(conductor.commands[0].registration_id, 777);
    EXPECT_EQ(conductor.commands[0].counter_type, 10);
    EXPECT_EQ(conductor.commands[0].channel, "static counter");
}

TEST_F(CncTest, OnAvailableImageRoundTrip)
{
    BroadcastTransmitter tx{broadcast_buffer_};
    BroadcastReceiver rx{broadcast_buffer_};

    constexpr i64 CLIENT_ID = 42;

    ClientProxy proxy{tx};
    proxy.on_available_image(1000, 1001, 42, 2000, 55, "/tmp/image.log", "localhost:40456");

    MockClientConductor conductor;
    DriverEventsAdapter adapter{rx, conductor, CLIENT_ID};
    adapter.receive(-1);

    ASSERT_EQ(conductor.events.size(), 1u);
    EXPECT_EQ(conductor.events[0].type, ON_AVAILABLE_IMAGE);
    EXPECT_EQ(conductor.events[0].correlation_id, 1000);
    EXPECT_EQ(conductor.events[0].registration_id, 2000);
    EXPECT_EQ(conductor.events[0].session_id, 42);
    EXPECT_EQ(conductor.events[0].position_counter_id, 55);
    EXPECT_EQ(conductor.events[0].log_file_name, "/tmp/image.log");
    EXPECT_EQ(conductor.events[0].source_identity, "localhost:40456");
}

TEST_F(CncTest, UnknownCommandErrorCallback)
{
    ManyToOneRingBuffer ring{ring_buffer_};

    // Write a message with an unknown type directly to the ring buffer.
    // We'll use type 0xFF which is not a valid command.
    std::byte msg[16]{};
    ring.write(0xFF, msg, sizeof(msg));

    bool error_called = false;
    i32 received_error_code = 0;
    std::string received_message;

    MockDriverConductor conductor;
    ClientCommandAdapter adapter{ring, conductor,
        [&](i64 /*correlation_id*/, i32 error_code, std::string_view message) {
            error_called = true;
            received_error_code = error_code;
            received_message = std::string(message);
        }};

    const i32 count = adapter.receive();
    EXPECT_EQ(count, 1);
    EXPECT_TRUE(error_called);
    EXPECT_EQ(received_error_code, ERROR_CODE_UNKNOWN_COMMAND);
    EXPECT_NE(received_message.find("unknown command typeId="), std::string::npos);
}

// ===========================================================================
// Wire format tests — verify raw byte layouts match Java expectations
// ===========================================================================

TEST_F(CncTest, RemovePublicationWireFormat)
{
    // Java layout: [0] i64 client_id, [8] i64 correlation_id, [16] i64 registration_id, [24] i64 flags
    std::byte buf[REMOVE_PUBLICATION_LENGTH]{};
    concurrent::UnsafeBuffer buffer{buf, REMOVE_PUBLICATION_LENGTH};
    command::RemovePublicationFlyweight fw{buffer, 0};

    fw.set_client_id(0x0102030405060708LL);
    fw.set_correlation_id(0x1112131415161718LL);
    fw.set_registration_id(0x2122232425262728LL);
    fw.set_revoke(true);

    EXPECT_EQ(fw.client_id(), 0x0102030405060708LL);
    EXPECT_EQ(fw.correlation_id(), 0x1112131415161718LL);
    EXPECT_EQ(fw.registration_id(), 0x2122232425262728LL);
    EXPECT_TRUE(fw.revoke());

    // Verify field offsets
    i64 val;
    std::memcpy(&val, buf + 0, 8);
    EXPECT_EQ(val, 0x0102030405060708LL);
    std::memcpy(&val, buf + 8, 8);
    EXPECT_EQ(val, 0x1112131415161718LL);
    std::memcpy(&val, buf + 16, 8);
    EXPECT_EQ(val, 0x2122232425262728LL);
    std::memcpy(&val, buf + 24, 8);
    EXPECT_EQ(val & 1, 1);

    EXPECT_EQ(REMOVE_PUBLICATION_LENGTH, 32);
}

TEST_F(CncTest, RemoveSubscriptionWireFormat)
{
    // Java layout: [0] i64 client_id, [8] i64 correlation_id, [16] i64 registration_id
    std::byte buf[REMOVE_SUBSCRIPTION_LENGTH]{};
    concurrent::UnsafeBuffer buffer{buf, REMOVE_SUBSCRIPTION_LENGTH};
    command::RemoveSubscriptionFlyweight fw{buffer, 0};

    fw.set_client_id(0x0102030405060708LL);
    fw.set_correlation_id(0x1112131415161718LL);
    fw.set_registration_id(0x2122232425262728LL);

    EXPECT_EQ(fw.client_id(), 0x0102030405060708LL);
    EXPECT_EQ(fw.correlation_id(), 0x1112131415161718LL);
    EXPECT_EQ(fw.registration_id(), 0x2122232425262728LL);

    i64 val;
    std::memcpy(&val, buf + 0, 8);
    EXPECT_EQ(val, 0x0102030405060708LL);
    std::memcpy(&val, buf + 8, 8);
    EXPECT_EQ(val, 0x1112131415161718LL);
    std::memcpy(&val, buf + 16, 8);
    EXPECT_EQ(val, 0x2122232425262728LL);

    EXPECT_EQ(REMOVE_SUBSCRIPTION_LENGTH, 24);
}

TEST_F(CncTest, SubscriptionMessageWireFormat)
{
    // Java layout: [0] i64 client_id, [8] i64 correlation_id, [16] i64 registration_correlation_id,
    //              [24] i32 stream_id, [28] i32 channel_length, [32] channel
    const char* channel = "aeron:ipc";
    const i32 channel_len = static_cast<i32>(strlen(channel));
    const i32 msg_len = command::SubscriptionMessageFlyweight::compute_length(channel_len);

    std::byte buf[128]{};
    concurrent::UnsafeBuffer buffer{buf, msg_len};
    command::SubscriptionMessageFlyweight fw{buffer, 0};

    fw.set_client_id(42);
    fw.set_correlation_id(100);
    fw.set_registration_correlation_id(-1);
    fw.set_stream_id(1001);
    fw.set_channel(channel, channel_len);

    EXPECT_EQ(fw.client_id(), 42);
    EXPECT_EQ(fw.correlation_id(), 100);
    EXPECT_EQ(fw.registration_correlation_id(), -1);
    EXPECT_EQ(fw.stream_id(), 1001);
    EXPECT_EQ(fw.channel_length(), channel_len);

    // Verify fixed offsets
    i64 i64_val;
    i32 i32_val;
    std::memcpy(&i64_val, buf + 0, 8);
    EXPECT_EQ(i64_val, 42);
    std::memcpy(&i64_val, buf + 8, 8);
    EXPECT_EQ(i64_val, 100);
    std::memcpy(&i64_val, buf + 16, 8);
    EXPECT_EQ(i64_val, -1);
    std::memcpy(&i32_val, buf + 24, 4);
    EXPECT_EQ(i32_val, 1001);
    std::memcpy(&i32_val, buf + 28, 4);
    EXPECT_EQ(i32_val, channel_len);
    EXPECT_EQ(memcmp(buf + 32, channel, channel_len), 0);

    EXPECT_EQ(SUBSCRIPTION_MSG_MINIMUM_LENGTH, 32);
}

TEST_F(CncTest, TerminateDriverWireFormat)
{
    // Java layout: [0] i64 client_id, [8] i64 correlation_id, [16] i32 token_buffer_length, [20] token_buffer
    const char* token = "auth-token";
    const i32 token_len = static_cast<i32>(strlen(token));
    const i32 msg_len = command::TerminateDriverFlyweight::compute_length(token_len);

    std::byte buf[128]{};
    concurrent::UnsafeBuffer buffer{buf, msg_len};
    command::TerminateDriverFlyweight fw{buffer, 0};

    fw.set_client_id(42);
    fw.set_correlation_id(-1);
    fw.set_token_buffer(token, token_len);

    EXPECT_EQ(fw.client_id(), 42);
    EXPECT_EQ(fw.correlation_id(), -1);
    EXPECT_EQ(fw.token_buffer_length(), token_len);

    i64 i64_val;
    i32 i32_val;
    std::memcpy(&i64_val, buf + 0, 8);
    EXPECT_EQ(i64_val, 42);
    std::memcpy(&i64_val, buf + 8, 8);
    EXPECT_EQ(i64_val, -1);
    std::memcpy(&i32_val, buf + 16, 4);
    EXPECT_EQ(i32_val, token_len);
    EXPECT_EQ(memcmp(buf + 20, token, token_len), 0);

    EXPECT_EQ(msg_len, 20 + token_len);
}

TEST_F(CncTest, PublicationErrorFrameWireFormat)
{
    // Java layout: [0] i64 registration_id, [8] i64 destination_registration_id,
    //              [16] i32 session_id, [20] i32 stream_id, [24] i64 receiver_id,
    //              [32] i64 group_tag, [40] i16 address_type, [42] i16 udp_port,
    //              [44] u8[16] address, [60] i32 error_code, [64] i32 error_message_length,
    //              [68] error_message
    const char* err_msg = "test error";
    const i32 err_len = static_cast<i32>(strlen(err_msg));
    const i32 msg_len = command::PublicationErrorFrameFlyweight::compute_length(err_len);

    std::byte buf[256]{};
    concurrent::UnsafeBuffer buffer{buf, msg_len};
    command::PublicationErrorFrameFlyweight fw{buffer, 0};

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
    fw.set_error_message(err_msg, err_len);

    EXPECT_EQ(fw.registration_id(), 100);
    EXPECT_EQ(fw.destination_registration_id(), 200);
    EXPECT_EQ(fw.session_id(), 42);
    EXPECT_EQ(fw.stream_id(), 1001);
    EXPECT_EQ(fw.receiver_id(), 500);
    EXPECT_EQ(fw.group_tag(), 600);
    EXPECT_EQ(fw.address_type(), 1);
    EXPECT_EQ(fw.udp_port(), 4045);
    EXPECT_EQ(fw.error_code(), 2);
    EXPECT_EQ(fw.error_message_length(), err_len);

    // Verify offsets
    i64 i64_val;
    i32 i32_val;
    i16 i16_val;
    std::memcpy(&i64_val, buf + 0, 8);
    EXPECT_EQ(i64_val, 100);
    std::memcpy(&i64_val, buf + 8, 8);
    EXPECT_EQ(i64_val, 200);
    std::memcpy(&i32_val, buf + 16, 4);
    EXPECT_EQ(i32_val, 42);
    std::memcpy(&i32_val, buf + 20, 4);
    EXPECT_EQ(i32_val, 1001);
    std::memcpy(&i64_val, buf + 24, 8);
    EXPECT_EQ(i64_val, 500);
    std::memcpy(&i64_val, buf + 32, 8);
    EXPECT_EQ(i64_val, 600);
    std::memcpy(&i16_val, buf + 40, 2);
    EXPECT_EQ(i16_val, 1);
    std::memcpy(&i16_val, buf + 42, 2);
    EXPECT_EQ(i16_val, 4045);
    EXPECT_EQ(memcmp(buf + 44, addr, 16), 0);
    std::memcpy(&i32_val, buf + 60, 4);
    EXPECT_EQ(i32_val, 2);
    std::memcpy(&i32_val, buf + 64, 4);
    EXPECT_EQ(i32_val, err_len);
    EXPECT_EQ(memcmp(buf + 68, err_msg, err_len), 0);

    EXPECT_EQ(PUBLICATION_ERROR_FRAME_LENGTH, 68);
}

TEST_F(CncTest, ImageMessageWireFormat)
{
    // Java layout: [0] i64 correlation_id, [8] i64 subscription_registration_id,
    //              [16] i32 stream_id, [20] channel (variable)
    std::byte buf[64]{};
    concurrent::UnsafeBuffer buffer{buf, IMAGE_MESSAGE_MINIMUM_LENGTH};
    command::ImageMessageFlyweight fw{buffer, 0};

    fw.set_correlation_id(1000);
    fw.set_subscription_registration_id(2000);
    fw.set_stream_id(1001);

    EXPECT_EQ(fw.correlation_id(), 1000);
    EXPECT_EQ(fw.subscription_registration_id(), 2000);
    EXPECT_EQ(fw.stream_id(), 1001);

    i64 i64_val;
    i32 i32_val;
    std::memcpy(&i64_val, buf + 0, 8);
    EXPECT_EQ(i64_val, 1000);
    std::memcpy(&i64_val, buf + 8, 8);
    EXPECT_EQ(i64_val, 2000);
    std::memcpy(&i32_val, buf + 16, 4);
    EXPECT_EQ(i32_val, 1001);

    EXPECT_EQ(IMAGE_MESSAGE_MINIMUM_LENGTH, 20);
}

TEST_F(CncTest, ImageBuffersReadyWireFormat)
{
    // Java layout: [0] i64 correlation_id, [8] i32 session_id, [12] i32 stream_id,
    //              [16] i64 subscription_registration_id, [24] i32 subscriber_position_id,
    //              [28] i32 log_file_name_length, [32] log_file_name,
    //              [32+log_len] i32 source_identity_length, [+4] source_identity
    const char* log_name = "/tmp/test.log";
    const char* src_id = "localhost:40456";
    const i32 log_len = static_cast<i32>(strlen(log_name));
    const i32 src_len = static_cast<i32>(strlen(src_id));
    const i32 msg_len = command::ImageBuffersReadyFlyweight::compute_length(log_len, src_len);

    std::byte buf[256]{};
    concurrent::UnsafeBuffer buffer{buf, msg_len};
    command::ImageBuffersReadyFlyweight fw{buffer, 0};

    fw.set_correlation_id(1000);
    fw.set_session_id(42);
    fw.set_stream_id(1001);
    fw.set_subscription_registration_id(2000);
    fw.set_subscriber_position_id(55);
    fw.set_log_file_name(log_name, log_len);
    fw.set_source_identity(src_id, src_len);

    EXPECT_EQ(fw.correlation_id(), 1000);
    EXPECT_EQ(fw.session_id(), 42);
    EXPECT_EQ(fw.stream_id(), 1001);
    EXPECT_EQ(fw.subscription_registration_id(), 2000);
    EXPECT_EQ(fw.subscriber_position_id(), 55);
    EXPECT_EQ(fw.log_file_name_length(), log_len);
    EXPECT_EQ(fw.source_identity_length(), src_len);

    // Verify offsets
    i64 i64_val;
    i32 i32_val;
    std::memcpy(&i64_val, buf + 0, 8);
    EXPECT_EQ(i64_val, 1000);
    std::memcpy(&i32_val, buf + 8, 4);
    EXPECT_EQ(i32_val, 42);
    std::memcpy(&i32_val, buf + 12, 4);
    EXPECT_EQ(i32_val, 1001);
    std::memcpy(&i64_val, buf + 16, 8);
    EXPECT_EQ(i64_val, 2000);
    std::memcpy(&i32_val, buf + 24, 4);
    EXPECT_EQ(i32_val, 55);
    std::memcpy(&i32_val, buf + 28, 4);
    EXPECT_EQ(i32_val, log_len);
    EXPECT_EQ(memcmp(buf + 32, log_name, log_len), 0);
    std::memcpy(&i32_val, buf + 32 + log_len, 4);
    EXPECT_EQ(i32_val, src_len);
    EXPECT_EQ(memcmp(buf + 32 + log_len + 4, src_id, src_len), 0);

    EXPECT_EQ(msg_len, 32 + log_len + 4 + src_len);
}

// ===========================================================================
// Malformed-command tests (Phase 6 review findings)
// ===========================================================================

TEST_F(CncTest, TruncatedAddPublication)
{
    ManyToOneRingBuffer ring{ring_buffer_};

    // Write a message that is too short for ADD_PUBLICATION (needs 24 bytes)
    std::byte msg[10]{};
    ring.write(ADD_PUBLICATION, msg, sizeof(msg));

    std::vector<std::string> errors;
    MockDriverConductor conductor;
    ClientCommandAdapter adapter{ring, conductor,
        [&](i64, i32, std::string_view message) {
            errors.push_back(std::string(message));
        }};

    adapter.receive();
    EXPECT_EQ(conductor.commands.size(), 0u);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_NE(errors[0].find("too short"), std::string::npos);
}

TEST_F(CncTest, NegativeChannelLengthAddPublication)
{
    ManyToOneRingBuffer ring{ring_buffer_};

    // Build a valid-length message but with negative channel_length
    std::byte msg[PUBLICATION_MSG_MINIMUM_LENGTH]{};
    UnsafeBuffer buffer{msg, sizeof(msg)};
    PublicationMessageFlyweight fw{buffer, 0};
    fw.set_client_id(42);
    fw.set_correlation_id(100);
    fw.set_stream_id(1001);
    fw.set_channel_length(-1);  // negative!

    ring.write(ADD_PUBLICATION, msg, sizeof(msg));

    std::vector<std::string> errors;
    MockDriverConductor conductor;
    ClientCommandAdapter adapter{ring, conductor,
        [&](i64, i32, std::string_view message) {
            errors.push_back(std::string(message));
        }};

    adapter.receive();
    EXPECT_EQ(conductor.commands.size(), 0u);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_NE(errors[0].find("invalid channel"), std::string::npos);
}

TEST_F(CncTest, OverflowedChannelLengthAddSubscription)
{
    ManyToOneRingBuffer ring{ring_buffer_};

    // Build a message with channel_length that overflows the message
    std::byte msg[SUBSCRIPTION_MSG_MINIMUM_LENGTH]{};
    UnsafeBuffer buffer{msg, sizeof(msg)};
    SubscriptionMessageFlyweight fw{buffer, 0};
    fw.set_client_id(42);
    fw.set_correlation_id(100);
    fw.set_stream_id(1001);
    fw.set_channel_length(1000);  // way too large for the message

    ring.write(ADD_SUBSCRIPTION, msg, sizeof(msg));

    std::vector<std::string> errors;
    MockDriverConductor conductor;
    ClientCommandAdapter adapter{ring, conductor,
        [&](i64, i32, std::string_view message) {
            errors.push_back(std::string(message));
        }};

    adapter.receive();
    EXPECT_EQ(conductor.commands.size(), 0u);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_NE(errors[0].find("invalid channel"), std::string::npos);
}

TEST_F(CncTest, EmptyRejectReason)
{
    ManyToOneRingBuffer ring{ring_buffer_};
    constexpr i64 CLIENT_ID = 42;

    // Build a REJECT_IMAGE message with reason_length = 0
    std::byte msg[command::REJECT_IMAGE_MINIMUM_SIZE]{};
    concurrent::UnsafeBuffer buffer{msg, sizeof(msg)};
    command::RejectImageFlyweight fw{buffer, 0};
    fw.set_client_id(CLIENT_ID);
    fw.set_correlation_id(100);
    fw.set_image_correlation_id(5555);
    fw.set_position(0);
    fw.set_reason_length(0);

    ring.write(REJECT_IMAGE, msg, sizeof(msg));

    MockDriverConductor conductor;
    ClientCommandAdapter adapter{ring, conductor};
    adapter.receive();

    // Empty reason is valid -- should be dispatched
    ASSERT_EQ(conductor.commands.size(), 1u);
    EXPECT_EQ(conductor.commands[0].type, REJECT_IMAGE);
    EXPECT_EQ(conductor.commands[0].reason, "");
}

TEST_F(CncTest, LegacyRemovePublication24Bytes)
{
    ManyToOneRingBuffer ring{ring_buffer_};
    constexpr i64 CLIENT_ID = 42;

    // Simulate a legacy client that sends 24-byte RemovePublication (no flags field)
    std::byte msg[REMOVE_PUBLICATION_LEGACY_LENGTH]{};
    concurrent::UnsafeBuffer buffer{msg, sizeof(msg)};
    command::RemovePublicationFlyweight fw{buffer, 0};
    fw.set_client_id(CLIENT_ID);
    fw.set_correlation_id(100);
    fw.set_registration_id(12345);
    // No flags field -- only 24 bytes total

    ring.write(REMOVE_PUBLICATION, msg, sizeof(msg));

    MockDriverConductor conductor;
    ClientCommandAdapter adapter{ring, conductor};
    adapter.receive();

    // Should be accepted and dispatched
    ASSERT_EQ(conductor.commands.size(), 1u);
    EXPECT_EQ(conductor.commands[0].type, REMOVE_PUBLICATION);
    EXPECT_EQ(conductor.commands[0].registration_id, 12345);
    EXPECT_FALSE(conductor.commands[0].revoke);  // no flags = revoke is false
}

TEST_F(CncTest, NegativeTokenLengthTerminateDriver)
{
    ManyToOneRingBuffer ring{ring_buffer_};

    // Build a TERMINATE_DRIVER message with negative token_buffer_length
    std::byte msg[TerminateDriverFlyweight::TOKEN_OFFSET + 4]{};
    concurrent::UnsafeBuffer buffer{msg, sizeof(msg)};
    command::TerminateDriverFlyweight fw{buffer, 0};
    fw.set_client_id(42);
    fw.set_correlation_id(-1);
    fw.set_token_buffer_length(-1);  // negative!

    ring.write(TERMINATE_DRIVER, msg, sizeof(msg));

    std::vector<std::string> errors;
    MockDriverConductor conductor;
    ClientCommandAdapter adapter{ring, conductor,
        [&](i64, i32, std::string_view message) {
            errors.push_back(std::string(message));
        }};

    adapter.receive();
    EXPECT_EQ(conductor.commands.size(), 0u);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_NE(errors[0].find("token"), std::string::npos);
}

TEST_F(CncTest, NegativeKeyLengthAddCounter)
{
    ManyToOneRingBuffer ring{ring_buffer_};

    // Build an ADD_COUNTER message with negative key_buffer_length
    std::byte msg[CounterMessageFlyweight::KEY_OFFSET + 8]{};
    concurrent::UnsafeBuffer buffer{msg, sizeof(msg)};
    command::CounterMessageFlyweight fw{buffer, 0};
    fw.set_client_id(42);
    fw.set_correlation_id(100);
    fw.set_counter_type(10);
    fw.set_key_buffer_length(-1);  // negative!

    ring.write(ADD_COUNTER, msg, sizeof(msg));

    std::vector<std::string> errors;
    MockDriverConductor conductor;
    ClientCommandAdapter adapter{ring, conductor,
        [&](i64, i32, std::string_view message) {
            errors.push_back(std::string(message));
        }};

    adapter.receive();
    EXPECT_EQ(conductor.commands.size(), 0u);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_NE(errors[0].find("key"), std::string::npos);
}

TEST_F(CncTest, NegativeKeyLengthAddStaticCounter)
{
    ManyToOneRingBuffer ring{ring_buffer_};

    // Build an ADD_STATIC_COUNTER message with negative key_length
    std::byte msg[STATIC_COUNTER_MSG_MINIMUM_LENGTH]{};
    concurrent::UnsafeBuffer buffer{msg, sizeof(msg)};
    command::StaticCounterMessageFlyweight fw{buffer, 0};
    fw.set_client_id(42);
    fw.set_correlation_id(100);
    fw.set_registration_id(777);
    fw.set_counter_type_id(10);
    fw.set_key_length(-1);  // negative!

    ring.write(ADD_STATIC_COUNTER, msg, sizeof(msg));

    std::vector<std::string> errors;
    MockDriverConductor conductor;
    ClientCommandAdapter adapter{ring, conductor,
        [&](i64, i32, std::string_view message) {
            errors.push_back(std::string(message));
        }};

    adapter.receive();
    EXPECT_EQ(conductor.commands.size(), 0u);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_NE(errors[0].find("key"), std::string::npos);
}

TEST_F(CncTest, OversizedPublicationReady)
{
    // Verify that a very long log_file_name in on_publication_ready throws
    // when it exceeds the scratch buffer size.
    BroadcastTransmitter tx{broadcast_buffer_};
    ClientProxy proxy{tx};

    // Create a string that exceeds SCRATCH_BUFFER_SIZE (2048)
    std::string long_name(3000, 'x');
    EXPECT_THROW(proxy.on_publication_ready(1, 2, 3, 4, long_name.c_str(), 5, 6, false),
                 std::runtime_error);
}

TEST_F(CncTest, NegativeLabelLengthAddCounter)
{
    ManyToOneRingBuffer ring{ring_buffer_};

    // Build an ADD_COUNTER message with valid key but negative label_length
    std::byte msg[CounterMessageFlyweight::KEY_OFFSET + 8]{};
    concurrent::UnsafeBuffer buffer{msg, sizeof(msg)};
    command::CounterMessageFlyweight fw{buffer, 0};
    fw.set_client_id(42);
    fw.set_correlation_id(100);
    fw.set_counter_type(10);
    fw.set_key_buffer_length(0);
    // label_length is at KEY_OFFSET + key_len = 24
    // Write a negative label length
    buffer.put_i32(CounterMessageFlyweight::KEY_OFFSET, -1);

    ring.write(ADD_COUNTER, msg, sizeof(msg));

    std::vector<std::string> errors;
    MockDriverConductor conductor;
    ClientCommandAdapter adapter{ring, conductor,
        [&](i64, i32, std::string_view message) {
            errors.push_back(std::string(message));
        }};

    adapter.receive();
    EXPECT_EQ(conductor.commands.size(), 0u);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_NE(errors[0].find("label"), std::string::npos);
}

// ===========================================================================
// Security tests (Phase 7 review findings)
// ===========================================================================

TEST_F(CncTest, OverflowedEmbeddedLengthPublicationReady)
{
    // Verify that a broadcast event with an oversized log_file_name_length
    // is safely rejected by the DriverEventsAdapter.
    BroadcastTransmitter tx{broadcast_buffer_};
    BroadcastReceiver rx{broadcast_buffer_};

    constexpr i64 CLIENT_ID = 42;

    // Manually construct a malformed ON_PUBLICATION_READY message
    // with log_file_name_length set to a huge value.
    const i32 msg_len = command::PublicationBuffersReadyFlyweight::LOG_FILE_NAME_OFFSET + 4;
    std::byte msg[128]{};
    concurrent::UnsafeBuffer buffer{msg, msg_len};
    command::PublicationBuffersReadyFlyweight fw{buffer, 0};
    fw.set_correlation_id(200);
    fw.set_registration_id(300);
    fw.set_session_id(42);
    fw.set_stream_id(1001);
    fw.set_pub_limit_counter_id(5);
    fw.set_channel_status_counter_id(6);
    fw.set_log_file_name_length(0x7FFFFFF0);  // huge overflow value

    tx.transmit(ON_PUBLICATION_READY, msg, msg_len);

    MockClientConductor conductor;
    DriverEventsAdapter adapter{rx, conductor, CLIENT_ID};
    adapter.receive(200);

    // Should be rejected -- no events dispatched.
    EXPECT_EQ(conductor.events.size(), 0u);
}

TEST_F(CncTest, OverflowedEmbeddedLengthError)
{
    // Verify that a broadcast ON_ERROR with oversized error_message_length is rejected.
    BroadcastTransmitter tx{broadcast_buffer_};
    BroadcastReceiver rx{broadcast_buffer_};

    constexpr i64 CLIENT_ID = 42;

    const i32 msg_len = command::ErrorResponseFlyweight::ERROR_MESSAGE_OFFSET + 4;
    std::byte msg[128]{};
    concurrent::UnsafeBuffer buffer{msg, msg_len};
    command::ErrorResponseFlyweight fw{buffer, 0};
    fw.set_offending_correlation_id(100);
    fw.set_error_code(1);
    fw.set_error_message_length(0x7FFFFFF0);  // huge overflow value

    tx.transmit(ON_ERROR, msg, msg_len);

    MockClientConductor conductor;
    DriverEventsAdapter adapter{rx, conductor, CLIENT_ID};
    adapter.receive(100);

    EXPECT_EQ(conductor.events.size(), 0u);
}

TEST_F(CncTest, AlignedStaticCounterTruncation)
{
    // Verify that ADD_STATIC_COUNTER with a key_length that fits unaligned
    // but overflows when aligned is correctly rejected.
    // key=5 bytes, aligned to 8, label_length field at 32+8=40, needs 4 more = 44 total.
    // If message is only 39 bytes, the unaligned check would pass but aligned check must fail.
    ManyToOneRingBuffer ring{ring_buffer_};

    const i32 msg_len = command::StaticCounterMessageFlyweight::KEY_BUFFER_OFFSET + 5;  // 37 bytes
    std::byte msg[64]{};
    concurrent::UnsafeBuffer buffer{msg, msg_len};
    command::StaticCounterMessageFlyweight fw{buffer, 0};
    fw.set_client_id(42);
    fw.set_correlation_id(100);
    fw.set_registration_id(777);
    fw.set_counter_type_id(10);
    fw.set_key_length(5);  // 5 bytes key, aligned to 8

    ring.write(ADD_STATIC_COUNTER, msg, msg_len);

    std::vector<std::string> errors;
    MockDriverConductor conductor;
    ClientCommandAdapter adapter{ring, conductor,
        [&](i64, i32, std::string_view message) {
            errors.push_back(std::string(message));
        }};

    adapter.receive();
    EXPECT_EQ(conductor.commands.size(), 0u);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_NE(errors[0].find("key"), std::string::npos);
}

TEST_F(CncTest, NegativeSourceOffsetInDriverProxy)
{
    // Verify that DriverProxy rejects negative offsets.
    ManyToOneRingBuffer ring{ring_buffer_};
    constexpr i64 CLIENT_ID = 42;
    DriverProxy proxy{ring, CLIENT_ID};

    const u8 key[] = {0x01, 0x02};
    EXPECT_THROW(proxy.add_counter(10, key, -1, 2, "label", 0, 5),
                 std::invalid_argument);
    EXPECT_THROW(proxy.add_counter(10, key, 0, 2, "label", -1, 5),
                 std::invalid_argument);
}

TEST_F(CncTest, NegativeSourceOffsetInStaticCounter)
{
    ManyToOneRingBuffer ring{ring_buffer_};
    constexpr i64 CLIENT_ID = 42;
    DriverProxy proxy{ring, CLIENT_ID};

    const u8 key[] = {0x01, 0x02};
    EXPECT_THROW(proxy.add_static_counter(10, key, -1, 2, "label", 0, 5, 777),
                 std::invalid_argument);
    EXPECT_THROW(proxy.add_static_counter(10, key, 0, 2, "label", -1, 5, 777),
                 std::invalid_argument);
}

TEST_F(CncTest, NegativeSourceOffsetTerminateDriver)
{
    ManyToOneRingBuffer ring{ring_buffer_};
    constexpr i64 CLIENT_ID = 42;
    DriverProxy proxy{ring, CLIENT_ID};

    const char token[] = "token";
    EXPECT_THROW(proxy.terminate_driver(token, -1, 5),
                 std::invalid_argument);
}

TEST_F(CncTest, NullBufferPositiveLengthInDriverProxy)
{
    // Verify that nullptr buffer with positive length clamps length to 0.
    ManyToOneRingBuffer ring{ring_buffer_};
    constexpr i64 CLIENT_ID = 42;
    DriverProxy proxy{ring, CLIENT_ID};

    // Should not throw -- nullptr with positive length is clamped to 0.
    const i64 corr_id = proxy.add_counter(10, nullptr, 0, 100, nullptr, 0, 200);
    EXPECT_GT(corr_id, 0);

    MockDriverConductor conductor;
    ClientCommandAdapter adapter{ring, conductor};
    adapter.receive();

    ASSERT_EQ(conductor.commands.size(), 1u);
    EXPECT_EQ(conductor.commands[0].type, ADD_COUNTER);
}

TEST_F(CncTest, StaleAddressReuseInPublicationError)
{
    // Verify that sending two publication error frames with different address
    // configurations doesn't leak stale address data.
    BroadcastTransmitter tx{broadcast_buffer_};
    BroadcastReceiver rx{broadcast_buffer_};

    constexpr i64 CLIENT_ID = 42;

    ClientProxy proxy{tx};

    // First call: with a specific address
    const u8 addr1[16] = {192, 168, 1, 1};
    proxy.on_publication_error_frame(100, 200, 42, 1001, 500, 600, 1, 4045, addr1, 1, "first");

    // Second call: with nullptr address (should zero the address field)
    proxy.on_publication_error_frame(101, 201, 43, 1002, 501, 601, 2, 4046, nullptr, 2, "second");

    MockClientConductor conductor;
    DriverEventsAdapter adapter{rx, conductor, CLIENT_ID};
    adapter.receive(-1);

    ASSERT_EQ(conductor.events.size(), 2u);

    // First event should have addr1
    EXPECT_EQ(conductor.events[0].address[0], 192);
    EXPECT_EQ(conductor.events[0].address[1], 168);

    // Second event should have zeroed address (not stale addr1)
    EXPECT_EQ(conductor.events[1].address[0], 0);
    EXPECT_EQ(conductor.events[1].address[1], 0);
}

TEST_F(CncTest, NegativeSetterLengthFlyweight)
{
    // Verify that flyweight setters with negative lengths are no-ops.
    // Each sub-test uses its own zeroed buffer so stale data doesn't interfere.
    std::byte buf[256]{};

    // PublicationMessageFlyweight::set_channel
    {
        std::memset(buf, 0, sizeof(buf));
        concurrent::UnsafeBuffer buffer{buf, sizeof(buf)};
        command::PublicationMessageFlyweight fw{buffer, 0};
        fw.set_channel_length(10);
        fw.set_channel("test", -1);
        // Negative length is a no-op, length should remain 10
        EXPECT_EQ(fw.channel_length(), 10);
    }

    // ErrorResponseFlyweight::set_error_message
    {
        std::memset(buf, 0, sizeof(buf));
        concurrent::UnsafeBuffer buffer{buf, sizeof(buf)};
        command::ErrorResponseFlyweight fw{buffer, 0};
        fw.set_error_message_length(5);
        fw.set_error_message("test", -1);
        // Negative length is a no-op, length should remain 5
        EXPECT_EQ(fw.error_message_length(), 5);
    }

    // CounterMessageFlyweight::set_key_buffer
    {
        std::memset(buf, 0, sizeof(buf));
        concurrent::UnsafeBuffer buffer{buf, sizeof(buf)};
        command::CounterMessageFlyweight fw{buffer, 0};
        fw.set_key_buffer_length(3);
        fw.set_key_buffer("key", -1);
        // Negative length is a no-op, length should remain 3
        EXPECT_EQ(fw.key_buffer_length(), 3);
    }

    // CounterMessageFlyweight::set_label
    {
        std::memset(buf, 0, sizeof(buf));
        concurrent::UnsafeBuffer buffer{buf, sizeof(buf)};
        command::CounterMessageFlyweight fw{buffer, 0};
        fw.set_key_buffer(nullptr, 0);
        fw.set_label_length(5);
        fw.set_label("label", -1);
        // Negative length is a no-op, length should remain 5
        EXPECT_EQ(fw.label_length(), 5);
    }

    // RejectImageFlyweight::set_reason
    {
        std::memset(buf, 0, sizeof(buf));
        concurrent::UnsafeBuffer buffer{buf, sizeof(buf)};
        command::RejectImageFlyweight fw{buffer, 0};
        fw.set_reason_length(6);
        fw.set_reason("reason", -1);
        // Negative length is a no-op, length should remain 6
        EXPECT_EQ(fw.reason_length(), 6);
    }

    // PublicationErrorFrameFlyweight::set_error_message
    {
        std::memset(buf, 0, sizeof(buf));
        concurrent::UnsafeBuffer buffer{buf, sizeof(buf)};
        command::PublicationErrorFrameFlyweight fw{buffer, 0};
        fw.set_error_message_length(5);
        fw.set_error_message("error", -1);
        // Negative length is a no-op, length should remain 5
        EXPECT_EQ(fw.error_message_length(), 5);
    }

    // PublicationErrorFrameFlyweight::set_address with negative length
    {
        std::memset(buf, 0, sizeof(buf));
        concurrent::UnsafeBuffer buffer{buf, sizeof(buf)};
        command::PublicationErrorFrameFlyweight fw{buffer, 0};
        fw.set_address(nullptr, -1);
        // Should not crash or corrupt memory
    }
}

TEST_F(CncTest, MalformedBroadcastEventIgnored)
{
    // Verify that an unknown broadcast event type is silently ignored.
    BroadcastTransmitter tx{broadcast_buffer_};
    BroadcastReceiver rx{broadcast_buffer_};

    constexpr i64 CLIENT_ID = 42;

    // Transmit a message with an unknown type ID
    std::byte msg[16]{};
    tx.transmit(0xFE, msg, sizeof(msg));

    MockClientConductor conductor;
    DriverEventsAdapter adapter{rx, conductor, CLIENT_ID};
    const i32 count = adapter.receive(-1);

    EXPECT_EQ(count, 1);  // message was received
    EXPECT_EQ(conductor.events.size(), 0u);  // but no events dispatched
}

TEST_F(CncTest, OverflowedEmbeddedLengthImageBuffersReady)
{
    // Verify that ON_AVAILABLE_IMAGE with oversized log_file_name_length is rejected.
    BroadcastTransmitter tx{broadcast_buffer_};
    BroadcastReceiver rx{broadcast_buffer_};

    constexpr i64 CLIENT_ID = 42;

    const i32 msg_len = command::ImageBuffersReadyFlyweight::LOG_FILE_NAME_OFFSET + 4;
    std::byte msg[128]{};
    concurrent::UnsafeBuffer buffer{msg, msg_len};
    command::ImageBuffersReadyFlyweight fw{buffer, 0};
    fw.set_correlation_id(1000);
    fw.set_session_id(42);
    fw.set_stream_id(1001);
    fw.set_subscription_registration_id(2000);
    fw.set_subscriber_position_id(55);
    fw.set_log_file_name_length(0x7FFFFFF0);  // huge overflow value

    tx.transmit(ON_AVAILABLE_IMAGE, msg, msg_len);

    MockClientConductor conductor;
    DriverEventsAdapter adapter{rx, conductor, CLIENT_ID};
    adapter.receive(-1);

    EXPECT_EQ(conductor.events.size(), 0u);
}

TEST_F(CncTest, OverflowedEmbeddedLengthPublicationError)
{
    // Verify that ON_PUBLICATION_ERROR with oversized error_message_length is rejected.
    BroadcastTransmitter tx{broadcast_buffer_};
    BroadcastReceiver rx{broadcast_buffer_};

    constexpr i64 CLIENT_ID = 42;

    const i32 msg_len = command::PublicationErrorFrameFlyweight::ERROR_MESSAGE_OFFSET + 4;
    std::byte msg[128]{};
    concurrent::UnsafeBuffer buffer{msg, msg_len};
    command::PublicationErrorFrameFlyweight fw{buffer, 0};
    fw.set_registration_id(100);
    fw.set_destination_registration_id(200);
    fw.set_session_id(42);
    fw.set_stream_id(1001);
    fw.set_receiver_id(500);
    fw.set_group_tag(600);
    fw.set_error_message_length(0x7FFFFFF0);  // huge overflow value

    tx.transmit(ON_PUBLICATION_ERROR, msg, msg_len);

    MockClientConductor conductor;
    DriverEventsAdapter adapter{rx, conductor, CLIENT_ID};
    adapter.receive(-1);

    EXPECT_EQ(conductor.events.size(), 0u);
}

// ===========================================================================
// Security hardening: null C-string DriverProxy inputs
// ===========================================================================

TEST_F(CncTest, NullChannelAddPublication)
{
    ManyToOneRingBuffer ring{ring_buffer_};
    constexpr i64 CLIENT_ID = 42;

    DriverProxy proxy{ring, CLIENT_ID};
    const i64 corr_id = proxy.add_publication(nullptr, 1001);

    EXPECT_GT(corr_id, 0);

    MockDriverConductor conductor;
    ClientCommandAdapter adapter{ring, conductor};
    adapter.receive();

    ASSERT_EQ(conductor.commands.size(), 1u);
    EXPECT_EQ(conductor.commands[0].type, ADD_PUBLICATION);
    EXPECT_EQ(conductor.commands[0].channel, "");
    EXPECT_EQ(conductor.commands[0].stream_id, 1001);
}

TEST_F(CncTest, NullChannelAddSubscription)
{
    ManyToOneRingBuffer ring{ring_buffer_};
    constexpr i64 CLIENT_ID = 42;

    DriverProxy proxy{ring, CLIENT_ID};
    proxy.add_subscription(nullptr, 1001);

    MockDriverConductor conductor;
    ClientCommandAdapter adapter{ring, conductor};
    adapter.receive();

    ASSERT_EQ(conductor.commands.size(), 1u);
    EXPECT_EQ(conductor.commands[0].type, ADD_SUBSCRIPTION);
    EXPECT_EQ(conductor.commands[0].channel, "");
}

TEST_F(CncTest, NullChannelAddDestination)
{
    ManyToOneRingBuffer ring{ring_buffer_};
    constexpr i64 CLIENT_ID = 42;

    DriverProxy proxy{ring, CLIENT_ID};
    proxy.add_destination(111, nullptr);

    MockDriverConductor conductor;
    ClientCommandAdapter adapter{ring, conductor};
    adapter.receive();

    ASSERT_EQ(conductor.commands.size(), 1u);
    EXPECT_EQ(conductor.commands[0].type, ADD_DESTINATION);
    EXPECT_EQ(conductor.commands[0].channel, "");
}

TEST_F(CncTest, NullLabelAddCounter)
{
    ManyToOneRingBuffer ring{ring_buffer_};
    constexpr i64 CLIENT_ID = 42;

    DriverProxy proxy{ring, CLIENT_ID};
    proxy.add_counter(10, nullptr);

    MockDriverConductor conductor;
    ClientCommandAdapter adapter{ring, conductor};
    adapter.receive();

    ASSERT_EQ(conductor.commands.size(), 1u);
    EXPECT_EQ(conductor.commands[0].type, ADD_COUNTER);
    EXPECT_EQ(conductor.commands[0].channel, "");
}

TEST_F(CncTest, NullReasonRejectImage)
{
    ManyToOneRingBuffer ring{ring_buffer_};
    constexpr i64 CLIENT_ID = 42;

    DriverProxy proxy{ring, CLIENT_ID};
    proxy.reject_image(5555, 1024, nullptr);

    MockDriverConductor conductor;
    ClientCommandAdapter adapter{ring, conductor};
    adapter.receive();

    ASSERT_EQ(conductor.commands.size(), 1u);
    EXPECT_EQ(conductor.commands[0].type, REJECT_IMAGE);
    EXPECT_EQ(conductor.commands[0].reason, "");
}

TEST_F(CncTest, NullLabelAddStaticCounter)
{
    ManyToOneRingBuffer ring{ring_buffer_};
    constexpr i64 CLIENT_ID = 42;

    DriverProxy proxy{ring, CLIENT_ID};
    proxy.add_static_counter(10, nullptr, 777);

    MockDriverConductor conductor;
    ClientCommandAdapter adapter{ring, conductor};
    adapter.receive();

    ASSERT_EQ(conductor.commands.size(), 1u);
    EXPECT_EQ(conductor.commands[0].type, ADD_STATIC_COUNTER);
    EXPECT_EQ(conductor.commands[0].channel, "");
}

TEST_F(CncTest, NullChannelAddExclusivePublication)
{
    ManyToOneRingBuffer ring{ring_buffer_};
    constexpr i64 CLIENT_ID = 42;

    DriverProxy proxy{ring, CLIENT_ID};
    proxy.add_exclusive_publication(nullptr, 2001);

    MockDriverConductor conductor;
    ClientCommandAdapter adapter{ring, conductor};
    adapter.receive();

    ASSERT_EQ(conductor.commands.size(), 1u);
    EXPECT_EQ(conductor.commands[0].type, ADD_EXCLUSIVE_PUBLICATION);
    EXPECT_EQ(conductor.commands[0].channel, "");
}

TEST_F(CncTest, NullChannelRemoveDestination)
{
    ManyToOneRingBuffer ring{ring_buffer_};
    constexpr i64 CLIENT_ID = 42;

    DriverProxy proxy{ring, CLIENT_ID};
    proxy.remove_destination(111, nullptr);

    MockDriverConductor conductor;
    ClientCommandAdapter adapter{ring, conductor};
    adapter.receive();

    ASSERT_EQ(conductor.commands.size(), 1u);
    EXPECT_EQ(conductor.commands[0].type, REMOVE_DESTINATION);
    EXPECT_EQ(conductor.commands[0].channel, "");
}

TEST_F(CncTest, NullChannelAddRcvDestination)
{
    ManyToOneRingBuffer ring{ring_buffer_};
    constexpr i64 CLIENT_ID = 42;

    DriverProxy proxy{ring, CLIENT_ID};
    proxy.add_rcv_destination(333, nullptr);

    MockDriverConductor conductor;
    ClientCommandAdapter adapter{ring, conductor};
    adapter.receive();

    ASSERT_EQ(conductor.commands.size(), 1u);
    EXPECT_EQ(conductor.commands[0].type, ADD_RCV_DESTINATION);
    EXPECT_EQ(conductor.commands[0].channel, "");
}

TEST_F(CncTest, NullChannelRemoveRcvDestination)
{
    ManyToOneRingBuffer ring{ring_buffer_};
    constexpr i64 CLIENT_ID = 42;

    DriverProxy proxy{ring, CLIENT_ID};
    proxy.remove_rcv_destination(333, nullptr);

    MockDriverConductor conductor;
    ClientCommandAdapter adapter{ring, conductor};
    adapter.receive();

    ASSERT_EQ(conductor.commands.size(), 1u);
    EXPECT_EQ(conductor.commands[0].type, REMOVE_RCV_DESTINATION);
    EXPECT_EQ(conductor.commands[0].channel, "");
}

// ===========================================================================
// Security hardening: null C-string ClientProxy inputs
// ===========================================================================

TEST_F(CncTest, NullErrorMessageClientProxy)
{
    BroadcastTransmitter tx{broadcast_buffer_};
    BroadcastReceiver rx{broadcast_buffer_};

    constexpr i64 CLIENT_ID = 42;

    ClientProxy proxy{tx};
    proxy.on_error(100, ERROR_CODE_GENERIC, nullptr);

    MockClientConductor conductor;
    DriverEventsAdapter adapter{rx, conductor, CLIENT_ID};
    adapter.receive(100);

    ASSERT_EQ(conductor.events.size(), 1u);
    EXPECT_EQ(conductor.events[0].type, ON_ERROR);
    EXPECT_EQ(conductor.events[0].error_message, "");
}

TEST_F(CncTest, NullLogFileClientProxy)
{
    BroadcastTransmitter tx{broadcast_buffer_};
    BroadcastReceiver rx{broadcast_buffer_};

    constexpr i64 CLIENT_ID = 42;

    ClientProxy proxy{tx};
    proxy.on_publication_ready(200, 300, 1001, 42, nullptr, 5, 6, false);

    MockClientConductor conductor;
    DriverEventsAdapter adapter{rx, conductor, CLIENT_ID};
    adapter.receive(200);

    ASSERT_EQ(conductor.events.size(), 1u);
    EXPECT_EQ(conductor.events[0].type, ON_PUBLICATION_READY);
    EXPECT_EQ(conductor.events[0].log_file_name, "");
}

TEST_F(CncTest, NullSourceIdentityClientProxy)
{
    BroadcastTransmitter tx{broadcast_buffer_};
    BroadcastReceiver rx{broadcast_buffer_};

    constexpr i64 CLIENT_ID = 42;

    ClientProxy proxy{tx};
    proxy.on_available_image(500, 1001, 42, 2000, 55, "/tmp/test.log", nullptr);

    MockClientConductor conductor;
    DriverEventsAdapter adapter{rx, conductor, CLIENT_ID};
    adapter.receive(500);

    ASSERT_EQ(conductor.events.size(), 1u);
    EXPECT_EQ(conductor.events[0].type, ON_AVAILABLE_IMAGE);
    EXPECT_EQ(conductor.events[0].source_identity, "");
}

TEST_F(CncTest, NullErrorMessagePublicationErrorClientProxy)
{
    BroadcastTransmitter tx{broadcast_buffer_};
    BroadcastReceiver rx{broadcast_buffer_};

    constexpr i64 CLIENT_ID = 42;

    ClientProxy proxy{tx};
    proxy.on_publication_error_frame(100, 200, 42, 1001, 500, 600, 1, 4045, nullptr, 2, nullptr);

    MockClientConductor conductor;
    DriverEventsAdapter adapter{rx, conductor, CLIENT_ID};
    adapter.receive(-1);

    ASSERT_EQ(conductor.events.size(), 1u);
    EXPECT_EQ(conductor.events[0].type, ON_PUBLICATION_ERROR);
    EXPECT_EQ(conductor.events[0].error_message, "");
}
