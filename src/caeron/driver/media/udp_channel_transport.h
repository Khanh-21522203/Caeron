#pragma once

#include "caeron/common/endian.h"
#include "caeron/common/types.h"
#include "caeron/driver/media/port_manager.h"
#include "caeron/driver/media/socket_address_parser.h"
#include "caeron/driver/media/udp_channel.h"

#include <arpa/inet.h>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace caeron::driver::media {

/// Base class managing UDP socket lifecycle (open, bind, connect, close, receive).
///
/// Owns one or two raw socket file descriptors. For unicast, send_fd_ == receive_fd_.
/// For multicast, the receive socket is separate (bound with SO_REUSEADDR) from the
/// send socket (connected to the remote endpoint).
class UdpChannelTransport
{
public:
    UdpChannelTransport(
        const UdpChannel& udp_channel,
        const struct sockaddr_storage& endpoint_address,
        const struct sockaddr_storage& bind_address,
        const struct sockaddr_storage* connect_address,
        PortManager* port_manager,
        int socket_rcvbuf_length = 0,
        int socket_sndbuf_length = 0)
        : udp_channel_{udp_channel}
        , endpoint_address_{endpoint_address}
        , bind_address_{bind_address}
        , port_manager_{port_manager}
        , multicast_ttl_{udp_channel.multicast_ttl()}
        , socket_rcvbuf_length_{socket_rcvbuf_length}
        , socket_sndbuf_length_{socket_sndbuf_length}
    {
        if (connect_address)
        {
            connect_address_ = *connect_address;
            has_connect_address_ = true;
        }
    }

    virtual ~UdpChannelTransport()
    {
        close();
    }

    UdpChannelTransport(const UdpChannelTransport&) = delete;
    UdpChannelTransport& operator=(const UdpChannelTransport&) = delete;
    UdpChannelTransport(UdpChannelTransport&&) = delete;
    UdpChannelTransport& operator=(UdpChannelTransport&&) = delete;

