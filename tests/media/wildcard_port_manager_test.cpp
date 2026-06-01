#include "caeron/common/types.h"
#include "caeron/driver/media/wildcard_port_manager.h"
#include "caeron/driver/media/udp_channel.h"

#include <gtest/gtest.h>

using namespace caeron;
using namespace caeron::driver::media;

TEST(WildcardPortManager, ParsePortRange)
{
    auto range = WildcardPortManager::parse_port_range("20000 20009");
    EXPECT_EQ(range[0], 20000);
    EXPECT_EQ(range[1], 20009);
}

TEST(WildcardPortManager, ParsePortRangeEmpty)
{
    auto range = WildcardPortManager::parse_port_range("");
    EXPECT_EQ(range[0], 0);
    EXPECT_EQ(range[1], 0);
}

TEST(WildcardPortManager, ParsePortRangeInvalidThrows)
{
    auto fn1 = []() { (void)WildcardPortManager::parse_port_range("abc"); };
    auto fn2 = []() { (void)WildcardPortManager::parse_port_range("20000"); };
    auto fn3 = []() { (void)WildcardPortManager::parse_port_range("20009 20000"); };
    EXPECT_THROW(fn1(), std::invalid_argument);
    EXPECT_THROW(fn2(), std::invalid_argument);
    EXPECT_THROW(fn3(), std::invalid_argument);
}

TEST(WildcardPortManager, ConsecutiveAllocation)
{
    std::array<int, 2> range = {20000, 20009};
    WildcardPortManager mgr(range, false);

    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:40456");

    struct sockaddr_storage bind_addr{};
    bind_addr.ss_family = AF_INET;

    // Allocate several ports
    auto addr1 = mgr.get_managed_port(ch, bind_addr);
    auto port1 = socket_address_parser::get_port(addr1);
    EXPECT_GE(port1, 20000);
    EXPECT_LE(port1, 20009);

    auto addr2 = mgr.get_managed_port(ch, bind_addr);
    auto port2 = socket_address_parser::get_port(addr2);
    EXPECT_NE(port1, port2);

    mgr.free_managed_port(addr1);
    mgr.free_managed_port(addr2);
}

TEST(WildcardPortManager, OsWildcardPassthrough)
{
    std::array<int, 2> range = {0, 0};
    WildcardPortManager mgr(range, false);

    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:40456");

    struct sockaddr_storage bind_addr{};
    bind_addr.ss_family = AF_INET;
    socket_address_parser::set_port(bind_addr, 0);

    auto addr = mgr.get_managed_port(ch, bind_addr);
    // OS wildcard mode should return the address as-is
    EXPECT_EQ(socket_address_parser::get_port(addr), 0);
}

TEST(WildcardPortManager, InRangePortPassthrough)
{
    std::array<int, 2> range = {20000, 20009};
    WildcardPortManager mgr(range, false);

    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:40456");

    struct sockaddr_storage bind_addr{};
    bind_addr.ss_family = AF_INET;
    socket_address_parser::set_port(bind_addr, 20005);

    auto addr = mgr.get_managed_port(ch, bind_addr);
    // Port already in range -- should pass through
    EXPECT_EQ(socket_address_parser::get_port(addr), 20005);
}

// HIGH-2: Non-zero port outside range should be tracked to prevent double-allocation
TEST(WildcardPortManager, OutOfRangePortTracked)
{
    std::array<int, 2> range = {20000, 20009};
    WildcardPortManager mgr(range, false);

    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:40456");

    // Pass a non-zero port outside the range -- should be tracked and returned as-is
    struct sockaddr_storage bind_addr{};
    bind_addr.ss_family = AF_INET;
    socket_address_parser::set_port(bind_addr, 80);

    auto addr = mgr.get_managed_port(ch, bind_addr);
    EXPECT_EQ(socket_address_parser::get_port(addr), 80);

    // Now allocate from the range -- should NOT get port 80 (it's outside range, but tracked)
    struct sockaddr_storage zero_addr{};
    zero_addr.ss_family = AF_INET;
    auto addr2 = mgr.get_managed_port(ch, zero_addr);
    auto port2 = socket_address_parser::get_port(addr2);
    EXPECT_GE(port2, 20000);
    EXPECT_LE(port2, 20009);

    mgr.free_managed_port(addr2);
}

