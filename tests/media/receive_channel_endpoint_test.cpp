#include "caeron/common/endian.h"
#include "caeron/driver/media/receive_channel_endpoint.h"
#include "caeron/driver/media/socket_address_parser.h"
#include "caeron/driver/media/udp_channel.h"
#include "caeron/protocol/data_header_flyweight.h"
#include "caeron/protocol/status_message_flyweight.h"
#include "platform/posix/udp_socket.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstring>
#include <poll.h>
#include <thread>
#include <vector>

using namespace caeron;
using namespace caeron::driver::media;

TEST(ReceiveChannelEndpoint, Construction)
{
    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    ReceiveChannelEndpoint endpoint(ch, 0);

    EXPECT_TRUE(endpoint.should_be_closed());
    EXPECT_GT(endpoint.receiver_id(), 0);
}

TEST(ReceiveChannelEndpoint, StreamRefCounting)
{
    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    ReceiveChannelEndpoint endpoint(ch, 0);

    EXPECT_EQ(endpoint.inc_ref_to_stream(1), 1);
    EXPECT_EQ(endpoint.inc_ref_to_stream(1), 2);
    EXPECT_EQ(endpoint.dec_ref_to_stream(1), 1);
    // Still has a stream ref -- should not be closed
    EXPECT_FALSE(endpoint.should_be_closed());
    EXPECT_EQ(endpoint.dec_ref_to_stream(1), 0);
    // All stream refs removed -- should be closed (no image refs either)
    EXPECT_TRUE(endpoint.should_be_closed());
}

TEST(ReceiveChannelEndpoint, SessionRefCounting)
{
    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    ReceiveChannelEndpoint endpoint(ch, 0);

    auto count = endpoint.inc_ref_to_stream_and_session(100, 200);
    EXPECT_EQ(count, 1);
    count = endpoint.inc_ref_to_stream_and_session(100, 200);
    EXPECT_EQ(count, 2);
    count = endpoint.dec_ref_to_stream_and_session(100, 200);
    EXPECT_EQ(count, 1);
    count = endpoint.dec_ref_to_stream_and_session(100, 200);
    EXPECT_EQ(count, 0);
}

// HIGH-1: Verify negative session_id produces distinct keys (no sign-extension collision)
TEST(ReceiveChannelEndpoint, StreamAndSessionKeyNegativeSessionId)
{
    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    ReceiveChannelEndpoint endpoint(ch, 0);

    // Two entries with different stream_ids but same negative session_id
    auto count1 = endpoint.inc_ref_to_stream_and_session(100, -1);
    EXPECT_EQ(count1, 1);
    auto count2 = endpoint.inc_ref_to_stream_and_session(200, -1);
    EXPECT_EQ(count2, 1);

    // They must be distinct entries (before fix, both mapped to same key)
    auto dec1 = endpoint.dec_ref_to_stream_and_session(100, -1);
    EXPECT_EQ(dec1, 0);
    // The other entry should still exist
    auto dec2 = endpoint.dec_ref_to_stream_and_session(200, -1);
    EXPECT_EQ(dec2, 0);
}

TEST(ReceiveChannelEndpoint, ResponseRefCounting)
{
    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    ReceiveChannelEndpoint endpoint(ch, 0);

    EXPECT_EQ(endpoint.inc_response_ref_to_stream(42), 1);
    EXPECT_EQ(endpoint.inc_response_ref_to_stream(42), 2);
    EXPECT_EQ(endpoint.dec_response_ref_to_stream(42), 1);
    EXPECT_EQ(endpoint.dec_response_ref_to_stream(42), 0);
}

TEST(ReceiveChannelEndpoint, ParseDataHeader)
{
    std::array<std::byte, 64> buf{};
    u8 flags = 0xC0;  // BEGIN | END

    put_le32(buf.data(), 32);
    buf[5] = std::byte{flags};
    put_le32(buf.data() + 8, 0);     // term_offset
    put_le32(buf.data() + 12, 1000); // session_id
    put_le32(buf.data() + 16, 2000); // stream_id
    put_le32(buf.data() + 20, 5);    // term_id

    i32 parsed_session = 0, parsed_stream = 0, parsed_term = 0;
    i32 parsed_offset = 0, parsed_frame_len = 0;
    u8 parsed_flags = 0;

    EXPECT_TRUE(ReceiveChannelEndpoint::parse_data_header(
        buf.data(), 64,
        parsed_session, parsed_stream, parsed_term,
        parsed_offset, parsed_frame_len, parsed_flags));

    EXPECT_EQ(parsed_session, 1000);
    EXPECT_EQ(parsed_stream, 2000);
    EXPECT_EQ(parsed_term, 5);
    EXPECT_EQ(parsed_offset, 0);
    EXPECT_EQ(parsed_frame_len, 32);
    EXPECT_EQ(parsed_flags, 0xC0);
}

