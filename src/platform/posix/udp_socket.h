#pragma once

#include "caeron/common/types.h"

#include <arpa/inet.h>
#include <string>
#include <vector>

namespace caeron::platform {

/// A single message to send via send_mmsg().
struct SendMsg
{
    const void*  data;       // pointer to the packet data
    i32          length;     // packet length in bytes
    std::string  address;    // destination IP (e.g., "192.168.1.1")
    u16          port;       // destination port
};

/// A single receive buffer for recv_mmsg().
struct RecvMsg
{
    std::byte*   buffer;          // where to write received data
    i32          buffer_length;   // size of buffer
    i32          received_length; // actual bytes received (output)
    std::string  from_address;    // source IP (output)
    u16          from_port;       // source port (output)
};

/// RAII wrapper for a UDP socket.
class UdpSocket
{
public:
    UdpSocket();
    ~UdpSocket();

    UdpSocket(UdpSocket&& other) noexcept;
    UdpSocket& operator=(UdpSocket&& other) noexcept;
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    void bind(const std::string& address, u16 port);
    void connect(const std::string& address, u16 port);

    [[nodiscard]] i32 send_to(const void* data, i32 length,
                               const std::string& address, u16 port);
    [[nodiscard]] i32 receive_from(void* buffer, i32 max_length,
                                    std::string& from_address, u16& from_port);

    void set_sndbuf(i32 size);
    void set_rcvbuf(i32 size);
    void set_busy_loop(i32 microseconds);
    void join_multicast(const std::string& group, const std::string& iface = "");
    void leave_multicast(const std::string& group, const std::string& iface = "");

    [[nodiscard]] int fd() const noexcept { return fd_; }
    [[nodiscard]] bool is_open() const noexcept { return fd_ >= 0; }

    /// Batch send multiple UDP packets in one syscall.
    /// Returns the number of packets actually sent (may be less than messages.size()).
    [[nodiscard]] i32 send_mmsg(const std::vector<SendMsg>& messages);

    /// Batch receive multiple UDP packets in one syscall.
    /// Returns the number of packets actually received.
    [[nodiscard]] i32 recv_mmsg(std::vector<RecvMsg>& messages);

    void close();

private:
    int fd_ = -1;
};

} // namespace caeron::platform
