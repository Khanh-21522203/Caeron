#include "caeron/common/endian.h"
#include "caeron/driver/media/send_channel_endpoint.h"
#include "caeron/driver/media/socket_address_parser.h"
#include "caeron/driver/media/udp_channel.h"
#include "caeron/protocol/header_flyweight.h"
#include "platform/posix/udp_socket.h"

#include <gtest/gtest.h>

#include <cstring>
#include <poll.h>
#include <thread>

using namespace caeron;
using namespace caeron::driver::media;
namespace sap = caeron::driver::media::socket_address_parser;

TEST(SendChannelEndpoint, Construction)
{
    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    SendChannelEndpoint endpoint(ch, 0);

    EXPECT_TRUE(endpoint.should_be_closed());
    EXPECT_FALSE(endpoint.is_active());
}

TEST(SendChannelEndpoint, ReferenceCounting)
{
    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    SendChannelEndpoint endpoint(ch, 0);

    endpoint.inc_ref();
    EXPECT_TRUE(endpoint.is_active());
    EXPECT_FALSE(endpoint.should_be_closed());

    endpoint.dec_ref();
    EXPECT_TRUE(endpoint.should_be_closed());
}

TEST(SendChannelEndpoint, PublicationRegistration)
{
    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    SendChannelEndpoint endpoint(ch, 0);

    auto key = SendChannelEndpoint::session_and_stream_key(100, 200);
    int dummy_pub = 42;
    endpoint.register_for_send(key, &dummy_pub);

    // Unregister
    endpoint.unregister_for_send(key);
}

TEST(SendChannelEndpoint, SessionAndStreamKey)
{
    auto key = SendChannelEndpoint::session_and_stream_key(1, 2);
    // key = (1 << 32) | 2
    EXPECT_EQ(key, (static_cast<i64>(1) << 32) | 2);
}

// HIGH-1: Verify negative stream_id produces distinct keys (no sign-extension collision)
TEST(SendChannelEndpoint, SessionAndStreamKeyNegativeStreamId)
{
    auto key1 = SendChannelEndpoint::session_and_stream_key(1, -1);
    auto key2 = SendChannelEndpoint::session_and_stream_key(2, -1);
    // These must be distinct -- before the fix, both produced the same key
    // because static_cast<i64>(-1) sign-extended to 0xFFFFFFFFFFFFFFFF,
    // wiping out the upper 32 bits.
    EXPECT_NE(key1, key2);

    // Verify the key structure: upper 32 bits = session_id, lower 32 bits = stream_id (zero-extended)
    EXPECT_EQ(key1 >> 32, static_cast<i64>(1));
    EXPECT_EQ(static_cast<u32>(key1 & 0xFFFFFFFF), static_cast<u32>(static_cast<i32>(-1)));
    EXPECT_EQ(key2 >> 32, static_cast<i64>(2));
}

// HIGH-1: Verify dispatch works correctly with negative stream_id
TEST(SendChannelEndpoint, DispatchControlFrameNegativeStreamId)
{
    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    SendChannelEndpoint endpoint(ch, 0);

    // Register publications with different session_ids but same negative stream_id
    auto key1 = SendChannelEndpoint::session_and_stream_key(1, -1);
    auto key2 = SendChannelEndpoint::session_and_stream_key(2, -1);
    int pub1 = 11, pub2 = 22;
    endpoint.register_for_send(key1, &pub1);
    endpoint.register_for_send(key2, &pub2);

    // Build a SM frame with session_id=1, stream_id=-1
    std::array<std::byte, 36> sm_frame{};
    put_le32(sm_frame.data(), 36);
    sm_frame[4] = std::byte{0};
    put_le16(sm_frame.data() + 6, protocol::HeaderFlyweight::HDR_TYPE_SM);
    put_le32(sm_frame.data() + 8, 1);
    put_le32(sm_frame.data() + 12, -1);

    struct sockaddr_storage src{};
    i32 out_session = 0, out_stream = 0;

    // Should find the correct publication (session=1, stream=-1), not collide with (session=2, stream=-1)
    EXPECT_TRUE(endpoint.dispatch_control_frame(
        sm_frame.data(), 36, src, out_session, out_stream));
    EXPECT_EQ(out_session, 1);
    EXPECT_EQ(out_stream, -1);

    endpoint.unregister_for_send(key1);
    endpoint.unregister_for_send(key2);
}