    /// Open the UDP socket, bind, join multicast if needed, set non-blocking.
    void open_datagram_channel()
    {
        if (send_fd_ >= 0 || receive_fd_ >= 0)
            throw std::runtime_error("transport already open");

        const auto family = static_cast<int>(bind_address_.ss_family);
        const auto socklen = (family == AF_INET)
            ? sizeof(struct sockaddr_in)
            : sizeof(struct sockaddr_in6);

        // Create receive socket
        receive_fd_ = ::socket(family, SOCK_DGRAM, IPPROTO_UDP);
        if (receive_fd_ < 0)
            throw std::runtime_error("failed to create receive socket: " +
                                     std::string(std::strerror(errno)));

        // SO_REUSEADDR for multicast
        if (udp_channel_.is_multicast())
        {
            int reuse = 1;
            if (::setsockopt(receive_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
            {
                ::close(receive_fd_);
                receive_fd_ = -1;
                throw std::runtime_error("SO_REUSEADDR failed: " +
                                         std::string(std::strerror(errno)));
            }
        }

        // Bind receive socket
        auto bind_addr = bind_address_;
        if (port_manager_)
        {
            bind_addr = port_manager_->get_managed_port(udp_channel_, bind_address_);
        }

        if (::bind(receive_fd_,
                   reinterpret_cast<const struct sockaddr*>(&bind_addr),
                   socklen) < 0)
        {
            // Free the managed port reservation if bind fails
            if (port_manager_)
            {
                port_manager_->free_managed_port(bind_addr);
            }
            ::close(receive_fd_);
            receive_fd_ = -1;
            throw std::runtime_error("failed to bind receive socket: " +
                                     std::string(std::strerror(errno)));
        }

        // Store the actual bound address for use by free_managed_port() in close().
        // When port_manager_ allocated a port (or OS assigned one for port-0),
        // we need to free that actual address, not the original bind_address_.
        {
            struct sockaddr_storage actual{};
            socklen_t actual_len = sizeof(actual);
            ::getsockname(receive_fd_,
                          reinterpret_cast<struct sockaddr*>(&actual), &actual_len);
            bound_address_ = actual;
        }

        // Set receive buffer size
        if (socket_rcvbuf_length_ > 0)
            set_rcvbuf(receive_fd_, socket_rcvbuf_length_);

        // Set non-blocking
        set_nonblocking(receive_fd_);

        if (udp_channel_.is_multicast())
        {
            // Multicast: separate send socket
            send_fd_ = ::socket(family, SOCK_DGRAM, IPPROTO_UDP);
            if (send_fd_ < 0)
            {
                // Clean up managed port reservation before closing receive socket
                if (port_manager_)
                    port_manager_->free_managed_port(bound_address_);
                ::close(receive_fd_);
                receive_fd_ = -1;
                throw std::runtime_error("failed to create send socket: " +
                                         std::string(std::strerror(errno)));
            }

            // Set send buffer size
            if (socket_sndbuf_length_ > 0)
                set_sndbuf(send_fd_, socket_sndbuf_length_);

            // Set multicast TTL
            if (multicast_ttl_ > 0)
            {
                if (family == AF_INET)
                {
                    if (::setsockopt(send_fd_, IPPROTO_IP, IP_MULTICAST_TTL,
                                     &multicast_ttl_, sizeof(multicast_ttl_)) < 0)
                    {
                        // Non-fatal: log warning but continue
                    }
                }
                else if (family == AF_INET6)
                {
                    if (::setsockopt(send_fd_, IPPROTO_IPV6, IPV6_MULTICAST_HOPS,
                                     &multicast_ttl_, sizeof(multicast_ttl_)) < 0)
                    {
                        // Non-fatal: log warning but continue
                    }
                }
            }

            // Join multicast group on receive socket
            join_multicast();

            // Connect send socket to endpoint
            if (has_connect_address_)
            {
                if (::connect(send_fd_,
                              reinterpret_cast<const struct sockaddr*>(&connect_address_),
                              socklen) < 0)
                {
                    // Constructor is throwing — destructor won't run. Clean up manually.
                    leave_multicast();
                    if (port_manager_)
                        port_manager_->free_managed_port(bound_address_);
                    ::close(send_fd_);
                    send_fd_ = -1;
                    ::close(receive_fd_);
                    receive_fd_ = -1;
                    throw std::runtime_error("failed to connect send socket: " +
                                             std::string(std::strerror(errno)));
                }
            }

            set_nonblocking(send_fd_);
        }
        else
        {
            // Unicast: same socket for send and receive
            send_fd_ = receive_fd_;

            // Connect to remote endpoint
            if (has_connect_address_)
            {
                if (::connect(send_fd_,
                              reinterpret_cast<const struct sockaddr*>(&connect_address_),
                              socklen) < 0)
                {
                    // Don't manually close FDs -- let the destructor's close()
                    // handle free_managed_port() properly.
                    throw std::runtime_error("failed to connect socket: " +
                                             std::string(std::strerror(errno)));
                }
            }

            // Set send buffer size
            if (socket_sndbuf_length_ > 0)
                set_sndbuf(send_fd_, socket_sndbuf_length_);
        }
    }

    /// Close sockets and free managed port.
    void close()
    {
        if (send_fd_ >= 0 && send_fd_ != receive_fd_)
        {
            ::close(send_fd_);
        }
        if (receive_fd_ >= 0)
        {
            if (udp_channel_.is_multicast())
                leave_multicast();

            ::close(receive_fd_);

            if (port_manager_)
                port_manager_->free_managed_port(bound_address_);
        }
        send_fd_ = -1;
        receive_fd_ = -1;
    }

    /// Receive one datagram. Returns false if no data available (EAGAIN/EWOULDBLOCK).
    /// Populates src_address with the source. Sets bytes_received.
    bool receive(std::byte* buffer, i32 buffer_capacity,
                 struct sockaddr_storage& src_address, i32& bytes_received)
    {
        if (buffer_capacity < 0)
            throw std::invalid_argument("negative buffer capacity");

        socklen_t addr_len = sizeof(src_address);
        std::memset(&src_address, 0, sizeof(src_address));

        auto received = ::recvfrom(receive_fd_, buffer, static_cast<size_t>(buffer_capacity),
                                   0, reinterpret_cast<struct sockaddr*>(&src_address), &addr_len);

        if (received < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            {
                bytes_received = 0;
                return false;
            }
            throw std::runtime_error("recvfrom failed: " +
                                     std::string(std::strerror(errno)));
        }

        bytes_received = static_cast<i32>(received);
        return true;
    }

    /// Send to connected address.
    [[nodiscard]] i32 send(const std::byte* data, i32 length)
    {
        if (length < 0)
            throw std::invalid_argument("negative length");

        auto sent = ::send(send_fd_, data, static_cast<size_t>(length), 0);
        if (sent < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
                return 0;
            throw std::runtime_error("send failed: " +
                                     std::string(std::strerror(errno)));
        }
        return static_cast<i32>(sent);
    }

    /// Send to explicit address.
    [[nodiscard]] i32 send_to(const std::byte* data, i32 length,
                               const struct sockaddr_storage& dest_address)
    {
        if (length < 0)
            throw std::invalid_argument("negative length");

        const auto socklen = (dest_address.ss_family == AF_INET)
            ? sizeof(struct sockaddr_in)
            : sizeof(struct sockaddr_in6);

        auto sent = ::sendto(send_fd_, data, static_cast<size_t>(length), 0,
                             reinterpret_cast<const struct sockaddr*>(&dest_address),
                             socklen);
        if (sent < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
                return 0;
            throw std::runtime_error("sendto failed: " +
                                     std::string(std::strerror(errno)));
        }
        return static_cast<i32>(sent);
    }

    /// Re-connect to a new endpoint address.
    void update_endpoint(const struct sockaddr_storage& new_address)
    {
        if (send_fd_ < 0)
            throw std::runtime_error("update_endpoint: transport not open");

        const auto socklen = (new_address.ss_family == AF_INET)
            ? sizeof(struct sockaddr_in)
            : sizeof(struct sockaddr_in6);

        if (::connect(send_fd_,
                      reinterpret_cast<const struct sockaddr*>(&new_address),
                      socklen) < 0)
        {
            throw std::runtime_error("update_endpoint connect failed: " +
                                     std::string(std::strerror(errno)));
        }
        connect_address_ = new_address;
        has_connect_address_ = true;
    }

    /// Validate that a received frame has correct version and minimum length.
    [[nodiscard]] bool is_valid_frame(const std::byte* buffer, i32 length) const
    {
        if (length < 8)
            return false;

        // Check frame version at offset 4
        const auto version = static_cast<u8>(buffer[4]);
        if (version != 0x0)
            return false;

        // Check frame length fits in the received data (explicit little-endian)
        const i32 frame_len = get_le32(buffer);
        // frame_len == 0 is a valid heartbeat sentinel (Aeron protocol).
        // frame_len > 0 must be at least 8 (minimum header) and fit in the datagram.
        if (frame_len < 0 || frame_len > length)
            return false;
        if (frame_len > 0 && frame_len < 8)
            return false;

        return true;
    }

    // Accessors
    [[nodiscard]] const UdpChannel& udp_channel() const noexcept { return udp_channel_; }
    [[nodiscard]] int receive_fd() const noexcept { return receive_fd_; }
    [[nodiscard]] int send_fd() const noexcept { return send_fd_; }
    [[nodiscard]] bool is_multicast() const noexcept { return udp_channel_.is_multicast(); }
    [[nodiscard]] int multicast_ttl() const noexcept { return multicast_ttl_; }
    [[nodiscard]] int socket_sndbuf_length() const noexcept { return socket_sndbuf_length_; }
    [[nodiscard]] int socket_rcvbuf_length() const noexcept { return socket_rcvbuf_length_; }

    [[nodiscard]] std::string bind_address_and_port() const
    {
        struct sockaddr_storage bound{};
        socklen_t len = sizeof(bound);
        if (receive_fd_ >= 0)
            ::getsockname(receive_fd_, reinterpret_cast<struct sockaddr*>(&bound), &len);
        return socket_address_parser::format_address_and_port(bound);
    }

protected:
    int send_fd_ = -1;
    int receive_fd_ = -1;
    const UdpChannel& udp_channel_;
    struct sockaddr_storage connect_address_{};
    bool has_connect_address_ = false;

private:
    static void set_nonblocking(int fd)
    {
        int flags = ::fcntl(fd, F_GETFL, 0);
        if (flags < 0)
            throw std::runtime_error("fcntl F_GETFL failed");
        if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
            throw std::runtime_error("fcntl F_SETFL O_NONBLOCK failed");
    }

    static void set_rcvbuf(int fd, int size)
    {
        if (::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size)) < 0)
        {
            // Non-fatal: buffer size is advisory, OS default will be used
        }
    }