TEST(ReceiveChannelEndpoint, ParseDataHeaderTooShort)
{
    std::array<std::byte, 4> buf{};
    i32 s, st, t, o, f;
    u8 flags;
    EXPECT_FALSE(ReceiveChannelEndpoint::parse_data_header(
        buf.data(), 4, s, st, t, o, f, flags));
}

TEST(ReceiveChannelEndpoint, ParseSetupHeader)
{
    std::array<std::byte, 64> buf{};

    put_le32(buf.data() + 12, 100);     // session_id
    put_le32(buf.data() + 16, 200);     // stream_id
    put_le32(buf.data() + 20, 1);       // initial_term_id
    put_le32(buf.data() + 24, 3);       // active_term_id
    put_le32(buf.data() + 28, 65536);   // term_length
    put_le32(buf.data() + 32, 4096);    // mtu
    put_le32(buf.data() + 36, 4);       // ttl

    i32 ps, pstream, pit, pat, ptl, pmtu, pttl;
    EXPECT_TRUE(ReceiveChannelEndpoint::parse_setup_header(
        buf.data(), 64, ps, pstream, pit, pat, ptl, pmtu, pttl));

    EXPECT_EQ(ps, 100);
    EXPECT_EQ(pstream, 200);
    EXPECT_EQ(pit, 1);
    EXPECT_EQ(pat, 3);
    EXPECT_EQ(ptl, 65536);
    EXPECT_EQ(pmtu, 4096);
    EXPECT_EQ(pttl, 4);
}

TEST(ReceiveChannelEndpoint, ParseRttMeasurement)
{
    std::array<std::byte, 64> buf{};
    buf[5] = std::byte{0x80};  // REPLY_FLAG

    put_le32(buf.data() + 8, 10);              // session_id
    put_le32(buf.data() + 12, 20);             // stream_id
    put_le64(buf.data() + 16, 1234567890LL);   // echo_ts
    put_le64(buf.data() + 24, 1000);           // delta
    put_le64(buf.data() + 32, 999);            // receiver_id

    i32 ps, pstream;
    i64 pecho, pdelta, prid;
    bool is_reply;

    EXPECT_TRUE(ReceiveChannelEndpoint::parse_rtt_measurement(
        buf.data(), 64, ps, pstream, pecho, pdelta, prid, is_reply));

    EXPECT_EQ(ps, 10);
    EXPECT_EQ(pstream, 20);
    EXPECT_EQ(pecho, 1234567890LL);
    EXPECT_EQ(pdelta, 1000);
    EXPECT_EQ(prid, 999);
    EXPECT_TRUE(is_reply);
}

TEST(ReceiveChannelEndpoint, ImageRefCounting)
{
    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    ReceiveChannelEndpoint endpoint(ch, 0);

    endpoint.inc_image_ref();
    endpoint.inc_image_ref();
    EXPECT_FALSE(endpoint.should_be_closed());

    endpoint.dec_image_ref();
    endpoint.dec_image_ref();
    EXPECT_TRUE(endpoint.should_be_closed());
}

