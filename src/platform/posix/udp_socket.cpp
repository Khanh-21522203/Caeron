#include "udp_socket.h"

#include <cstring>
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
    if (::inet_pton(AF_INET, address.c_str(), &addr.sin_addr) <= 0)
        throw std::runtime_error("Invalid address for bind: " + address);

    if (::bind(fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0)
        throw std::runtime_error("Failed to bind UDP socket");
}

void UdpSocket::connect(const std::string& address, u16 port)
{
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (::inet_pton(AF_INET, address.c_str(), &addr.sin_addr) <= 0)
        throw std::runtime_error("Invalid address for connect: " + address);

    if (::connect(fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0)
        throw std::runtime_error("Failed to connect UDP socket");
}

i32 UdpSocket::send_to(const void* data, i32 length,
                        const std::string& address, u16 port)
{
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (::inet_pton(AF_INET, address.c_str(), &addr.sin_addr) <= 0)
        throw std::runtime_error("Invalid address for send_to: " + address);

    auto sent = ::sendto(fd_, data, static_cast<size_t>(length), 0,
                         reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    if (sent < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            return 0;
        throw std::runtime_error("sendto failed: " + std::string(std::strerror(errno)));
    }
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
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            return 0;
        return static_cast<i32>(received);
    }

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

void UdpSocket::set_busy_loop(i32 microseconds)
{
    if (::setsockopt(fd_, SOL_SOCKET, SO_BUSY_POLL,
                     &microseconds, sizeof(microseconds)) < 0)
    {
        // Not fatal — some kernels don't support it. Log and continue.
        // In production, you'd check errno == ENOPROTOOPT.
    }
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

i32 UdpSocket::send_mmsg(const std::vector<SendMsg>& messages)
{
    std::vector<sockaddr_in> addresses(messages.size());
    std::vector<iovec> iovecs(messages.size());
    std::vector<mmsghdr> mmsghdrs(messages.size());

    for (size_t i = 0; i < messages.size(); ++i)
    {
        // Fill the destination address
        auto& addr = addresses[i];
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(messages[i].port);
        ::inet_pton(AF_INET, messages[i].address.c_str(), &addr.sin_addr);

        // Fill the iovec (pointer to data + length)
        iovecs[i].iov_base = const_cast<void*>(messages[i].data);
        iovecs[i].iov_len  = static_cast<size_t>(messages[i].length);

        // Fill the mmsghdr (link address + iovec)
        auto& hdr = mmsghdrs[i];
        std::memset(&hdr, 0, sizeof(hdr));
        hdr.msg_hdr.msg_name    = &addresses[i];
        hdr.msg_hdr.msg_namelen = sizeof(addresses[i]);
        hdr.msg_hdr.msg_iov     = &iovecs[i];
        hdr.msg_hdr.msg_iovlen  = 1;
    }

    auto sent = ::sendmmsg(fd_, mmsghdrs.data(),
        static_cast<unsigned>(messages.size()), 0);
    if (sent < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            return 0;
        throw std::runtime_error("sendmmsg failed: " + std::string(std::strerror(errno)));
    }
    return sent;
}

i32 UdpSocket::recv_mmsg(std::vector<RecvMsg>& messages)
{
    std::vector<sockaddr_in> addresses(messages.size());
    std::vector<iovec> iovecs(messages.size());
    std::vector<mmsghdr> mmsghdrs(messages.size());

    for (size_t i = 0; i < messages.size(); ++i)
    {
        // iovec points to the receive buffer
        iovecs[i].iov_base = messages[i].buffer;
        iovecs[i].iov_len  = static_cast<size_t>(messages[i].buffer_length);

        // mmsghdr links the iovec + space for source address
        auto& hdr = mmsghdrs[i];
        std::memset(&hdr, 0, sizeof(hdr));
        hdr.msg_hdr.msg_name    = &addresses[i];
        hdr.msg_hdr.msg_namelen = sizeof(addresses[i]);
        hdr.msg_hdr.msg_iov     = &iovecs[i];
        hdr.msg_hdr.msg_iovlen  = 1;
    }

    // MSG_DONTWAIT = non-blocking
    auto received = ::recvmmsg(fd_, mmsghdrs.data(),
                               static_cast<unsigned>(messages.size()),
                               MSG_DONTWAIT, nullptr);
    if (received < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            return 0;
        return -1;
    }

    for (ssize_t i = 0; i < received; ++i)
    {
        messages[i].received_length = static_cast<i32>(mmsghdrs[i].msg_len);

        char ip[INET_ADDRSTRLEN];
        ::inet_ntop(AF_INET, &addresses[i].sin_addr, ip, sizeof(ip));
        messages[i].from_address = ip;
        messages[i].from_port = ntohs(addresses[i].sin_port);
    }

    return received;
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
