#include "platform/posix/udp_socket.h"

#include <gtest/gtest.h>

#include <cstring>

using namespace caeron;
using namespace caeron::platform;

TEST(UdpSocket, ConstructorCreatesSocket)
{
    UdpSocket sock;
    EXPECT_TRUE(sock.is_open());
    EXPECT_GE(sock.fd(), 0);
}

TEST(UdpSocket, BindAndSendReceive)
{
    UdpSocket receiver;
    receiver.bind("127.0.0.1", 0);  // port 0 = kernel picks a free port

    // Get the bound port via getsockname
    struct sockaddr_in bound_addr{};
    socklen_t addr_len = sizeof(bound_addr);
    ::getsockname(receiver.fd(),
                  reinterpret_cast<struct sockaddr*>(&bound_addr),
                  &addr_len);
    u16 port = ntohs(bound_addr.sin_port);

    // Send a message
    UdpSocket sender;
    const char* msg = "hello";
    auto sent = sender.send_to(msg, 5, "127.0.0.1", port);
    EXPECT_EQ(sent, 5);

    // Receive it
    char buf[64]{};
    std::string from_addr;
    u16 from_port = 0;
    auto received = receiver.receive_from(buf, sizeof(buf), from_addr, from_port);
    EXPECT_EQ(received, 5);
    EXPECT_EQ(std::string(buf, 5), "hello");
    EXPECT_EQ(from_addr, "127.0.0.1");
    EXPECT_GT(from_port, 0);
}

TEST(UdpSocket, SetBufOptions)
{
    UdpSocket sock;
    // Should not throw
    sock.set_sndbuf(1024 * 1024);
    sock.set_rcvbuf(1024 * 1024);
}

TEST(UdpSocket, SetBusyPoll)
{
    UdpSocket sock;
    // Should not throw (may silently fail on unsupported kernels)
    sock.set_busy_loop(50);
}

TEST(UdpSocket, SendMmsgRoundTrip)
{
    UdpSocket receiver;
    receiver.bind("127.0.0.1", 0);

    struct sockaddr_in bound_addr{};
    socklen_t addr_len = sizeof(bound_addr);
    ::getsockname(receiver.fd(),
                  reinterpret_cast<struct sockaddr*>(&bound_addr),
                  &addr_len);
    u16 port = ntohs(bound_addr.sin_port);

    // Send 3 messages via send_mmsg
    UdpSocket sender;
    std::string data1 = "msg1";
    std::string data2 = "msg2";
    std::string data3 = "msg3";

    std::vector<SendMsg> sends = {
        {data1.data(), static_cast<i32>(data1.size()), "127.0.0.1", port},
        {data2.data(), static_cast<i32>(data2.size()), "127.0.0.1", port},
        {data3.data(), static_cast<i32>(data3.size()), "127.0.0.1", port},
    };

    auto sent = sender.send_mmsg(sends);
    EXPECT_EQ(sent, 3);

    // Receive via recv_mmsg
    std::vector<std::byte> buf1(64), buf2(64), buf3(64);
    std::vector<RecvMsg> recvs = {
        {buf1.data(), 64, 0, "", 0},
        {buf2.data(), 64, 0, "", 0},
        {buf3.data(), 64, 0, "", 0},
    };

    auto received = receiver.recv_mmsg(recvs);
    EXPECT_EQ(received, 3);

    // Verify content (order may vary with UDP)
    EXPECT_EQ(recvs[0].received_length, 4);
    EXPECT_EQ(recvs[1].received_length, 4);
    EXPECT_EQ(recvs[2].received_length, 4);
}

TEST(UdpSocket, MoveConstruct)
{
    UdpSocket sock1;
    int fd = sock1.fd();

    UdpSocket sock2 = std::move(sock1);

    EXPECT_EQ(sock2.fd(), fd);
    EXPECT_TRUE(sock2.is_open());
    EXPECT_FALSE(sock1.is_open());
    EXPECT_EQ(sock1.fd(), -1);
}

TEST(UdpSocket, MoveAssign)
{
    UdpSocket sock1;
    UdpSocket sock2;

    int fd1 = sock1.fd();
    sock2 = std::move(sock1);

    EXPECT_EQ(sock2.fd(), fd1);
    EXPECT_FALSE(sock1.is_open());
}

TEST(UdpSocket, CloseMakesNotOpen)
{
    UdpSocket sock;
    EXPECT_TRUE(sock.is_open());
    sock.close();
    EXPECT_FALSE(sock.is_open());
    EXPECT_EQ(sock.fd(), -1);
}

TEST(UdpSocket, DoubleCloseDoesNotCrash)
{
    UdpSocket sock;
    sock.close();
    sock.close();  // should be safe
    EXPECT_FALSE(sock.is_open());
}
