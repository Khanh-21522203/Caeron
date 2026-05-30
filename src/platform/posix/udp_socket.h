#pragma once

#include "caeron/common/types.h"

#include <arpa/inet.h>
#include <string>

namespace caeron::platform {

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
    void join_multicast(const std::string& group, const std::string& iface = "");
    void leave_multicast(const std::string& group, const std::string& iface = "");

    [[nodiscard]] int fd() const noexcept { return fd_; }
    [[nodiscard]] bool is_open() const noexcept { return fd_ >= 0; }

    void close();

private:
    int fd_ = -1;
};

} // namespace caeron::platform
