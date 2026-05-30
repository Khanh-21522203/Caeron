#include "udp_socket.h"

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <stdexcept>

namespace caeron::platform {

UdpSocket::UdpSocket()
{
    fd_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd_ < 0)
        throw std::runtime_error("Failed to create UDP socket");
}

UdpSocket::~UdpSocket()
{
    close();
}

UdpSocket::UdpSocket(UdpSocket&& other) noexcept
    : fd_{other.fd_}
{
    other.fd_ = -1;
}

UdpSocket& UdpSocket::operator=(UdpSocket&& other) noexcept
{
    if (this != &other)
    {
        close();
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

void UdpSocket::bind(const std::string& address, u16 port)
{
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    ::inet_pton(AF_INET, address.c_str(), &addr.sin_addr);

    if (::bind(fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0)
        throw std::runtime_error("Failed to bind UDP socket");
}

void UdpSocket::connect(const std::string& address, u16 port)
{
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    ::inet_pton(AF_INET, address.c_str(), &addr.sin_addr);

    if (::connect(fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0)
        throw std::runtime_error("Failed to connect UDP socket");
}

i32 UdpSocket::send_to(const void* data, i32 length,
                        const std::string& address, u16 port)
{
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    ::inet_pton(AF_INET, address.c_str(), &addr.sin_addr);

    auto sent = ::sendto(fd_, data, static_cast<size_t>(length), 0,
                         reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    return static_cast<i32>(sent);
}

i32 UdpSocket::receive_from(void* buffer, i32 max_length,
                             std::string& from_address, u16& from_port)
{
    struct sockaddr_in addr{};
    socklen_t addr_len = sizeof(addr);

    auto received = ::recvfrom(fd_, buffer, static_cast<size_t>(max_length), 0,
                               reinterpret_cast<struct sockaddr*>(&addr), &addr_len);
    if (received < 0)
        return static_cast<i32>(received);

    char ip[INET_ADDRSTRLEN];
    ::inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
    from_address = ip;
    from_port = ntohs(addr.sin_port);

    return static_cast<i32>(received);
}

void UdpSocket::set_sndbuf(i32 size)
{
    ::setsockopt(fd_, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size));
}

void UdpSocket::set_rcvbuf(i32 size)
{
    ::setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
}

void UdpSocket::join_multicast(const std::string& group, const std::string& iface)
{
    struct ip_mreq mreq{};
    ::inet_pton(AF_INET, group.c_str(), &mreq.imr_multiaddr);
    if (!iface.empty())
        ::inet_pton(AF_INET, iface.c_str(), &mreq.imr_interface);
    ::setsockopt(fd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
}

void UdpSocket::leave_multicast(const std::string& group, const std::string& iface)
{
    struct ip_mreq mreq{};
    ::inet_pton(AF_INET, group.c_str(), &mreq.imr_multiaddr);
    if (!iface.empty())
        ::inet_pton(AF_INET, iface.c_str(), &mreq.imr_interface);
    ::setsockopt(fd_, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq));
}

void UdpSocket::close()
{
    if (fd_ >= 0)
    {
        ::close(fd_);
        fd_ = -1;
    }
}

} // namespace caeron::platform
