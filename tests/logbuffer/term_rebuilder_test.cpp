#include <gtest/gtest.h>
#include "logbuffer/term_rebuilder.h"
#include "logbuffer/frame_descriptor.h"
#include "protocol/data_header_flyweight.h"

using namespace caeron;
using namespace caeron::logbuffer;
using namespace caeron::concurrent;
using namespace caeron::protocol;

class TermRebuilderTest : public ::testing::Test
{
protected:
    static constexpr i32 TERM_LENGTH = 4096;
    std::byte term_storage_[TERM_LENGTH]{};
    UnsafeBuffer term_buffer_{term_storage_, TERM_LENGTH};

    void SetUp() override { std::memset(term_storage_, 0, sizeof(term_storage_)); }

    // Build a simple DATA frame in a packet buffer
    static std::unique_ptr<std::byte[]> make_packet(i32& out_length)
    {
        constexpr i32 LEN = DataHeaderFlyweight::HEADER_LENGTH + 16;
        out_length = LEN;
        auto pkt = std::make_unique<std::byte[]>(LEN);
        std::memset(pkt.get(), 0, LEN);

        UnsafeBuffer buf{pkt.get(), LEN};
        buf.put_i32(0, LEN);   // frame_length
        buf.put_u8(4, 0);      // version
        buf.put_u8(5, DataHeaderFlyweight::UNFRAGMENTED);
        buf.put_u16(6, protocol::HeaderFlyweight::HDR_TYPE_DATA);
        buf.put_i32(8, 0);     // term_offset
        buf.put_i32(12, 42);   // session_id
        buf.put_i32(16, 7);    // stream_id
        buf.put_i32(20, 100);  // term_id
        // payload
        for (int i = 0; i < 16; ++i)
            pkt[DataHeaderFlyweight::HEADER_LENGTH + i] = std::byte(i + 1);

        return pkt;
    }
};

TEST_F(TermRebuilderTest, InsertIntoEmptySlot)
{
    i32 length;
    auto pkt = make_packet(length);
    UnsafeBuffer pkt_buf{pkt.get(), length};

    TermRebuilder::insert(term_buffer_, 0, pkt_buf, length);

    // Frame should be published
    EXPECT_EQ(FrameDescriptor::frame_length(term_buffer_, 0), length);
    EXPECT_EQ(FrameDescriptor::frame_term_id(term_buffer_, 0), 100);

    // Payload should be copied
    for (int i = 0; i < 16; ++i)
    {
        EXPECT_EQ(term_storage_[DataHeaderFlyweight::HEADER_LENGTH + i], std::byte(i + 1));
    }
}

TEST_F(TermRebuilderTest, SkipNonEmptySlot)
{
    // Pre-fill the slot with data
    term_buffer_.put_i32(0, 999);

    i32 length;
    auto pkt = make_packet(length);
    UnsafeBuffer pkt_buf{pkt.get(), length};

    TermRebuilder::insert(term_buffer_, 0, pkt_buf, length);

    // Should NOT have overwritten — original value remains
    EXPECT_EQ(term_buffer_.get_i32(0), 999);
}

TEST_F(TermRebuilderTest, InsertAtNonZeroOffset)
{
    constexpr i32 OFFSET = 128;
    i32 length;
    auto pkt = make_packet(length);
    UnsafeBuffer pkt_buf{pkt.get(), length};

    TermRebuilder::insert(term_buffer_, OFFSET, pkt_buf, length);

    EXPECT_EQ(FrameDescriptor::frame_length(term_buffer_, OFFSET), length);
}

TEST_F(TermRebuilderTest, InsertMultipleFrames)
{
    for (int i = 0; i < 3; ++i)
    {
        constexpr i32 HEADER = DataHeaderFlyweight::HEADER_LENGTH;
        constexpr i32 LEN = HEADER + 8;
        std::byte pkt_storage[LEN]{};
        UnsafeBuffer pkt{pkt_storage, LEN};
        pkt.put_i32(0, LEN);
        pkt.put_i32(20, 100 + i); // term_id
        pkt.put_i32(HEADER, i);   // payload

        TermRebuilder::insert(term_buffer_, i * FrameDescriptor::FRAME_ALIGNMENT, pkt, LEN);
    }

    // All three frames should be present
    for (int i = 0; i < 3; ++i)
    {
        const i32 off = i * FrameDescriptor::FRAME_ALIGNMENT;
        EXPECT_GT(FrameDescriptor::frame_length(term_buffer_, off), 0);
    }
}

TEST_F(TermRebuilderTest, InsertMultiFramePacket)
{
    // Build a packet containing two frames back-to-back, as the sender would
    // batch them into a single network packet. TermRebuilder::insert is called
    // once per frame, but the packet buffer contains both frames.
    // Frame 1: 32 bytes (header only, no payload)
    // Frame 2: 64 bytes (header + 32 bytes payload)
    constexpr i32 HEADER = DataHeaderFlyweight::HEADER_LENGTH;
    constexpr i32 FRAME1_LEN = HEADER;           // 32
    constexpr i32 FRAME2_LEN = HEADER + 32;      // 64
    constexpr i32 TOTAL_LEN = FRAME1_LEN + FRAME2_LEN; // 96

    std::byte pkt_storage[TOTAL_LEN]{};
    UnsafeBuffer pkt{pkt_storage, TOTAL_LEN};

    // Frame 1 at offset 0 in packet
    pkt.put_i32(0, FRAME1_LEN);      // frame_length = 32
    pkt.put_i32(20, 100);            // term_id = 100

    // Frame 2 at offset 32 in packet
    pkt.put_i32(FRAME1_LEN + 0, FRAME2_LEN);  // frame_length = 64
    pkt.put_i32(FRAME1_LEN + 20, 101);        // term_id = 101
    for (int i = 0; i < 32; ++i)
        pkt_storage[FRAME1_LEN + HEADER + i] = std::byte(i + 1);

    // Insert using the full multi-frame packet. TermRebuilder reads
    // frame_length from packet[0] (= 32), not the total length (96).
    TermRebuilder::insert(term_buffer_, 0, pkt, TOTAL_LEN);

    // frame_length should be 32 (from packet header), not 96 (total packet)
    EXPECT_EQ(FrameDescriptor::frame_length(term_buffer_, 0), FRAME1_LEN);
    EXPECT_EQ(FrameDescriptor::frame_term_id(term_buffer_, 0), 100);

    // The payload bytes (HEADER..TOTAL_LEN) were copied as-is.
    // frame_length=32 tells readers to only consume 32 bytes for this frame.
}