TEST(ReceiveChannelEndpoint, SendStatusMessage)
{
    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    ReceiveChannelEndpoint endpoint(ch, 0);
    endpoint.open_datagram_channel();

    // Set up a receiver socket
    caeron::platform::UdpSocket recv_sock;
    recv_sock.bind("127.0.0.1", 0);

    struct sockaddr_in bound{};
    socklen_t len = sizeof(bound);
    ::getsockname(recv_sock.fd(),
                  reinterpret_cast<struct sockaddr*>(&bound), &len);

    // Create an image connection pointing to our receiver
    struct sockaddr_storage control_addr{};
    std::memcpy(&control_addr, &bound, sizeof(bound));

    ImageConnection conn(0, control_addr);
    std::vector<ImageConnection> connections = {conn};

    // Send SM
    endpoint.send_status_message(connections, 100, 200, 1, 0, 65536, 0);

    // Wait for data with poll (up to 100ms)
    struct pollfd pfd{};
    pfd.fd = recv_sock.fd();
    pfd.events = POLLIN;
    int poll_result = ::poll(&pfd, 1, 100);
    ASSERT_GT(poll_result, 0) << "Timed out waiting for SM";

    // Receive on the other side
    char buf[128]{};
    std::string from;
    u16 from_port = 0;
    auto received = recv_sock.receive_from(buf, 128, from, from_port);
    EXPECT_GE(received, 36);  // SM is at least 36 bytes

    endpoint.close();
}

// MEDIUM-2: Verify HAS_GROUP_ID_FLAG is set when group_tag is present
TEST(ReceiveChannelEndpoint, SendStatusMessageWithGroupTagSetsFlag)
{
    // Create channel with group-tag
    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0|group-tag=42");
    ReceiveChannelEndpoint endpoint(ch, 0);
    endpoint.open_datagram_channel();

    caeron::platform::UdpSocket recv_sock;
    recv_sock.bind("127.0.0.1", 0);

    struct sockaddr_in bound{};
    socklen_t len = sizeof(bound);
    ::getsockname(recv_sock.fd(),
                  reinterpret_cast<struct sockaddr*>(&bound), &len);

    struct sockaddr_storage control_addr{};
    std::memcpy(&control_addr, &bound, sizeof(bound));

    ImageConnection conn(0, control_addr);
    std::vector<ImageConnection> connections = {conn};

    endpoint.send_status_message(connections, 100, 200, 1, 0, 65536, 0);

    // Wait for data with poll (up to 100ms)
    {
        struct pollfd pfd{};
        pfd.fd = recv_sock.fd();
        pfd.events = POLLIN;
        int poll_result = ::poll(&pfd, 1, 100);
        ASSERT_GT(poll_result, 0) << "Timed out waiting for SM with group tag";
    }

    char buf[128]{};
    std::string from;
    u16 from_port = 0;
    auto received = recv_sock.receive_from(buf, 128, from, from_port);
    EXPECT_GE(received, 44);  // SM with group tag is 44 bytes

    // Verify HAS_GROUP_ID_FLAG (0x08) is set in flags byte at offset 5
    u8 flags = static_cast<u8>(buf[5]);
    EXPECT_NE(flags & protocol::StatusMessageFlyweight::HAS_GROUP_ID_FLAG, 0);

    // Verify frame_length is 44
    i32 frame_length = 0;
    std::memcpy(&frame_length, buf, sizeof(i32));
    EXPECT_EQ(frame_length, protocol::StatusMessageFlyweight::HEADER_LENGTH_WITH_GROUP_TAG);

    endpoint.close();
}

// Finding 7: Verify group-tag=0 is not collapsed to "absent".
// With std::optional<i64>, group-tag=0 should produce a 44-byte SM with
// HAS_GROUP_ID_FLAG set and group_tag field = 0 (not silently omitted).
TEST(ReceiveChannelEndpoint, SendStatusMessageWithGroupTagZero)
{
    // Create channel with group-tag=0 (explicit zero, not absent)
    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0|group-tag=0");
    ReceiveChannelEndpoint endpoint(ch, 0);
    endpoint.open_datagram_channel();

    caeron::platform::UdpSocket recv_sock;
    recv_sock.bind("127.0.0.1", 0);

    struct sockaddr_in bound{};
    socklen_t len = sizeof(bound);
    ::getsockname(recv_sock.fd(),
                  reinterpret_cast<struct sockaddr*>(&bound), &len);

    struct sockaddr_storage control_addr{};
    std::memcpy(&control_addr, &bound, sizeof(bound));

    ImageConnection conn(0, control_addr);
    std::vector<ImageConnection> connections = {conn};

    endpoint.send_status_message(connections, 100, 200, 1, 0, 65536, 0);

    // Wait for data with poll
    {
        struct pollfd pfd{};
        pfd.fd = recv_sock.fd();
        pfd.events = POLLIN;
        int poll_result = ::poll(&pfd, 1, 100);
        ASSERT_GT(poll_result, 0) << "Timed out waiting for SM with group-tag=0";
    }

    char buf[128]{};
    std::string from;
    u16 from_port = 0;
    auto received = recv_sock.receive_from(buf, 128, from, from_port);

    // SM with group tag must be 44 bytes (not 36)
    EXPECT_GE(received, 44) << "group-tag=0 must produce 44-byte SM, not 36";

    // Verify HAS_GROUP_ID_FLAG is set
    u8 flags = static_cast<u8>(buf[5]);
    EXPECT_NE(flags & protocol::StatusMessageFlyweight::HAS_GROUP_ID_FLAG, 0)
        << "HAS_GROUP_ID_FLAG must be set for group-tag=0";

    // Verify group_tag field at offset 36 is 0
    i64 group_tag = 0;
    std::memcpy(&group_tag, buf + 36, sizeof(i64));
    EXPECT_EQ(group_tag, 0) << "group_tag field must be 0, not absent";

    endpoint.close();
}

