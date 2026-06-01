#include "caeron/driver/media/data_transport_poller.h"
#include "caeron/driver/media/udp_channel.h"
#include "platform/posix/epoll_poller.h"
#include "platform/posix/udp_socket.h"

#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <thread>
#include <vector>

using namespace caeron;
using namespace caeron::driver::media;

// Helper: poll until poll_transports() returns > 0 or timeout_ms elapses.
// Returns total bytes received, or 0 on timeout. Prevents flaky hangs.
static i32 poll_until_data(DataTransportPoller& poller, int timeout_ms = 500)
{
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    i32 total = 0;
    while (std::chrono::steady_clock::now() < deadline)
    {
        i32 bytes = poller.poll_transports();
        if (bytes > 0)
        {
            total += bytes;
            return total;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return 0;
}

// Helper: build a little-endian frame header
static void put_frame_header(std::byte* buf, i32 frame_len, u16 type)
{
    put_le32(buf + 0, frame_len);
    buf[4] = std::byte{0};  // version
    buf[5] = std::byte{0};  // flags
    put_le16(buf + 6, type);
}

TEST(DataTransportPoller, Construction)
{
    caeron::platform::EpollPoller poller;
    DataTransportPoller data_poller(poller);
    EXPECT_EQ(data_poller.poll_transports(), 0);
}

TEST(DataTransportPoller, RegisterAndCancel)
{
    caeron::platform::EpollPoller poller;
    DataTransportPoller data_poller(poller);

    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    ReceiveChannelEndpoint endpoint(ch, 0);
    endpoint.open_datagram_channel();

    data_poller.register_for_read(endpoint, endpoint, 0);
    EXPECT_EQ(data_poller.poll_transports(), 0);

    data_poller.cancel_read(endpoint, endpoint);
    endpoint.close();
}

TEST(DataTransportPoller, ReceiveDataFrame)
{
    caeron::platform::EpollPoller poller;
    DataTransportPoller data_poller(poller);

    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    ReceiveChannelEndpoint endpoint(ch, 0);
    endpoint.open_datagram_channel();

    struct sockaddr_storage bound{};
    socklen_t len = sizeof(bound);
    ::getsockname(endpoint.receive_fd(),
                  reinterpret_cast<struct sockaddr*>(&bound), &len);
    auto port = socket_address_parser::get_port(bound);

    data_poller.register_for_read(endpoint, endpoint, 0);

    // Send a DATA frame from a sender socket
    caeron::platform::UdpSocket sender;
    std::array<std::byte, 32> frame{};
    put_frame_header(frame.data(), 32, 0x01);  // DATA

    (void)sender.send_to(frame.data(), 32, "127.0.0.1", port);

    auto bytes = poll_until_data(data_poller);
    EXPECT_GT(bytes, 0);

    data_poller.cancel_read(endpoint, endpoint);
    endpoint.close();
}

// CRITICAL-2: Registering multiple transports must not invalidate epoll pointers.
TEST(DataTransportPoller, MultipleRegistrationsNoUseAfterFree)
{
    caeron::platform::EpollPoller poller;
    DataTransportPoller data_poller(poller);

    std::vector<std::unique_ptr<ReceiveChannelEndpoint>> endpoints;
    for (int i = 0; i < 10; ++i)
    {
        auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
        auto ep = std::make_unique<ReceiveChannelEndpoint>(ch, 0);
        ep->open_datagram_channel();
        data_poller.register_for_read(*ep, *ep, 0);
        endpoints.push_back(std::move(ep));
    }

    EXPECT_NO_FATAL_FAILURE((void)data_poller.poll_transports());

    for (auto& e : endpoints)
        e->close();
}

// CRITICAL-3: Erasing an element must not invalidate pointers for remaining elements.
TEST(DataTransportPoller, EraseDoesNotInvalidateRemainingPointers)
{
    caeron::platform::EpollPoller poller;
    DataTransportPoller data_poller(poller);

    auto ch1 = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    auto ch2 = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    ReceiveChannelEndpoint ep1(ch1, 0);
    ReceiveChannelEndpoint ep2(ch2, 0);
    ep1.open_datagram_channel();
    ep2.open_datagram_channel();

    data_poller.register_for_read(ep1, ep1, 0);
    data_poller.register_for_read(ep2, ep2, 0);

    data_poller.cancel_read(ep1, ep1);

    EXPECT_NO_FATAL_FAILURE((void)data_poller.poll_transports());

    data_poller.cancel_read(ep2, ep2);
    ep1.close();
    ep2.close();
}

TEST(DataTransportPoller, CancelReadForAllTransports)
{
    caeron::platform::EpollPoller poller;
    DataTransportPoller data_poller(poller);

    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    ReceiveChannelEndpoint endpoint(ch, 0);
    endpoint.open_datagram_channel();

    data_poller.register_for_read(endpoint, endpoint, 0);
    EXPECT_EQ(data_poller.poll_transports(), 0);

    data_poller.cancel_read_for_all_transports(endpoint);
    EXPECT_EQ(data_poller.poll_transports(), 0);

    endpoint.close();
}

// CRITICAL-3: Verify dispatch_frame dispatches DATA frames to the endpoint.
TEST(DataTransportPoller, DispatchDataFrameToEndpoint)
{
    caeron::platform::EpollPoller poller;
    DataTransportPoller data_poller(poller);

    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    ReceiveChannelEndpoint endpoint(ch, 0);
    endpoint.open_datagram_channel();

    struct sockaddr_storage bound{};
    socklen_t len = sizeof(bound);
    ::getsockname(endpoint.receive_fd(),
                  reinterpret_cast<struct sockaddr*>(&bound), &len);
    auto port = socket_address_parser::get_port(bound);

    data_poller.register_for_read(endpoint, endpoint, 0);

    EXPECT_EQ(endpoint.data_frame_count(), 0);

    caeron::platform::UdpSocket sender;
    std::array<std::byte, 32> frame{};
    put_frame_header(frame.data(), 32, 0x01);  // DATA
    frame[5] = std::byte{0xC0};  // flags: BEGIN | END
    put_le32(frame.data() + 8, 0);     // term_offset
    put_le32(frame.data() + 12, 42);   // session_id
    put_le32(frame.data() + 16, 100);  // stream_id
    put_le32(frame.data() + 20, 1);    // term_id

    (void)sender.send_to(frame.data(), 32, "127.0.0.1", port);

    auto bytes = poll_until_data(data_poller);
    EXPECT_GT(bytes, 0);

    EXPECT_EQ(endpoint.data_frame_count(), 1);
    EXPECT_EQ(endpoint.last_data_session_id(), 42);
    EXPECT_EQ(endpoint.last_data_stream_id(), 100);

    data_poller.cancel_read(endpoint, endpoint);
    endpoint.close();
}

// CRITICAL-3: Verify dispatch_frame dispatches SETUP frames.
TEST(DataTransportPoller, DispatchSetupFrameToEndpoint)
{
    caeron::platform::EpollPoller poller;
    DataTransportPoller data_poller(poller);

    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    ReceiveChannelEndpoint endpoint(ch, 0);
    endpoint.open_datagram_channel();

    struct sockaddr_storage bound{};
    socklen_t len = sizeof(bound);
    ::getsockname(endpoint.receive_fd(),
                  reinterpret_cast<struct sockaddr*>(&bound), &len);
    auto port = socket_address_parser::get_port(bound);

    data_poller.register_for_read(endpoint, endpoint, 0);

    EXPECT_EQ(endpoint.setup_frame_count(), 0);

    caeron::platform::UdpSocket sender;
    std::array<std::byte, 40> frame{};
    put_frame_header(frame.data(), 40, 0x05);  // SETUP
    put_le32(frame.data() + 12, 77);  // session_id
    put_le32(frame.data() + 16, 88);  // stream_id

    (void)sender.send_to(frame.data(), 40, "127.0.0.1", port);

    auto bytes = poll_until_data(data_poller);
    EXPECT_GT(bytes, 0);

    EXPECT_EQ(endpoint.setup_frame_count(), 1);
    EXPECT_EQ(endpoint.last_setup_session_id(), 77);
    EXPECT_EQ(endpoint.last_setup_stream_id(), 88);

    data_poller.cancel_read(endpoint, endpoint);
    endpoint.close();
}

// CRITICAL-3: Verify dispatch_frame dispatches RTT measurement frames.
TEST(DataTransportPoller, DispatchRttMeasurementToEndpoint)
{
    caeron::platform::EpollPoller poller;
    DataTransportPoller data_poller(poller);

    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    ReceiveChannelEndpoint endpoint(ch, 0);
    endpoint.open_datagram_channel();

    struct sockaddr_storage bound{};
    socklen_t len = sizeof(bound);
    ::getsockname(endpoint.receive_fd(),
                  reinterpret_cast<struct sockaddr*>(&bound), &len);
    auto port = socket_address_parser::get_port(bound);

    data_poller.register_for_read(endpoint, endpoint, 0);

    EXPECT_EQ(endpoint.rtt_frame_count(), 0);

    caeron::platform::UdpSocket sender;
    std::array<std::byte, 40> frame{};
    put_frame_header(frame.data(), 40, 0x06);  // RTTM
    put_le32(frame.data() + 8, 55);   // session_id
    put_le32(frame.data() + 12, 66);  // stream_id

    (void)sender.send_to(frame.data(), 40, "127.0.0.1", port);

    auto bytes = poll_until_data(data_poller);
    EXPECT_GT(bytes, 0);

    EXPECT_EQ(endpoint.rtt_frame_count(), 1);

    data_poller.cancel_read(endpoint, endpoint);
    endpoint.close();
}

// CRITICAL-3: Verify heartbeat DATA frames (frame_length == 0) are dispatched.
TEST(DataTransportPoller, DispatchHeartbeatDataFrame)
{
    caeron::platform::EpollPoller poller;
    DataTransportPoller data_poller(poller);

    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    ReceiveChannelEndpoint endpoint(ch, 0);
    endpoint.open_datagram_channel();

    struct sockaddr_storage bound{};
    socklen_t len = sizeof(bound);
    ::getsockname(endpoint.receive_fd(),
                  reinterpret_cast<struct sockaddr*>(&bound), &len);
    auto port = socket_address_parser::get_port(bound);

    data_poller.register_for_read(endpoint, endpoint, 0);

    caeron::platform::UdpSocket sender;
    std::array<std::byte, 32> frame{};
    put_frame_header(frame.data(), 0, 0x01);  // heartbeat: frame_length=0
    put_le32(frame.data() + 12, 99);   // session_id
    put_le32(frame.data() + 16, 101);  // stream_id

    (void)sender.send_to(frame.data(), 32, "127.0.0.1", port);

    auto bytes = poll_until_data(data_poller);
    EXPECT_GT(bytes, 0);

    EXPECT_EQ(endpoint.data_frame_count(), 1);
    EXPECT_EQ(endpoint.last_data_session_id(), 99);
    EXPECT_EQ(endpoint.last_data_stream_id(), 101);

    data_poller.cancel_read(endpoint, endpoint);
    endpoint.close();
}

// ISSUE-5: Epoll path -- register enough transports to exceed ITERATION_THRESHOLD (4).
TEST(DataTransportPoller, EpollPathDispatchesToCorrectEndpoint)
{
    caeron::platform::EpollPoller poller;
    DataTransportPoller data_poller(poller);

    constexpr int NUM_ENDPOINTS = 6;
    constexpr int TARGET = 3;
    std::vector<std::unique_ptr<ReceiveChannelEndpoint>> endpoints;
    std::vector<u16> ports;

    for (int i = 0; i < NUM_ENDPOINTS; ++i)
    {
        auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
        auto ep = std::make_unique<ReceiveChannelEndpoint>(ch, 0);
        ep->open_datagram_channel();

        struct sockaddr_storage bound{};
        socklen_t len = sizeof(bound);
        ::getsockname(ep->receive_fd(),
                      reinterpret_cast<struct sockaddr*>(&bound), &len);
        ports.push_back(socket_address_parser::get_port(bound));

        data_poller.register_for_read(*ep, *ep, 0);
        endpoints.push_back(std::move(ep));
    }

    caeron::platform::UdpSocket sender;
    std::array<std::byte, 32> frame{};
    put_frame_header(frame.data(), 32, 0x01);  // DATA
    put_le32(frame.data() + 12, 42);   // session_id
    put_le32(frame.data() + 16, 100);  // stream_id

    (void)sender.send_to(frame.data(), 32, "127.0.0.1", ports[TARGET]);

    auto bytes = poll_until_data(data_poller);
    EXPECT_GT(bytes, 0);

    for (int i = 0; i < NUM_ENDPOINTS; ++i)
    {
        if (i == TARGET)
        {
            EXPECT_EQ(endpoints[i]->data_frame_count(), 1);
            EXPECT_EQ(endpoints[i]->last_data_session_id(), 42);
            EXPECT_EQ(endpoints[i]->last_data_stream_id(), 100);
        }
        else
        {
            EXPECT_EQ(endpoints[i]->data_frame_count(), 0);
        }
    }

    for (auto& e : endpoints)
        e->close();
}

// ISSUE-5: Epoll path with erase -- pointer stability after erase.
TEST(DataTransportPoller, EpollPathPointerStabilityAfterErase)
{
    caeron::platform::EpollPoller poller;
    DataTransportPoller data_poller(poller);

    constexpr int NUM_ENDPOINTS = 6;
    constexpr int ERASE_IDX = 1;
    constexpr int TARGET = 4;
    std::vector<std::unique_ptr<ReceiveChannelEndpoint>> endpoints;
    std::vector<u16> ports;

    for (int i = 0; i < NUM_ENDPOINTS; ++i)
    {
        auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
        auto ep = std::make_unique<ReceiveChannelEndpoint>(ch, 0);
        ep->open_datagram_channel();

        struct sockaddr_storage bound{};
        socklen_t len = sizeof(bound);
        ::getsockname(ep->receive_fd(),
                      reinterpret_cast<struct sockaddr*>(&bound), &len);
        ports.push_back(socket_address_parser::get_port(bound));

        data_poller.register_for_read(*ep, *ep, 0);
        endpoints.push_back(std::move(ep));
    }

    data_poller.cancel_read(*endpoints[ERASE_IDX], *endpoints[ERASE_IDX]);
    endpoints[ERASE_IDX]->close();

    caeron::platform::UdpSocket sender;
    std::array<std::byte, 32> frame{};
    put_frame_header(frame.data(), 32, 0x01);  // DATA
    put_le32(frame.data() + 12, 77);  // session_id
    put_le32(frame.data() + 16, 88);  // stream_id

    (void)sender.send_to(frame.data(), 32, "127.0.0.1", ports[TARGET]);

    auto bytes = poll_until_data(data_poller);
    EXPECT_GT(bytes, 0);

    for (int i = 0; i < NUM_ENDPOINTS; ++i)
    {
        if (i == ERASE_IDX)
            continue;
        if (i == TARGET)
        {
            EXPECT_EQ(endpoints[i]->data_frame_count(), 1);
            EXPECT_EQ(endpoints[i]->last_data_session_id(), 77);
        }
        else
        {
            EXPECT_EQ(endpoints[i]->data_frame_count(), 0);
        }
    }

    for (int i = 0; i < NUM_ENDPOINTS; ++i)
    {
        if (i != ERASE_IDX)
            endpoints[i]->close();
    }
}

// ISSUE-6: Negative coverage -- truncated DATA frame (< 32 bytes) should not dispatch.
TEST(DataTransportPoller, RejectsTruncatedDataFrame)
{
    caeron::platform::EpollPoller poller;
    DataTransportPoller data_poller(poller);

    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    ReceiveChannelEndpoint endpoint(ch, 0);
    endpoint.open_datagram_channel();

    struct sockaddr_storage bound{};
    socklen_t len = sizeof(bound);
    ::getsockname(endpoint.receive_fd(),
                  reinterpret_cast<struct sockaddr*>(&bound), &len);
    auto port = socket_address_parser::get_port(bound);

    data_poller.register_for_read(endpoint, endpoint, 0);

    caeron::platform::UdpSocket sender;
    std::array<std::byte, 16> frame{};
    put_frame_header(frame.data(), 16, 0x01);  // DATA, but only 16 bytes

    (void)sender.send_to(frame.data(), 16, "127.0.0.1", port);

    (void)poll_until_data(data_poller, 100);
    EXPECT_EQ(endpoint.data_frame_count(), 0);

    data_poller.cancel_read(endpoint, endpoint);
    endpoint.close();
}

// ISSUE-6: Negative coverage -- truncated SETUP frame (< 40 bytes) should not dispatch.
TEST(DataTransportPoller, RejectsTruncatedSetupFrame)
{
    caeron::platform::EpollPoller poller;
    DataTransportPoller data_poller(poller);

    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    ReceiveChannelEndpoint endpoint(ch, 0);
    endpoint.open_datagram_channel();

    struct sockaddr_storage bound{};
    socklen_t len = sizeof(bound);
    ::getsockname(endpoint.receive_fd(),
                  reinterpret_cast<struct sockaddr*>(&bound), &len);
    auto port = socket_address_parser::get_port(bound);

    data_poller.register_for_read(endpoint, endpoint, 0);

    caeron::platform::UdpSocket sender;
    std::array<std::byte, 24> frame{};
    put_frame_header(frame.data(), 24, 0x05);  // SETUP, but only 24 bytes

    (void)sender.send_to(frame.data(), 24, "127.0.0.1", port);

    (void)poll_until_data(data_poller, 100);
    EXPECT_EQ(endpoint.setup_frame_count(), 0);

    data_poller.cancel_read(endpoint, endpoint);
    endpoint.close();
}

// ISSUE-6: Negative coverage -- truncated RTTM frame (< 40 bytes) should not dispatch.
TEST(DataTransportPoller, RejectsTruncatedRttmFrame)
{
    caeron::platform::EpollPoller poller;
    DataTransportPoller data_poller(poller);

    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    ReceiveChannelEndpoint endpoint(ch, 0);
    endpoint.open_datagram_channel();

    struct sockaddr_storage bound{};
    socklen_t len = sizeof(bound);
    ::getsockname(endpoint.receive_fd(),
                  reinterpret_cast<struct sockaddr*>(&bound), &len);
    auto port = socket_address_parser::get_port(bound);

    data_poller.register_for_read(endpoint, endpoint, 0);

    caeron::platform::UdpSocket sender;
    std::array<std::byte, 20> frame{};
    put_frame_header(frame.data(), 20, 0x06);  // RTTM, but only 20 bytes

    (void)sender.send_to(frame.data(), 20, "127.0.0.1", port);

    (void)poll_until_data(data_poller, 100);
    EXPECT_EQ(endpoint.rtt_frame_count(), 0);

    data_poller.cancel_read(endpoint, endpoint);
    endpoint.close();
}

// ISSUE-6: Frame with invalid version byte should not dispatch.
TEST(DataTransportPoller, RejectsFrameWithInvalidVersion)
{
    caeron::platform::EpollPoller poller;
    DataTransportPoller data_poller(poller);

    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    ReceiveChannelEndpoint endpoint(ch, 0);
    endpoint.open_datagram_channel();

    struct sockaddr_storage bound{};
    socklen_t len = sizeof(bound);
    ::getsockname(endpoint.receive_fd(),
                  reinterpret_cast<struct sockaddr*>(&bound), &len);
    auto port = socket_address_parser::get_port(bound);

    data_poller.register_for_read(endpoint, endpoint, 0);

    caeron::platform::UdpSocket sender;
    std::array<std::byte, 32> frame{};
    put_frame_header(frame.data(), 32, 0x01);  // DATA
    frame[4] = std::byte{0xFF};  // INVALID version

    (void)sender.send_to(frame.data(), 32, "127.0.0.1", port);

    (void)poll_until_data(data_poller, 100);
    EXPECT_EQ(endpoint.data_frame_count(), 0);

    data_poller.cancel_read(endpoint, endpoint);
    endpoint.close();
}

// ISSUE-6: Frame with negative frame_length should not dispatch.
TEST(DataTransportPoller, RejectsFrameWithNegativeFrameLength)
{
    caeron::platform::EpollPoller poller;
    DataTransportPoller data_poller(poller);

    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    ReceiveChannelEndpoint endpoint(ch, 0);
    endpoint.open_datagram_channel();

    struct sockaddr_storage bound{};
    socklen_t len = sizeof(bound);
    ::getsockname(endpoint.receive_fd(),
                  reinterpret_cast<struct sockaddr*>(&bound), &len);
    auto port = socket_address_parser::get_port(bound);

    data_poller.register_for_read(endpoint, endpoint, 0);

    caeron::platform::UdpSocket sender;
    std::array<std::byte, 32> frame{};
    put_frame_header(frame.data(), -1, 0x01);  // negative frame_length

    (void)sender.send_to(frame.data(), 32, "127.0.0.1", port);

    (void)poll_until_data(data_poller, 100);
    EXPECT_EQ(endpoint.data_frame_count(), 0);

    data_poller.cancel_read(endpoint, endpoint);
    endpoint.close();
}

// ISSUE-6: Frame with frame_length > datagram size should not dispatch.
TEST(DataTransportPoller, RejectsFrameWithLengthExceedingDatagram)
{
    caeron::platform::EpollPoller poller;
    DataTransportPoller data_poller(poller);

    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    ReceiveChannelEndpoint endpoint(ch, 0);
    endpoint.open_datagram_channel();

    struct sockaddr_storage bound{};
    socklen_t len = sizeof(bound);
    ::getsockname(endpoint.receive_fd(),
                  reinterpret_cast<struct sockaddr*>(&bound), &len);
    auto port = socket_address_parser::get_port(bound);

    data_poller.register_for_read(endpoint, endpoint, 0);

    caeron::platform::UdpSocket sender;
    std::array<std::byte, 16> frame{};
    put_frame_header(frame.data(), 9999, 0x01);  // claims 9999 bytes, only 16 sent

    (void)sender.send_to(frame.data(), 16, "127.0.0.1", port);

    (void)poll_until_data(data_poller, 100);
    EXPECT_EQ(endpoint.data_frame_count(), 0);

    data_poller.cancel_read(endpoint, endpoint);
    endpoint.close();
}
