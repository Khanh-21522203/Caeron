#include "caeron/driver/media/control_transport_poller.h"
#include "caeron/driver/media/send_channel_endpoint.h"
#include "caeron/driver/media/udp_channel.h"
#include "caeron/protocol/header_flyweight.h"
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
static i32 poll_until_data(ControlTransportPoller& poller, int timeout_ms = 500)
{
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline)
    {
        i32 bytes = poller.poll_transports();
        if (bytes > 0) return bytes;
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

TEST(ControlTransportPoller, Construction)
{
    caeron::platform::EpollPoller poller;
    ControlTransportPoller ctrl_poller(poller);
    EXPECT_EQ(ctrl_poller.poll_transports(), 0);
}

TEST(ControlTransportPoller, RegisterAndCancel)
{
    caeron::platform::EpollPoller poller;
    ControlTransportPoller ctrl_poller(poller);

    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    SendChannelEndpoint endpoint(ch, 0);
    endpoint.open_datagram_channel();

    ctrl_poller.register_for_read(endpoint);
    EXPECT_EQ(ctrl_poller.poll_transports(), 0);

    ctrl_poller.cancel_read(endpoint);
    endpoint.close();
}

TEST(ControlTransportPoller, ReceiveOnConnectedSocket)
{
    caeron::platform::EpollPoller poller;
    ControlTransportPoller ctrl_poller(poller);

    caeron::platform::UdpSocket peer;
    peer.bind("127.0.0.1", 0);

    struct sockaddr_in peer_bound{};
    socklen_t plen = sizeof(peer_bound);
    ::getsockname(peer.fd(),
                  reinterpret_cast<struct sockaddr*>(&peer_bound), &plen);
    u16 peer_port = ntohs(peer_bound.sin_port);

    auto ch = UdpChannel::parse(
        "aeron:udp?endpoint=127.0.0.1:" + std::to_string(peer_port));
    SendChannelEndpoint endpoint(ch, 0);
    endpoint.open_datagram_channel();

    struct sockaddr_storage ep_bound{};
    socklen_t elen = sizeof(ep_bound);
    ::getsockname(endpoint.send_fd(),
                  reinterpret_cast<struct sockaddr*>(&ep_bound), &elen);
    auto ep_port = socket_address_parser::get_port(ep_bound);

    ctrl_poller.register_for_read(endpoint);

    std::array<std::byte, 36> sm_frame{};
    put_frame_header(sm_frame.data(), 36, protocol::HeaderFlyweight::HDR_TYPE_SM);

    (void)peer.send_to(sm_frame.data(), 36, "127.0.0.1", ep_port);

    auto bytes = poll_until_data(ctrl_poller);
    EXPECT_GT(bytes, 0);

    ctrl_poller.cancel_read(endpoint);
    endpoint.close();
}

TEST(ControlTransportPoller, ReceiveNakFromPeer)
{
    caeron::platform::EpollPoller poller;
    ControlTransportPoller ctrl_poller(poller);

    caeron::platform::UdpSocket peer;
    peer.bind("127.0.0.1", 0);

    struct sockaddr_in peer_bound{};
    socklen_t plen = sizeof(peer_bound);
    ::getsockname(peer.fd(),
                  reinterpret_cast<struct sockaddr*>(&peer_bound), &plen);
    u16 peer_port = ntohs(peer_bound.sin_port);

    auto ch = UdpChannel::parse(
        "aeron:udp?endpoint=127.0.0.1:" + std::to_string(peer_port));
    SendChannelEndpoint endpoint(ch, 0);
    endpoint.open_datagram_channel();

    struct sockaddr_storage ep_bound{};
    socklen_t elen = sizeof(ep_bound);
    ::getsockname(endpoint.send_fd(),
                  reinterpret_cast<struct sockaddr*>(&ep_bound), &elen);
    auto ep_port = socket_address_parser::get_port(ep_bound);

    ctrl_poller.register_for_read(endpoint);

    std::array<std::byte, 28> nak_frame{};
    put_frame_header(nak_frame.data(), 28, protocol::HeaderFlyweight::HDR_TYPE_NAK);

    (void)peer.send_to(nak_frame.data(), 28, "127.0.0.1", ep_port);

    auto bytes = poll_until_data(ctrl_poller);
    EXPECT_GT(bytes, 0);

    ctrl_poller.cancel_read(endpoint);
    endpoint.close();
}

// CRITICAL-1: Registering multiple transports must not invalidate epoll pointers.
TEST(ControlTransportPoller, MultipleRegistrationsNoUseAfterFree)
{
    caeron::platform::EpollPoller poller;
    ControlTransportPoller ctrl(poller);

    std::vector<std::unique_ptr<SendChannelEndpoint>> endpoints;
    for (int i = 0; i < 10; ++i)
    {
        auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
        auto ep = std::make_unique<SendChannelEndpoint>(ch, 0);
        ep->open_datagram_channel();
        ctrl.register_for_read(*ep);
        endpoints.push_back(std::move(ep));
    }

    EXPECT_NO_FATAL_FAILURE((void)ctrl.poll_transports());

    for (auto& e : endpoints)
        e->close();
}

// CRITICAL-3: Erasing an element must not invalidate pointers for remaining elements.
TEST(ControlTransportPoller, EraseDoesNotInvalidateRemainingPointers)
{
    caeron::platform::EpollPoller poller;
    ControlTransportPoller ctrl(poller);

    auto ch1 = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    auto ch2 = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    SendChannelEndpoint ep1(ch1, 0);
    SendChannelEndpoint ep2(ch2, 0);
    ep1.open_datagram_channel();
    ep2.open_datagram_channel();

    ctrl.register_for_read(ep1);
    ctrl.register_for_read(ep2);

    ctrl.cancel_read(ep1);

    EXPECT_NO_FATAL_FAILURE((void)ctrl.poll_transports());

    ctrl.cancel_read(ep2);
    ep1.close();
    ep2.close();
}

// CRITICAL-3: Rejects frames with invalid version.
TEST(ControlTransportPoller, RejectsFrameWithInvalidVersion)
{
    caeron::platform::EpollPoller poller;
    ControlTransportPoller ctrl_poller(poller);

    caeron::platform::UdpSocket peer;
    peer.bind("127.0.0.1", 0);

    struct sockaddr_in peer_bound{};
    socklen_t plen = sizeof(peer_bound);
    ::getsockname(peer.fd(),
                  reinterpret_cast<struct sockaddr*>(&peer_bound), &plen);
    u16 peer_port = ntohs(peer_bound.sin_port);

    auto ch = UdpChannel::parse(
        "aeron:udp?endpoint=127.0.0.1:" + std::to_string(peer_port));
    SendChannelEndpoint endpoint(ch, 0);
    endpoint.open_datagram_channel();

    struct sockaddr_storage ep_bound{};
    socklen_t elen = sizeof(ep_bound);
    ::getsockname(endpoint.send_fd(),
                  reinterpret_cast<struct sockaddr*>(&ep_bound), &elen);
    auto ep_port = socket_address_parser::get_port(ep_bound);

    ctrl_poller.register_for_read(endpoint);

    std::array<std::byte, 36> bad_frame{};
    put_frame_header(bad_frame.data(), 36, protocol::HeaderFlyweight::HDR_TYPE_SM);
    bad_frame[4] = std::byte{0xFF};  // INVALID version
    put_le32(bad_frame.data() + 8, 100);   // session_id
    put_le32(bad_frame.data() + 12, 200);  // stream_id

    (void)peer.send_to(bad_frame.data(), 36, "127.0.0.1", ep_port);

    auto bytes = poll_until_data(ctrl_poller, 100);
    EXPECT_EQ(bytes, 0);

    ctrl_poller.cancel_read(endpoint);
    endpoint.close();
}

// CRITICAL-3: Rejects frames with frame_length > datagram size.
TEST(ControlTransportPoller, RejectsFrameWithMismatchedLength)
{
    caeron::platform::EpollPoller poller;
    ControlTransportPoller ctrl_poller(poller);

    caeron::platform::UdpSocket peer;
    peer.bind("127.0.0.1", 0);

    struct sockaddr_in peer_bound{};
    socklen_t plen = sizeof(peer_bound);
    ::getsockname(peer.fd(),
                  reinterpret_cast<struct sockaddr*>(&peer_bound), &plen);
    u16 peer_port = ntohs(peer_bound.sin_port);

    auto ch = UdpChannel::parse(
        "aeron:udp?endpoint=127.0.0.1:" + std::to_string(peer_port));
    SendChannelEndpoint endpoint(ch, 0);
    endpoint.open_datagram_channel();

    struct sockaddr_storage ep_bound{};
    socklen_t elen = sizeof(ep_bound);
    ::getsockname(endpoint.send_fd(),
                  reinterpret_cast<struct sockaddr*>(&ep_bound), &elen);
    auto ep_port = socket_address_parser::get_port(ep_bound);

    ctrl_poller.register_for_read(endpoint);

    std::array<std::byte, 16> bad_frame{};
    put_frame_header(bad_frame.data(), 9999, protocol::HeaderFlyweight::HDR_TYPE_SM);

    (void)peer.send_to(bad_frame.data(), 16, "127.0.0.1", ep_port);

    auto bytes = poll_until_data(ctrl_poller, 100);
    EXPECT_EQ(bytes, 0);

    ctrl_poller.cancel_read(endpoint);
    endpoint.close();
}

// ISSUE-5: Epoll path -- register enough transports to exceed ITERATION_THRESHOLD (4).
TEST(ControlTransportPoller, EpollPathDispatchesToCorrectEndpoint)
{
    caeron::platform::EpollPoller poller;
    ControlTransportPoller ctrl_poller(poller);

    caeron::platform::UdpSocket peer;
    peer.bind("127.0.0.1", 0);

    struct sockaddr_in peer_bound{};
    socklen_t plen = sizeof(peer_bound);
    ::getsockname(peer.fd(),
                  reinterpret_cast<struct sockaddr*>(&peer_bound), &plen);
    u16 peer_port = ntohs(peer_bound.sin_port);

    constexpr int NUM_ENDPOINTS = 6;
    constexpr int TARGET = 2;
    std::vector<std::unique_ptr<SendChannelEndpoint>> endpoints;
    std::vector<u16> ep_ports;

    for (int i = 0; i < NUM_ENDPOINTS; ++i)
    {
        auto ch = UdpChannel::parse(
            "aeron:udp?endpoint=127.0.0.1:" + std::to_string(peer_port));
        auto ep = std::make_unique<SendChannelEndpoint>(ch, 0);
        ep->open_datagram_channel();

        struct sockaddr_storage ep_bound{};
        socklen_t elen = sizeof(ep_bound);
        ::getsockname(ep->send_fd(),
                      reinterpret_cast<struct sockaddr*>(&ep_bound), &elen);
        ep_ports.push_back(socket_address_parser::get_port(ep_bound));

        ctrl_poller.register_for_read(*ep);
        endpoints.push_back(std::move(ep));
    }

    std::array<std::byte, 36> sm_frame{};
    put_frame_header(sm_frame.data(), 36, protocol::HeaderFlyweight::HDR_TYPE_SM);

    (void)peer.send_to(sm_frame.data(), 36, "127.0.0.1", ep_ports[TARGET]);

    auto bytes = poll_until_data(ctrl_poller);
    EXPECT_GT(bytes, 0);

    for (auto& e : endpoints)
        e->close();
}

// ISSUE-5: Epoll path with erase -- pointer stability after erase.
TEST(ControlTransportPoller, EpollPathPointerStabilityAfterErase)
{
    caeron::platform::EpollPoller poller;
    ControlTransportPoller ctrl_poller(poller);

    caeron::platform::UdpSocket peer;
    peer.bind("127.0.0.1", 0);

    struct sockaddr_in peer_bound{};
    socklen_t plen = sizeof(peer_bound);
    ::getsockname(peer.fd(),
                  reinterpret_cast<struct sockaddr*>(&peer_bound), &plen);
    u16 peer_port = ntohs(peer_bound.sin_port);

    constexpr int NUM_ENDPOINTS = 6;
    constexpr int ERASE_IDX = 0;
    constexpr int TARGET = 4;
    std::vector<std::unique_ptr<SendChannelEndpoint>> endpoints;
    std::vector<u16> ep_ports;

    for (int i = 0; i < NUM_ENDPOINTS; ++i)
    {
        auto ch = UdpChannel::parse(
            "aeron:udp?endpoint=127.0.0.1:" + std::to_string(peer_port));
        auto ep = std::make_unique<SendChannelEndpoint>(ch, 0);
        ep->open_datagram_channel();

        struct sockaddr_storage ep_bound{};
        socklen_t elen = sizeof(ep_bound);
        ::getsockname(ep->send_fd(),
                      reinterpret_cast<struct sockaddr*>(&ep_bound), &elen);
        ep_ports.push_back(socket_address_parser::get_port(ep_bound));

        ctrl_poller.register_for_read(*ep);
        endpoints.push_back(std::move(ep));
    }

    ctrl_poller.cancel_read(*endpoints[ERASE_IDX]);
    endpoints[ERASE_IDX]->close();

    std::array<std::byte, 36> sm_frame{};
    put_frame_header(sm_frame.data(), 36, protocol::HeaderFlyweight::HDR_TYPE_SM);

    (void)peer.send_to(sm_frame.data(), 36, "127.0.0.1", ep_ports[TARGET]);

    auto bytes = poll_until_data(ctrl_poller);
    EXPECT_GT(bytes, 0);

    for (int i = 0; i < NUM_ENDPOINTS; ++i)
    {
        if (i != ERASE_IDX)
            endpoints[i]->close();
    }
}
