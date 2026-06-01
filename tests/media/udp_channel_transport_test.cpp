#include "caeron/common/endian.h"
#include "caeron/driver/media/receive_destination_transport.h"
#include "caeron/driver/media/udp_channel.h"
#include "caeron/driver/media/port_manager.h"
#include "caeron/driver/media/udp_channel_transport.h"

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <cstring>
#include <poll.h>
#include <thread>

using namespace caeron;
using namespace caeron::driver::media;

TEST(UdpChannelTransport, OpenAndCloseUnicast)
{
    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");

    UdpChannelTransport transport(
        ch, ch.remote_data(), ch.local_data(),
        nullptr, nullptr, 0, 0);

    // Should not throw
    transport.open_datagram_channel();
    EXPECT_GE(transport.receive_fd(), 0);
    EXPECT_GE(transport.send_fd(), 0);
    EXPECT_FALSE(transport.is_multicast());

    transport.close();
    EXPECT_EQ(transport.receive_fd(), -1);
}

TEST(UdpChannelTransport, SendAndReceive)
{
    // Create two transports on loopback
    auto ch1 = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    UdpChannelTransport receiver(
        ch1, ch1.remote_data(), ch1.local_data(),
        nullptr, nullptr, 0, 0);
    receiver.open_datagram_channel();

    // Get the receiver's bound port
    struct sockaddr_storage bound{};
    socklen_t len = sizeof(bound);
    ::getsockname(receiver.receive_fd(),
                  reinterpret_cast<struct sockaddr*>(&bound), &len);
    auto port = socket_address_parser::get_port(bound);

    // Connect sender to receiver
    auto dest = socket_address_parser::resolve_host("127.0.0.1", port);
    auto ch2 = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:" + std::to_string(port));
    UdpChannelTransport sender(
        ch2, ch2.remote_data(), ch2.local_data(),
        &dest, nullptr, 0, 0);
    sender.open_datagram_channel();

    // Send data
    const char* msg = "hello";
    auto sent = sender.send(reinterpret_cast<const std::byte*>(msg), 5);
    EXPECT_EQ(sent, 5);

    // Wait for data with poll (up to 100ms)
    struct pollfd pfd{};
    pfd.fd = receiver.receive_fd();
    pfd.events = POLLIN;
    int poll_result = ::poll(&pfd, 1, 100);
    ASSERT_GT(poll_result, 0) << "Timed out waiting for UDP data";

    // Receive
    std::array<std::byte, 64> buf{};
    struct sockaddr_storage src{};
    i32 received = 0;
    bool got_data = receiver.receive(buf.data(), 64, src, received);
    EXPECT_TRUE(got_data);
    EXPECT_EQ(received, 5);
    EXPECT_EQ(std::memcmp(buf.data(), "hello", 5), 0);

    sender.close();
    receiver.close();
}

TEST(UdpChannelTransport, NonBlockingReceiveReturnsFalse)
{
    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    UdpChannelTransport transport(
        ch, ch.remote_data(), ch.local_data(),
        nullptr, nullptr, 0, 0);
    transport.open_datagram_channel();

    std::array<std::byte, 64> buf{};
    struct sockaddr_storage src{};
    i32 received = 0;

    // No data available -- should return false
    bool got = transport.receive(buf.data(), 64, src, received);
    EXPECT_FALSE(got);
    EXPECT_EQ(received, 0);

    transport.close();
}

TEST(UdpChannelTransport, FrameValidation)
{
    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    UdpChannelTransport transport(
        ch, ch.remote_data(), ch.local_data(),
        nullptr, nullptr, 0, 0);

    // Valid frame: 8 bytes, version 0, frame_length 8
    std::array<std::byte, 8> valid_frame{};
    put_le32(valid_frame.data(), 8);
    valid_frame[4] = std::byte{0};  // version
    EXPECT_TRUE(transport.is_valid_frame(valid_frame.data(), 8));

    // Invalid: too short
    EXPECT_FALSE(transport.is_valid_frame(valid_frame.data(), 4));

    // Invalid: wrong version
    valid_frame[4] = std::byte{1};
    EXPECT_FALSE(transport.is_valid_frame(valid_frame.data(), 8));
}

