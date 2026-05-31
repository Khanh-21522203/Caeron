#include "caeron/protocol/resolution_entry_flyweight.h"

#include <gtest/gtest.h>
#include <array>

using namespace caeron;
using namespace caeron::concurrent;
using namespace caeron::protocol;

TEST(ResolutionEntryFlyweight, IPv4EntryAtOffsetZero)
{
    std::array<std::byte, 512> storage{};
    UnsafeBuffer buf{storage};
    ResolutionEntryFlyweight entry{buf};

    entry.set_res_type(ResolutionEntryFlyweight::RES_TYPE_NAME_TO_IP4_MD)
         .set_flags(ResolutionEntryFlyweight::SELF_FLAG)
         .set_udp_port(40456)
         .set_age_in_ms(5000);

    u8 addr[] = {192, 168, 1, 100};
    entry.put_address(addr, 4);

    EXPECT_EQ(entry.res_type(), ResolutionEntryFlyweight::RES_TYPE_NAME_TO_IP4_MD);
    EXPECT_EQ(entry.flags(), ResolutionEntryFlyweight::SELF_FLAG);
    EXPECT_EQ(entry.udp_port(), 40456);
    EXPECT_EQ(entry.age_in_ms(), 5000);

    u8 dst[4]{};
    EXPECT_EQ(entry.get_address(dst, 4), 4);
    EXPECT_EQ(dst[0], 192);
    EXPECT_EQ(dst[1], 168);
    EXPECT_EQ(dst[2], 1);
    EXPECT_EQ(dst[3], 100);
}

TEST(ResolutionEntryFlyweight, IPv6EntryAtNonZeroOffset)
{
    std::array<std::byte, 512> storage{};
    UnsafeBuffer buf{storage};
    constexpr i32 offset = 64;
    ResolutionEntryFlyweight entry{buf, offset};

    entry.set_res_type(ResolutionEntryFlyweight::RES_TYPE_NAME_TO_IP6_MD)
         .set_flags(0)
         .set_udp_port(40457)
         .set_age_in_ms(10000);

    u8 addr[16] = {0x20, 0x01, 0x0d, 0xb8, 0x85, 0xa3, 0x00, 0x00,
                   0x00, 0x00, 0x8a, 0x2e, 0x03, 0x70, 0x73, 0x34};
    entry.put_address(addr, 16);

    EXPECT_EQ(entry.res_type(), ResolutionEntryFlyweight::RES_TYPE_NAME_TO_IP6_MD);
    EXPECT_EQ(entry.udp_port(), 40457);

    u8 dst[16]{};
    EXPECT_EQ(entry.get_address(dst, 16), 16);
    EXPECT_EQ(dst[0], 0x20);
    EXPECT_EQ(dst[1], 0x01);
    EXPECT_EQ(dst[15], 0x34);

    // Verify data is at correct offset
    EXPECT_EQ(buf.get_u8(offset + ResolutionEntryFlyweight::RES_TYPE_FIELD_OFFSET),
              ResolutionEntryFlyweight::RES_TYPE_NAME_TO_IP6_MD);
}

TEST(ResolutionEntryFlyweight, NameField)
{
    std::array<std::byte, 512> storage{};
    UnsafeBuffer buf{storage};
    ResolutionEntryFlyweight entry{buf};

    entry.set_res_type(ResolutionEntryFlyweight::RES_TYPE_NAME_TO_IP4_MD);

    u8 addr[] = {10, 0, 0, 1};
    entry.put_address(addr, 4);

    std::string name = "endpoint.example.com";
    entry.put_name(reinterpret_cast<const u8*>(name.data()), static_cast<i32>(name.size()));

    u8 name_buf[256]{};
    i32 name_len = entry.get_name(name_buf, 256);
    EXPECT_EQ(name_len, static_cast<i32>(name.size()));
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(name_buf), name_len), name);
}

TEST(ResolutionEntryFlyweight, EntryLength)
{
    std::array<std::byte, 512> storage{};
    UnsafeBuffer buf{storage};
    ResolutionEntryFlyweight entry{buf};

    entry.set_res_type(ResolutionEntryFlyweight::RES_TYPE_NAME_TO_IP4_MD);

    u8 addr[] = {10, 0, 0, 1};
    entry.put_address(addr, 4);

    std::string name = "test";
    entry.put_name(reinterpret_cast<const u8*>(name.data()), static_cast<i32>(name.size()));

    // entry_length_required: name_offset(4) = 8+4 = 12, + 2 (short) + 4 (name) = 18, aligned to 8 = 24
    i32 required = ResolutionEntryFlyweight::entry_length_required(
        ResolutionEntryFlyweight::RES_TYPE_NAME_TO_IP4_MD, 4);
    EXPECT_EQ(required, 24);

    EXPECT_EQ(entry.entry_length(), 24);
}

TEST(ResolutionEntryFlyweight, StaticHelpers)
{
    EXPECT_EQ(ResolutionEntryFlyweight::name_offset(ResolutionEntryFlyweight::RES_TYPE_NAME_TO_IP4_MD), 12);
    EXPECT_EQ(ResolutionEntryFlyweight::name_offset(ResolutionEntryFlyweight::RES_TYPE_NAME_TO_IP6_MD), 24);
    EXPECT_EQ(ResolutionEntryFlyweight::address_length(ResolutionEntryFlyweight::RES_TYPE_NAME_TO_IP4_MD), 4);
    EXPECT_EQ(ResolutionEntryFlyweight::address_length(ResolutionEntryFlyweight::RES_TYPE_NAME_TO_IP6_MD), 16);
}