    static void set_sndbuf(int fd, int size)
    {
        if (::setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size)) < 0)
        {
            // Non-fatal: buffer size is advisory, OS default will be used
        }
    }

    void join_multicast()
    {
        const auto family = endpoint_address_.ss_family;
        if (family == AF_INET)
        {
            struct ip_mreq mreq{};
            const auto* ep = reinterpret_cast<const struct sockaddr_in*>(&endpoint_address_);
            mreq.imr_multiaddr = ep->sin_addr;

            if (udp_channel_.local_interface().has_interface)
            {
                const auto* local = reinterpret_cast<const struct sockaddr_in*>(
                    &udp_channel_.local_interface().address);
                mreq.imr_interface = local->sin_addr;
            }

            if (::setsockopt(receive_fd_, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                             &mreq, sizeof(mreq)) < 0)
            {
                throw std::runtime_error("IP_ADD_MEMBERSHIP failed: " +
                                         std::string(std::strerror(errno)));
            }
        }
        else if (family == AF_INET6)
        {
            struct ipv6_mreq mreq{};
            const auto* ep = reinterpret_cast<const struct sockaddr_in6*>(&endpoint_address_);
            mreq.ipv6mr_multiaddr = ep->sin6_addr;
            mreq.ipv6mr_interface = static_cast<unsigned>(udp_channel_.local_interface().interface_index);

            if (::setsockopt(receive_fd_, IPPROTO_IPV6, IPV6_JOIN_GROUP,
                             &mreq, sizeof(mreq)) < 0)
            {
                throw std::runtime_error("IPV6_JOIN_GROUP failed: " +
                                         std::string(std::strerror(errno)));
            }
        }
    }

    void leave_multicast()
    {
        const auto family = endpoint_address_.ss_family;
        if (family == AF_INET)
        {
            struct ip_mreq mreq{};
            const auto* ep = reinterpret_cast<const struct sockaddr_in*>(&endpoint_address_);
            mreq.imr_multiaddr = ep->sin_addr;

            if (udp_channel_.local_interface().has_interface)
            {
                const auto* local = reinterpret_cast<const struct sockaddr_in*>(
                    &udp_channel_.local_interface().address);
                mreq.imr_interface = local->sin_addr;
            }

            ::setsockopt(receive_fd_, IPPROTO_IP, IP_DROP_MEMBERSHIP,
                         &mreq, sizeof(mreq));
        }
        else if (family == AF_INET6)
        {
            struct ipv6_mreq mreq{};
            const auto* ep = reinterpret_cast<const struct sockaddr_in6*>(&endpoint_address_);
            mreq.ipv6mr_multiaddr = ep->sin6_addr;
            mreq.ipv6mr_interface = static_cast<unsigned>(udp_channel_.local_interface().interface_index);

            ::setsockopt(receive_fd_, IPPROTO_IPV6, IPV6_LEAVE_GROUP,
                         &mreq, sizeof(mreq));
        }
    }

    struct sockaddr_storage endpoint_address_{};
    struct sockaddr_storage bind_address_{};
    struct sockaddr_storage bound_address_{};  // actual address after bind (may differ from bind_address_ for port-0)
    PortManager* port_manager_;
    int multicast_ttl_ = 0;
    int socket_rcvbuf_length_;
    int socket_sndbuf_length_;
};

} // namespace caeron::driver::media