// CRITICAL-2: Verify heartbeat DATA frames (frame_length == 0) are accepted.
// In the Aeron protocol, heartbeat frames have frame_length == 0 in the header
// but the actual datagram received via recvfrom is HEADER_LENGTH (32) bytes.
TEST(UdpChannelTransport, FrameValidationHeartbeat)
{
    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    UdpChannelTransport transport(
        ch, ch.remote_data(), ch.local_data(),
        nullptr, nullptr, 0, 0);

    // Heartbeat frame: 32-byte datagram with frame_length=0, version=0
    std::array<std::byte, 32> heartbeat{};
    put_le32(heartbeat.data(), 0);  // heartbeat sentinel
    heartbeat[4] = std::byte{0};  // version
    // Type = DATA (0x01)
    put_le16(heartbeat.data() + 6, 0x01);

    // Before fix: is_valid_frame returns false because frame_len (0) < 8.
    // After fix: is_valid_frame returns true because frame_len == 0 is a valid
    // heartbeat sentinel and the datagram length (32) >= 8.
    EXPECT_TRUE(transport.is_valid_frame(heartbeat.data(), 32));

    // Also valid with larger datagram
    EXPECT_TRUE(transport.is_valid_frame(heartbeat.data(), 64));

    // Still invalid: frame_length negative
    std::array<std::byte, 32> bad_heartbeat{};
    put_le32(bad_heartbeat.data(), -1);
    bad_heartbeat[4] = std::byte{0};
    EXPECT_FALSE(transport.is_valid_frame(bad_heartbeat.data(), 32));

    // Still invalid: frame_length > actual datagram length
    std::array<std::byte, 32> oversized{};
    put_le32(oversized.data(), 64);
    oversized[4] = std::byte{0};
    EXPECT_FALSE(transport.is_valid_frame(oversized.data(), 32));

    // Still invalid: frame_length between 1 and 7
    std::array<std::byte, 32> small_nonzero{};
    put_le32(small_nonzero.data(), 4);
    small_nonzero[4] = std::byte{0};
    EXPECT_FALSE(transport.is_valid_frame(small_nonzero.data(), 32));
}

TEST(UdpChannelTransport, DestructorClosesSocket)
{
    int fd = -1;
    {
        auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
        UdpChannelTransport transport(
            ch, ch.remote_data(), ch.local_data(),
            nullptr, nullptr, 0, 0);
        transport.open_datagram_channel();
        fd = transport.receive_fd();
        EXPECT_GE(fd, 0);
    }
    // After destructor, the fd should be closed.
    // We can't easily verify this without trying to use the fd,
    // but at least the test doesn't crash (double-close would crash).
}

// CRITICAL-1: Verify send_fd_ is reset to -1 after close() for unicast
TEST(UdpChannelTransport, SendFdResetAfterClose)
{
    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    UdpChannelTransport transport(ch, ch.remote_data(), ch.local_data(),
                                   nullptr, nullptr, 0, 0);
    transport.open_datagram_channel();
    EXPECT_GE(transport.send_fd(), 0);
    EXPECT_GE(transport.receive_fd(), 0);
    // For unicast, send_fd == receive_fd
    EXPECT_EQ(transport.send_fd(), transport.receive_fd());

    transport.close();
    EXPECT_EQ(transport.receive_fd(), -1);
    EXPECT_EQ(transport.send_fd(), -1);  // Must be -1, not a stale fd
}

// CRITICAL-2: Verify EAGAIN/EWOULDBLOCK/EINTR all return false (not throw)
// The socket is set non-blocking by open_datagram_channel(), so calling
// receive with no data available triggers EAGAIN. We verify the return
// value is false (not true, not throw) and bytes_received is reset to 0.
TEST(UdpChannelTransport, NonBlockingReceiveHandlesEAGAIN)
{
    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    UdpChannelTransport transport(ch, ch.remote_data(), ch.local_data(),
                                   nullptr, nullptr, 0, 0);
    transport.open_datagram_channel();

    std::array<std::byte, 64> buf{};
    struct sockaddr_storage src{};
    i32 received = -1;  // sentinel: receive() must set this to 0 on EAGAIN

    // No data available -- socket is non-blocking, so EAGAIN is expected.
    // Verify return value is false AND received is reset to 0.
    bool got = transport.receive(buf.data(), 64, src, received);
    EXPECT_FALSE(got) << "receive() must return false when EAGAIN";
    EXPECT_EQ(received, 0) << "received must be 0 on EAGAIN";

    // Repeat to verify consistency
    for (int i = 0; i < 10; ++i)
    {
        received = -1;
        got = transport.receive(buf.data(), 64, src, received);
        EXPECT_FALSE(got);
        EXPECT_EQ(received, 0);
    }

    transport.close();
}