TEST(ResolutionEntryFlyweight, IsAnyLocalAddress)
{
    u8 ipv4_any[] = {0, 0, 0, 0};
    EXPECT_TRUE(ResolutionEntryFlyweight::is_any_local_address(ipv4_any, 4));

    u8 ipv4_not_any[] = {127, 0, 0, 1};
    EXPECT_FALSE(ResolutionEntryFlyweight::is_any_local_address(ipv4_not_any, 4));

    u8 ipv6_any[16] = {};
    EXPECT_TRUE(ResolutionEntryFlyweight::is_any_local_address(ipv6_any, 16));

    u8 ipv6_not_any[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    EXPECT_FALSE(ResolutionEntryFlyweight::is_any_local_address(ipv6_not_any, 16));
}

TEST(ResolutionEntryFlyweight, MaxNameLengthAccepted)
{
    std::array<std::byte, 1024> storage{};
    UnsafeBuffer buf{storage};
    ResolutionEntryFlyweight entry{buf};

    entry.set_res_type(ResolutionEntryFlyweight::RES_TYPE_NAME_TO_IP4_MD);

    u8 addr[] = {10, 0, 0, 1};
    entry.put_address(addr, 4);

    // MAX_NAME_LENGTH = 512 should be accepted
    std::string name(512, 'x');
    EXPECT_NO_THROW(entry.put_name(reinterpret_cast<const u8*>(name.data()), 512));

    // Verify it was stored correctly
    u8 name_buf[512]{};
    EXPECT_EQ(entry.get_name(name_buf, 512), 512);
}

TEST(ResolutionEntryFlyweight, OversizedNameRejected)
{
    std::array<std::byte, 1024> storage{};
    UnsafeBuffer buf{storage};
    ResolutionEntryFlyweight entry{buf};

    entry.set_res_type(ResolutionEntryFlyweight::RES_TYPE_NAME_TO_IP4_MD);

    u8 addr[] = {10, 0, 0, 1};
    entry.put_address(addr, 4);

    // 513 bytes exceeds MAX_NAME_LENGTH
    std::string name(513, 'x');
    EXPECT_THROW(entry.put_name(reinterpret_cast<const u8*>(name.data()), 513), std::out_of_range);
}

TEST(ResolutionEntryFlyweight, NegativeNameLengthRejected)
{
    std::array<std::byte, 1024> storage{};
    UnsafeBuffer buf{storage};
    ResolutionEntryFlyweight entry{buf};

    entry.set_res_type(ResolutionEntryFlyweight::RES_TYPE_NAME_TO_IP4_MD);

    u8 addr[] = {10, 0, 0, 1};
    entry.put_address(addr, 4);

    u8 name[] = {'t', 'e', 's', 't'};
    EXPECT_THROW(entry.put_name(name, -1), std::out_of_range);
}

TEST(ResolutionEntryFlyweight, StoredNegativeNameLengthRejected)
{
    std::array<std::byte, 1024> storage{};
    UnsafeBuffer buf{storage};
    ResolutionEntryFlyweight entry{buf};

    entry.set_res_type(ResolutionEntryFlyweight::RES_TYPE_NAME_TO_IP4_MD);

    // Write a negative name length directly into the buffer
    // name_offset for IPv4 = 12, so name_length is at offset 12
    buf.put_i16(12, -5);

    u8 name_buf[64]{};
    EXPECT_THROW(entry.get_name(name_buf, 64), std::runtime_error);
    EXPECT_THROW(
        { [[maybe_unused]] i32 v = entry.entry_length(); },
        std::runtime_error);
}

TEST(ResolutionEntryFlyweight, StoredOversizedNameLengthRejected)
{
    std::array<std::byte, 1024> storage{};
    UnsafeBuffer buf{storage};
    ResolutionEntryFlyweight entry{buf};

    entry.set_res_type(ResolutionEntryFlyweight::RES_TYPE_NAME_TO_IP4_MD);

    // Write an oversized name length directly (600 > MAX_NAME_LENGTH=512)
    buf.put_i16(12, 600);

    u8 name_buf[64]{};
    EXPECT_THROW(entry.get_name(name_buf, 64), std::runtime_error);
    EXPECT_THROW(
        { [[maybe_unused]] i32 v = entry.entry_length(); },
        std::runtime_error);
}

TEST(ResolutionEntryFlyweight, EntryLengthRequiredRejectsInvalid)
{
    EXPECT_THROW(
        { [[maybe_unused]] i32 v = ResolutionEntryFlyweight::entry_length_required(
            ResolutionEntryFlyweight::RES_TYPE_NAME_TO_IP4_MD, -1); },
        std::out_of_range);
    EXPECT_THROW(
        { [[maybe_unused]] i32 v = ResolutionEntryFlyweight::entry_length_required(
            ResolutionEntryFlyweight::RES_TYPE_NAME_TO_IP4_MD, 513); },
        std::out_of_range);
}