// HIGH-3: Verify dispatch_control_frame looks up publication and dispatches
TEST(SendChannelEndpoint, DispatchControlFrameFindsPublication)
{
    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    SendChannelEndpoint endpoint(ch, 0);

    // Register a publication for session=100, stream=200
    auto key = SendChannelEndpoint::session_and_stream_key(100, 200);
    int dummy_pub = 42;
    endpoint.register_for_send(key, &dummy_pub);

    // Build a SM frame with session_id=100, stream_id=200
    std::array<std::byte, 36> sm_frame{};
    put_le32(sm_frame.data(), 36);
    sm_frame[4] = std::byte{0};  // version
    put_le16(sm_frame.data() + 6, protocol::HeaderFlyweight::HDR_TYPE_SM);
    put_le32(sm_frame.data() + 8, 100);
    put_le32(sm_frame.data() + 12, 200);

    struct sockaddr_storage src{};
    i32 out_session = 0, out_stream = 0;

    // Should return true because the publication is registered
    EXPECT_TRUE(endpoint.dispatch_control_frame(
        sm_frame.data(), 36, src, out_session, out_stream));
    EXPECT_EQ(out_session, 100);
    EXPECT_EQ(out_stream, 200);

    endpoint.unregister_for_send(key);
}

TEST(SendChannelEndpoint, DispatchControlFrameUnknownPublication)
{
    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    SendChannelEndpoint endpoint(ch, 0);

    // Build a SM frame with session_id=999, stream_id=999 (not registered)
    std::array<std::byte, 36> sm_frame{};
    put_le32(sm_frame.data(), 36);
    sm_frame[4] = std::byte{0};
    put_le16(sm_frame.data() + 6, protocol::HeaderFlyweight::HDR_TYPE_SM);
    put_le32(sm_frame.data() + 8, 999);
    put_le32(sm_frame.data() + 12, 999);

    struct sockaddr_storage src{};
    i32 out_session = 0, out_stream = 0;

    // Should return false because no publication is registered for this key
    EXPECT_FALSE(endpoint.dispatch_control_frame(
        sm_frame.data(), 36, src, out_session, out_stream));
}

TEST(SendChannelEndpoint, DispatchControlFrameUnknownType)
{
    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    SendChannelEndpoint endpoint(ch, 0);

    auto key = SendChannelEndpoint::session_and_stream_key(100, 200);
    int dummy_pub = 42;
    endpoint.register_for_send(key, &dummy_pub);

    // Build a frame with an unknown type (0xFF)
    std::array<std::byte, 16> frame{};
    put_le32(frame.data(), 16);
    frame[4] = std::byte{0};
    put_le16(frame.data() + 6, 0x00FF);
    put_le32(frame.data() + 8, 100);
    put_le32(frame.data() + 12, 200);

    struct sockaddr_storage src{};
    i32 out_session = 0, out_stream = 0;

    // Should return false because the frame type is not a control frame
    EXPECT_FALSE(endpoint.dispatch_control_frame(
        frame.data(), 16, src, out_session, out_stream));

    endpoint.unregister_for_send(key);
}