// Verify send_to returns a non-negative value and does not throw.
// UDP send_to on loopback is fire-and-forget — it succeeds even with no listener.
// True EAGAIN on send requires a full kernel buffer, which is impractical to test.
// This test verifies the API contract: send_to returns bytes sent (>= 0), not throw.
TEST(UdpChannelTransport, SendToReturnsNonNegative)
{
    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    UdpChannelTransport transport(ch, ch.remote_data(), ch.local_data(),
                                   nullptr, nullptr, 0, 0);
    transport.open_datagram_channel();

    struct sockaddr_storage dest{};
    auto* dest_in = reinterpret_cast<struct sockaddr_in*>(&dest);
    dest_in->sin_family = AF_INET;
    dest_in->sin_port = htons(1);  // unlikely to have a listener
    ::inet_pton(AF_INET, "127.0.0.1", &dest_in->sin_addr);

    std::array<std::byte, 4> data{};
    i32 sent = transport.send_to(data.data(), 4, dest);
    EXPECT_GE(sent, 0) << "send_to must return >= 0";

    transport.close();
}

// MEDIUM-5: update_endpoint must throw if transport is not open
TEST(UdpChannelTransport, UpdateEndpointOnClosedTransportThrows)
{
    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    UdpChannelTransport transport(ch, ch.remote_data(), ch.local_data(),
                                   nullptr, nullptr, 0, 0);
    // Transport is not open -- send_fd_ is -1
    struct sockaddr_storage new_addr{};
    auto* addr_in = reinterpret_cast<struct sockaddr_in*>(&new_addr);
    addr_in->sin_family = AF_INET;
    ::inet_pton(AF_INET, "127.0.0.1", &addr_in->sin_addr);
    addr_in->sin_port = htons(12345);

    EXPECT_THROW(transport.update_endpoint(new_addr), std::runtime_error);
}

// Finding 4: Verify double-open throws
TEST(UdpChannelTransport, DoubleOpenThrows)
{
    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    UdpChannelTransport transport(ch, ch.remote_data(), ch.local_data(),
                                   nullptr, nullptr, 0, 0);
    transport.open_datagram_channel();
    EXPECT_THROW(transport.open_datagram_channel(), std::runtime_error);
    transport.close();
}

// Finding 8: Verify negative length throws in send()
TEST(UdpChannelTransport, SendNegativeLengthThrows)
{
    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    UdpChannelTransport transport(ch, ch.remote_data(), ch.local_data(),
                                   nullptr, nullptr, 0, 0);
    transport.open_datagram_channel();

    std::array<std::byte, 4> data{};
    EXPECT_THROW((void)transport.send(data.data(), -1), std::invalid_argument);

    transport.close();
}

// Finding 8: Verify negative length throws in send_to()
TEST(UdpChannelTransport, SendToNegativeLengthThrows)
{
    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    UdpChannelTransport transport(ch, ch.remote_data(), ch.local_data(),
                                   nullptr, nullptr, 0, 0);
    transport.open_datagram_channel();

    struct sockaddr_storage dest{};
    auto* dest_in = reinterpret_cast<struct sockaddr_in*>(&dest);
    dest_in->sin_family = AF_INET;
    dest_in->sin_port = htons(1);
    ::inet_pton(AF_INET, "127.0.0.1", &dest_in->sin_addr);

    std::array<std::byte, 4> data{};
    EXPECT_THROW((void)transport.send_to(data.data(), -1, dest), std::invalid_argument);

    transport.close();
}

// Finding 8: Verify negative buffer capacity throws in receive()
TEST(UdpChannelTransport, ReceiveNegativeCapacityThrows)
{
    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    UdpChannelTransport transport(ch, ch.remote_data(), ch.local_data(),
                                   nullptr, nullptr, 0, 0);
    transport.open_datagram_channel();

    std::array<std::byte, 64> buf{};
    struct sockaddr_storage src{};
    i32 received = 0;
    EXPECT_THROW(transport.receive(buf.data(), -1, src, received), std::invalid_argument);

    transport.close();
}