// HIGH-2: In-range non-zero port should be tracked so it's not double-allocated
TEST(WildcardPortManager, InRangePortTrackedPreventsDoubleAllocation)
{
    std::array<int, 2> range = {20000, 20009};
    WildcardPortManager mgr(range, false);

    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:40456");

    // Pass port 20005 as a non-zero bind address
    struct sockaddr_storage bind_addr{};
    bind_addr.ss_family = AF_INET;
    socket_address_parser::set_port(bind_addr, 20005);

    auto addr1 = mgr.get_managed_port(ch, bind_addr);
    EXPECT_EQ(socket_address_parser::get_port(addr1), 20005);

    // Allocate all remaining ports (20000-20009 = 10 ports, one is taken)
    std::vector<int> allocated_ports;
    for (int i = 0; i < 9; ++i)
    {
        struct sockaddr_storage zero_addr{};
        zero_addr.ss_family = AF_INET;
        auto addr = mgr.get_managed_port(ch, zero_addr);
        allocated_ports.push_back(socket_address_parser::get_port(addr));
    }

    // Port 20005 should NOT appear in the allocated ports
    for (int p : allocated_ports)
    {
        EXPECT_NE(p, 20005);
    }

    // All 10 ports are now used -- next allocation should throw
    struct sockaddr_storage zero_addr{};
    zero_addr.ss_family = AF_INET;
    EXPECT_THROW(mgr.get_managed_port(ch, zero_addr), std::runtime_error);

    // Cleanup
    for (int i = 0; i < 9; ++i)
    {
        struct sockaddr_storage cleanup_addr{};
        cleanup_addr.ss_family = AF_INET;
        socket_address_parser::set_port(cleanup_addr, static_cast<u16>(allocated_ports[i]));
        mgr.free_managed_port(cleanup_addr);
    }
}

// MEDIUM-5: Port range with values > 65535 should be rejected
TEST(WildcardPortManager, ParsePortRangeAbove65535Throws)
{
    auto fn1 = []() { (void)WildcardPortManager::parse_port_range("70000 70001"); };
    auto fn2 = []() { (void)WildcardPortManager::parse_port_range("20000 70000"); };
    auto fn3 = []() { (void)WildcardPortManager::parse_port_range("65536 65537"); };
    EXPECT_THROW(fn1(), std::invalid_argument);
    EXPECT_THROW(fn2(), std::invalid_argument);
    EXPECT_THROW(fn3(), std::invalid_argument);
}

TEST(WildcardPortManager, PortExhaustion)
{
    std::array<int, 2> range = {30000, 30001};
    WildcardPortManager mgr(range, false);

    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:40456");

    struct sockaddr_storage bind_addr{};
    bind_addr.ss_family = AF_INET;

    auto addr1 = mgr.get_managed_port(ch, bind_addr);
    auto addr2 = mgr.get_managed_port(ch, bind_addr);

    // Third allocation should throw (only 2 ports in range)
    EXPECT_THROW(mgr.get_managed_port(ch, bind_addr), std::runtime_error);

    mgr.free_managed_port(addr1);
    mgr.free_managed_port(addr2);
}

TEST(WildcardPortManager, FreeAndReallocate)
{
    std::array<int, 2> range = {30000, 30000};
    WildcardPortManager mgr(range, false);

    auto ch = UdpChannel::parse("aeron:udp?endpoint=127.0.0.1:40456");

    struct sockaddr_storage bind_addr{};
    bind_addr.ss_family = AF_INET;

    auto addr1 = mgr.get_managed_port(ch, bind_addr);
    EXPECT_EQ(socket_address_parser::get_port(addr1), 30000);

    // Should throw since the only port is taken
    EXPECT_THROW(mgr.get_managed_port(ch, bind_addr), std::runtime_error);

    // Free and reallocate
    mgr.free_managed_port(addr1);
    auto addr2 = mgr.get_managed_port(ch, bind_addr);
    EXPECT_EQ(socket_address_parser::get_port(addr2), 30000);

    mgr.free_managed_port(addr2);
}