// CRITICAL-1: Verify dispatch_control_frame rejects short frames (8-15 bytes)
// that have a valid type at offset 6 but no room for session_id/stream_id.
TEST(SendChannelEndpoint, DispatchControlFrameRejectsShortFrame)
{
    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    SendChannelEndpoint endpoint(ch, 0);

    // Register a publication at key(0, 0) -- this is the spurious match we're testing against
    auto key = SendChannelEndpoint::session_and_stream_key(0, 0);
    int dummy_pub = 42;
    endpoint.register_for_send(key, &dummy_pub);

    // Build an 8-byte frame with type=SM. The type field is at offset 6-7, which
    // is within the 8-byte bounds check. But session_id (offset 8) and stream_id
    // (offset 12) are NOT in the buffer. Before the fix, the function would read
    // type correctly, leave session_id/stream_id as 0, and look up key(0, 0).
    std::array<std::byte, 8> short_frame{};
    put_le32(short_frame.data(), 8);
    short_frame[4] = std::byte{0};  // version
    put_le16(short_frame.data() + 6, protocol::HeaderFlyweight::HDR_TYPE_SM);
    // No session_id or stream_id -- buffer is only 8 bytes

    struct sockaddr_storage src{};
    i32 out_session = -1, out_stream = -1;

    // Before fix: returns true (spurious match at key(0,0)).
    // After fix: returns false because 8 < 16.
    EXPECT_FALSE(endpoint.dispatch_control_frame(
        short_frame.data(), 8, src, out_session, out_stream));

    // Verify session_id and stream_id were NOT modified (remain at sentinel values)
    EXPECT_EQ(out_session, -1);
    EXPECT_EQ(out_stream, -1);

    // Also test with 15 bytes (still < 16)
    EXPECT_FALSE(endpoint.dispatch_control_frame(
        short_frame.data(), 15, src, out_session, out_stream));

    endpoint.unregister_for_send(key);
}

TEST(SendChannelEndpoint, OpenAndSend)
{
    // Set up a receiver on loopback
    caeron::platform::UdpSocket recv_sock;
    recv_sock.bind("127.0.0.1", 0);

    struct sockaddr_in bound{};
    socklen_t len = sizeof(bound);
    ::getsockname(recv_sock.fd(),
                  reinterpret_cast<struct sockaddr*>(&bound), &len);
    u16 port = ntohs(bound.sin_port);

    auto ch = UdpChannel::parse(
        "aeron:udp?endpoint=127.0.0.1:" + std::to_string(port));
    SendChannelEndpoint endpoint(ch, 0);
    endpoint.open_datagram_channel();

    // Send data
    std::array<std::byte, 32> data{};
    put_le32(data.data(), 32);
    data[4] = std::byte{0};  // version
    put_le16(data.data() + 6, 0x01);  // DATA

    auto sent = endpoint.send(data.data(), 32);
    EXPECT_EQ(sent, 32);

    // Wait for data with poll (up to 100ms)
    struct pollfd pfd{};
    pfd.fd = recv_sock.fd();
    pfd.events = POLLIN;
    int poll_result = ::poll(&pfd, 1, 100);
    ASSERT_GT(poll_result, 0) << "Timed out waiting for UDP data";

    // Receive on the other side
    char buf[64]{};
    std::string from;
    u16 from_port = 0;
    auto received = recv_sock.receive_from(buf, 64, from, from_port);
    EXPECT_EQ(received, 32);

    endpoint.close();
}

// MEDIUM-1: Verify that ref_count_ operations are atomic.
// Concurrent inc_ref/dec_ref from multiple threads must not lose counts.
TEST(SendChannelEndpoint, ConcurrentRefCounting)
{
    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    SendChannelEndpoint endpoint(ch, 0);

    constexpr int NUM_THREADS = 4;
    constexpr int ITERATIONS = 10000;

    // Inc ref from multiple threads
    {
        std::vector<std::thread> threads;
        for (int i = 0; i < NUM_THREADS; ++i)
        {
            threads.emplace_back([&endpoint]() {
                for (int j = 0; j < ITERATIONS; ++j)
                    endpoint.inc_ref();
            });
        }
        for (auto& t : threads)
            t.join();
    }

    // After all increments, ref_count should be NUM_THREADS * ITERATIONS
    // With atomic<int>, this is guaranteed. With plain<int>, this is a data race.
    EXPECT_FALSE(endpoint.should_be_closed());
    EXPECT_TRUE(endpoint.is_active());

    // Dec ref from multiple threads
    {
        std::vector<std::thread> threads;
        for (int i = 0; i < NUM_THREADS; ++i)
        {
            threads.emplace_back([&endpoint]() {
                for (int j = 0; j < ITERATIONS; ++j)
                    endpoint.dec_ref();
            });
        }
        for (auto& t : threads)
            t.join();
    }

    EXPECT_TRUE(endpoint.should_be_closed());
    EXPECT_FALSE(endpoint.is_active());
}