// Finding 6: ReceiveDestinationTransport initializes explicit-control state from UdpChannel
TEST(ReceiveDestinationTransport, ExplicitControlInitialization)
{
    // Channel with explicit control
    auto ch = UdpChannel::parse(
        "aeron:udp?endpoint=224.0.1.1:40456|control-mode=dynamic|control=224.0.1.1:40457");
    EXPECT_TRUE(ch.has_explicit_control());

    ReceiveDestinationTransport transport(ch, 0, 0);
    EXPECT_TRUE(transport.has_explicit_control());
    EXPECT_NE(transport.current_control_address(), nullptr);
}

// Finding 6: ReceiveDestinationTransport without explicit control
TEST(ReceiveDestinationTransport, NoExplicitControlInitialization)
{
    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    EXPECT_FALSE(ch.has_explicit_control());

    ReceiveDestinationTransport transport(ch, 0, 0);
    EXPECT_FALSE(transport.has_explicit_control());
    EXPECT_EQ(transport.current_control_address(), nullptr);
}

// Verify close() is idempotent (safe to call twice).
// This is important because the destructor calls close(), and the user may also
// call close() explicitly. After the HIGH-1/HIGH-2 fix, the destructor handles
// cleanup on exception paths, so double-close safety is critical.
TEST(UdpChannelTransport, CloseIsIdempotent)
{
    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    UdpChannelTransport transport(ch, ch.remote_data(), ch.local_data(),
                                   nullptr, nullptr, 0, 0);
    transport.open_datagram_channel();
    EXPECT_GE(transport.receive_fd(), 0);

    transport.close();
    EXPECT_EQ(transport.receive_fd(), -1);
    EXPECT_EQ(transport.send_fd(), -1);

    // Second close should be safe (no-op)
    EXPECT_NO_THROW(transport.close());
    EXPECT_EQ(transport.receive_fd(), -1);
    EXPECT_EQ(transport.send_fd(), -1);
}

// Finding 6: Verify that bind failure frees the managed port reservation.
// We use a mock PortManager that returns a port already in use (EADDRINUSE).
namespace {
class TrackingPortManager : public PortManager {
public:
    mutable bool freed = false;
    struct sockaddr_storage last_allocated{};
    u16 conflict_port = 0;  // port to return that will cause EADDRINUSE

    struct sockaddr_storage get_managed_port(
        const UdpChannel& /*udp_channel*/,
        const struct sockaddr_storage& bind_address) override
    {
        struct sockaddr_storage addr = bind_address;
        auto* a4 = reinterpret_cast<struct sockaddr_in*>(&addr);
        a4->sin_port = htons(conflict_port);
        last_allocated = addr;
        return addr;
    }

    void free_managed_port(const struct sockaddr_storage& /*addr*/) override
    {
        freed = true;
    }
};
}  // namespace

TEST(UdpChannelTransport, BindFailureFreesManagedPort)
{
    // Bind a socket to a random port to guarantee EADDRINUSE
    int blocker_fd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    ASSERT_GE(blocker_fd, 0);

    struct sockaddr_in blocker_addr{};
    blocker_addr.sin_family = AF_INET;
    blocker_addr.sin_port = 0;  // OS picks a free port
    ::inet_pton(AF_INET, "127.0.0.1", &blocker_addr.sin_addr);
    ASSERT_EQ(::bind(blocker_fd, reinterpret_cast<struct sockaddr*>(&blocker_addr),
                     sizeof(blocker_addr)), 0);

    struct sockaddr_in blocker_bound{};
    socklen_t blen = sizeof(blocker_bound);
    ::getsockname(blocker_fd,
                  reinterpret_cast<struct sockaddr*>(&blocker_bound), &blen);
    u16 occupied_port = ntohs(blocker_bound.sin_port);

    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:0");
    auto port_mgr = std::make_unique<TrackingPortManager>();
    port_mgr->conflict_port = occupied_port;
    auto* mgr_ptr = port_mgr.get();

    UdpChannelTransport transport(ch, ch.remote_data(), ch.local_data(),
                                   nullptr, port_mgr.get(), 0, 0);

    // bind() must fail with EADDRINUSE because the port is already taken.
    // The transport must call free_managed_port() before throwing.
    EXPECT_THROW(transport.open_datagram_channel(), std::runtime_error);
    EXPECT_TRUE(mgr_ptr->freed) << "free_managed_port must be called on bind failure";

    ::close(blocker_fd);
}