// MEDIUM-3: Verify long error messages are truncated, not silently dropped
TEST(ReceiveChannelEndpoint, SendErrorFrameTruncatesLongMessage)
{
    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    ReceiveChannelEndpoint endpoint(ch, 0);
    endpoint.open_datagram_channel();

    caeron::platform::UdpSocket recv_sock;
    recv_sock.bind("127.0.0.1", 0);

    struct sockaddr_in bound{};
    socklen_t len = sizeof(bound);
    ::getsockname(recv_sock.fd(),
                  reinterpret_cast<struct sockaddr*>(&bound), &len);

    struct sockaddr_storage control_addr{};
    std::memcpy(&control_addr, &bound, sizeof(bound));

    ImageConnection conn(0, control_addr);
    std::vector<ImageConnection> connections = {conn};

    // error_buffer_ is 256 bytes, HEADER_LENGTH is 40, so max message is 216 bytes.
    // Send a 300-byte message -- should be truncated to 216 bytes, not dropped.
    std::string long_message(300, 'X');
    endpoint.send_error_frame(connections, 100, 200, 1, long_message);

    // Wait for data with poll (up to 100ms)
    {
        struct pollfd pfd{};
        pfd.fd = recv_sock.fd();
        pfd.events = POLLIN;
        int poll_result = ::poll(&pfd, 1, 100);
        ASSERT_GT(poll_result, 0) << "Timed out waiting for error frame";
    }

    char buf[512]{};
    std::string from;
    u16 from_port = 0;
    auto received = recv_sock.receive_from(buf, 512, from, from_port);

    // Before fix: received would be 0 (frame silently dropped).
    // After fix: received should be 256 (40 header + 216 truncated message).
    EXPECT_EQ(received, 256);

    // Verify the frame_length field matches
    i32 frame_length = 0;
    std::memcpy(&frame_length, buf, sizeof(i32));
    EXPECT_EQ(frame_length, 256);

    endpoint.close();
}

// CRITICAL-2: Verify that concurrent ref counting and dispatch is thread-safe.
// Before the fix, ref_count_by_stream_id_ and diagnostic counters were accessed
// from multiple threads without synchronization (data race / UB).
TEST(ReceiveChannelEndpoint, ConcurrentRefCountingAndDispatch)
{
    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    ReceiveChannelEndpoint endpoint(ch, 0);

    std::atomic<bool> stop{false};
    constexpr int NUM_STREAMS = 50;

    // Thread 1: inc/dec ref in a loop (conductor thread pattern)
    std::thread ref_thread([&endpoint, &stop]() {
        while (!stop.load(std::memory_order_relaxed))
        {
            for (int i = 0; i < NUM_STREAMS; ++i)
            {
                (void)endpoint.inc_ref_to_stream(i);
                (void)endpoint.dec_ref_to_stream(i);
            }
        }
    });

    // Thread 2: dispatch data frames in a loop (poller thread pattern)
    std::thread dispatch_thread([&endpoint, &stop]() {
        std::array<std::byte, 64> buf{};
        put_le32(buf.data(), 32);
        buf[5] = std::byte{0xC0};  // BEGIN | END
        put_le32(buf.data() + 8, 1000);   // term_offset (actually session_id in this layout)
        put_le32(buf.data() + 12, 1000);  // session_id
        put_le32(buf.data() + 16, 2000);  // stream_id
        put_le32(buf.data() + 20, 5);     // term_id

        struct sockaddr_storage src{};

        while (!stop.load(std::memory_order_relaxed))
        {
            (void)endpoint.dispatch_data_frame(buf.data(), 64, src);
        }
    });

    // Run for a short duration
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    stop.store(true, std::memory_order_relaxed);

    ref_thread.join();
    dispatch_thread.join();

    // If we get here without crashing, the concurrent access is safe.
    SUCCEED();
}