// CRITICAL-1: Verify that concurrent register/unregister and dispatch
// on publication_by_session_and_stream_ is thread-safe.
// Before the fix, this was a data race on std::unordered_map.
TEST(SendChannelEndpoint, ConcurrentRegisterAndDispatch)
{
    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    SendChannelEndpoint endpoint(ch, 0);

    std::atomic<bool> stop{false};
    constexpr int NUM_KEYS = 100;

    // Thread 1: register/unregister in a loop
    std::thread registrar([&endpoint, &stop]() {
        int dummy = 42;
        while (!stop.load(std::memory_order_relaxed))
        {
            for (int i = 0; i < NUM_KEYS; ++i)
            {
                auto key = SendChannelEndpoint::session_and_stream_key(1, i);
                endpoint.register_for_send(key, &dummy);
                endpoint.unregister_for_send(key);
            }
        }
    });

    // Thread 2: dispatch frames in a loop
    std::thread dispatcher([&endpoint, &stop]() {
        std::array<std::byte, 36> sm_frame{};
        put_le32(sm_frame.data(), 36);
        sm_frame[4] = std::byte{0};
        put_le16(sm_frame.data() + 6, protocol::HeaderFlyweight::HDR_TYPE_SM);
        put_le32(sm_frame.data() + 8, 1);
        put_le32(sm_frame.data() + 12, 50);

        struct sockaddr_storage src{};
        i32 out_session = 0, out_stream = 0;

        while (!stop.load(std::memory_order_relaxed))
        {
            (void)endpoint.dispatch_control_frame(
                sm_frame.data(), 36, src, out_session, out_stream);
        }
    });

    // Run for a short duration
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    stop.store(true, std::memory_order_relaxed);

    registrar.join();
    dispatcher.join();

    // If we get here without crashing, the concurrent access is safe.
    SUCCEED();
}

// HIGH-1/HIGH-2: Verify that dispatch_control_frame reads fields in
// little-endian byte order. On x86-64 this is native, but the test
// verifies the byte layout is correct by constructing a frame byte-by-byte.
TEST(SendChannelEndpoint, DispatchControlFrameLittleEndianFields)
{
    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    SendChannelEndpoint endpoint(ch, 0);

    // Register a publication for session=0x01020304, stream=0x05060708
    auto key = SendChannelEndpoint::session_and_stream_key(0x01020304, 0x05060708);
    int dummy_pub = 42;
    endpoint.register_for_send(key, &dummy_pub);

    // Build a SM frame with explicit little-endian byte layout
    std::array<std::byte, 36> sm_frame{};

    // frame_length = 36 (LE: 0x24 0x00 0x00 0x00)
    sm_frame[0] = std::byte{0x24};
    sm_frame[1] = std::byte{0x00};
    sm_frame[2] = std::byte{0x00};
    sm_frame[3] = std::byte{0x00};

    // version = 0, flags = 0
    sm_frame[4] = std::byte{0x00};
    sm_frame[5] = std::byte{0x00};

    // type = HDR_TYPE_SM = 0x03 (LE: 0x03 0x00)
    sm_frame[6] = std::byte{0x03};
    sm_frame[7] = std::byte{0x00};

    // session_id = 0x01020304 (LE: 0x04 0x03 0x02 0x01)
    sm_frame[8]  = std::byte{0x04};
    sm_frame[9]  = std::byte{0x03};
    sm_frame[10] = std::byte{0x02};
    sm_frame[11] = std::byte{0x01};

    // stream_id = 0x05060708 (LE: 0x08 0x07 0x06 0x05)
    sm_frame[12] = std::byte{0x08};
    sm_frame[13] = std::byte{0x07};
    sm_frame[14] = std::byte{0x06};
    sm_frame[15] = std::byte{0x05};

    struct sockaddr_storage src{};
    i32 out_session = 0, out_stream = 0;

    EXPECT_TRUE(endpoint.dispatch_control_frame(
        sm_frame.data(), 36, src, out_session, out_stream));
    EXPECT_EQ(out_session, 0x01020304);
    EXPECT_EQ(out_stream, 0x05060708);

    endpoint.unregister_for_send(key);
}