// CRITICAL-2: Verify that concurrent session ref counting is thread-safe.
TEST(ReceiveChannelEndpoint, ConcurrentSessionRefCounting)
{
    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    ReceiveChannelEndpoint endpoint(ch, 0);

    std::atomic<bool> stop{false};

    // Thread 1: inc/dec stream+session ref
    std::thread t1([&endpoint, &stop]() {
        while (!stop.load(std::memory_order_relaxed))
        {
            (void)endpoint.inc_ref_to_stream_and_session(100, 200);
            (void)endpoint.dec_ref_to_stream_and_session(100, 200);
        }
    });

    // Thread 2: inc/dec image ref
    std::thread t2([&endpoint, &stop]() {
        while (!stop.load(std::memory_order_relaxed))
        {
            endpoint.inc_image_ref();
            endpoint.dec_image_ref();
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    stop.store(true, std::memory_order_relaxed);

    t1.join();
    t2.join();

    SUCCEED();
}

// HIGH-1/HIGH-2: Verify that parse_data_header reads fields in little-endian
// byte order by constructing a frame with explicit LE bytes.
TEST(ReceiveChannelEndpoint, ParseDataHeaderLittleEndian)
{
    std::array<std::byte, 64> buf{};

    // frame_length = 32 (LE: 0x20 0x00 0x00 0x00)
    buf[0] = std::byte{0x20};
    buf[1] = std::byte{0x00};
    buf[2] = std::byte{0x00};
    buf[3] = std::byte{0x00};

    // flags = 0xC0 at offset 5
    buf[5] = std::byte{0xC0};

    // term_offset = 0x10203040 (LE: 0x40 0x30 0x20 0x10)
    buf[8]  = std::byte{0x40};
    buf[9]  = std::byte{0x30};
    buf[10] = std::byte{0x20};
    buf[11] = std::byte{0x10};

    // session_id = 0x01020304 (LE: 0x04 0x03 0x02 0x01)
    buf[12] = std::byte{0x04};
    buf[13] = std::byte{0x03};
    buf[14] = std::byte{0x02};
    buf[15] = std::byte{0x01};

    // stream_id = 0x05060708 (LE: 0x08 0x07 0x06 0x05)
    buf[16] = std::byte{0x08};
    buf[17] = std::byte{0x07};
    buf[18] = std::byte{0x06};
    buf[19] = std::byte{0x05};

    // term_id = 0x0A0B0C0D (LE: 0x0D 0x0C 0x0B 0x0A)
    buf[20] = std::byte{0x0D};
    buf[21] = std::byte{0x0C};
    buf[22] = std::byte{0x0B};
    buf[23] = std::byte{0x0A};

    i32 session_id = 0, stream_id = 0, term_id = 0;
    i32 term_offset = 0, frame_length = 0;
    u8 flags = 0;

    EXPECT_TRUE(ReceiveChannelEndpoint::parse_data_header(
        buf.data(), 64,
        session_id, stream_id, term_id,
        term_offset, frame_length, flags));

    EXPECT_EQ(frame_length, 32);
    EXPECT_EQ(flags, 0xC0);
    EXPECT_EQ(term_offset, 0x10203040);
    EXPECT_EQ(session_id, 0x01020304);
    EXPECT_EQ(stream_id, 0x05060708);
    EXPECT_EQ(term_id, 0x0A0B0C0D);
}

// HIGH-1/HIGH-2: Verify that parse_rtt_measurement reads i64 fields in
// little-endian byte order.
TEST(ReceiveChannelEndpoint, ParseRttMeasurementLittleEndian)
{
    std::array<std::byte, 64> buf{};

    // flags with REPLY_FLAG at offset 5
    buf[5] = std::byte{0x80};

    // session_id = 10 (LE)
    put_le32(buf.data() + 8, 10);

    // stream_id = 20 (LE)
    put_le32(buf.data() + 12, 20);

    // echo_timestamp = 0x0102030405060708 (LE: 0x08 0x07 0x06 0x05 0x04 0x03 0x02 0x01)
    put_le64(buf.data() + 16, 0x0102030405060708LL);

    // reception_delta = 1000 (LE)
    put_le64(buf.data() + 24, 1000);

    // receiver_id = 999 (LE)
    put_le64(buf.data() + 32, 999);

    i32 ps, pstream;
    i64 pecho, pdelta, prid;
    bool is_reply;

    EXPECT_TRUE(ReceiveChannelEndpoint::parse_rtt_measurement(
        buf.data(), 64, ps, pstream, pecho, pdelta, prid, is_reply));

    EXPECT_EQ(ps, 10);
    EXPECT_EQ(pstream, 20);
    EXPECT_EQ(pecho, 0x0102030405060708LL);
    EXPECT_EQ(pdelta, 1000);
    EXPECT_EQ(prid, 999);
    EXPECT_TRUE(is_reply);
}

// HIGH-1/HIGH-2: Verify that send_status_message writes fields in
// little-endian byte order by checking the raw bytes of the sent frame.
TEST(ReceiveChannelEndpoint, SendStatusMessageLittleEndianBytes)
{
    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    ReceiveChannelEndpoint endpoint(ch, 0);
    endpoint.open_datagram_channel();

    caeron::platform::UdpSocket recv_sock;
    recv_sock.bind("127.0.0.1", 0);

    struct sockaddr_in bound{};
    socklen_t len = sizeof(bound);
    ::getsockname(recv_sock.fd(),
                  reinterpret_cast<struct sockaddr*>(&bound), &len);

    struct sockaddr_storage control_addr{};
    std::memcpy(&control_addr, &bound, sizeof(bound));

    ImageConnection conn(0, control_addr);
    std::vector<ImageConnection> connections = {conn};

    // Send SM with known values
    endpoint.send_status_message(connections, 0x01020304, 0x05060708,
                                  0x0A0B0C0D, 0x10203040, 65536, 0);

    // Wait for data with poll (up to 100ms)
    {
        struct pollfd pfd{};
        pfd.fd = recv_sock.fd();
        pfd.events = POLLIN;
        int poll_result = ::poll(&pfd, 1, 100);
        ASSERT_GT(poll_result, 0) << "Timed out waiting for SM";
    }

    char buf[128]{};
    std::string from;
    u16 from_port = 0;
    auto received = recv_sock.receive_from(buf, 128, from, from_port);
    ASSERT_GE(received, 36);

    // Verify frame_length at offset 0 is 36 in LE
    EXPECT_EQ(static_cast<u8>(buf[0]), 0x24);
    EXPECT_EQ(static_cast<u8>(buf[1]), 0x00);

    // Verify type at offset 6 is HDR_TYPE_SM (0x03) in LE
    EXPECT_EQ(static_cast<u8>(buf[6]), 0x03);
    EXPECT_EQ(static_cast<u8>(buf[7]), 0x00);

    // Verify session_id at offset 8 is 0x01020304 in LE
    EXPECT_EQ(static_cast<u8>(buf[8]),  0x04);
    EXPECT_EQ(static_cast<u8>(buf[9]),  0x03);
    EXPECT_EQ(static_cast<u8>(buf[10]), 0x02);
    EXPECT_EQ(static_cast<u8>(buf[11]), 0x01);

    // Verify stream_id at offset 12 is 0x05060708 in LE
    EXPECT_EQ(static_cast<u8>(buf[12]), 0x08);
    EXPECT_EQ(static_cast<u8>(buf[13]), 0x07);
    EXPECT_EQ(static_cast<u8>(buf[14]), 0x06);
    EXPECT_EQ(static_cast<u8>(buf[15]), 0x05);

    endpoint.close();
}
