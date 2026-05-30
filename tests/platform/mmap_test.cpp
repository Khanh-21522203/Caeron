#include "platform/posix/mmap.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

using namespace caeron;
using namespace caeron::platform;
namespace fs = std::filesystem;

class MmapTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        test_dir_ = fs::temp_directory_path() / "caeron_mmap_test";
        fs::create_directories(test_dir_);
    }

    void TearDown() override
    {
        fs::remove_all(test_dir_);
    }

    fs::path test_dir_;
};

TEST_F(MmapTest, CreateNewHasCorrectSize)
{
    auto path = (test_dir_ / "test.bin").string();
    auto mmap = MemoryMappedFile::create_new(path, 4096);

    EXPECT_EQ(mmap.size(), 4096);
    EXPECT_NE(mmap.addr(), nullptr);
    EXPECT_EQ(mmap.span().size(), 4096u);
}

TEST_F(MmapTest, CreateNewFileExistsOnDisk)
{
    auto path = (test_dir_ / "test.bin").string();
    auto mmap = MemoryMappedFile::create_new(path, 4096);

    EXPECT_TRUE(fs::exists(path));
    EXPECT_EQ(fs::file_size(path), 4096u);
}

TEST_F(MmapTest, MapExistingRoundTrip)
{
    auto path = (test_dir_ / "test.bin").string();

    // Create and write a value — must stay alive (owns_file_ deletes on destroy)
    auto writer = MemoryMappedFile::create_new(path, 4096);
    auto* ptr = static_cast<i32*>(writer.addr());
    *ptr = 42;

    // Map it back and read
    auto reader = MemoryMappedFile::map_existing(path);
    auto* rptr = static_cast<const i32*>(reader.addr());
    EXPECT_EQ(*rptr, 42);
}

TEST_F(MmapTest, MapExistingReadOnly)
{
    auto path = (test_dir_ / "test.bin").string();
    auto writer = MemoryMappedFile::create_new(path, 4096);

    auto mmap = MemoryMappedFile::map_existing(path, true);
    EXPECT_EQ(mmap.size(), 4096);
}

TEST_F(MmapTest, PreTouchDoesNotCrash)
{
    auto path = (test_dir_ / "test.bin").string();
    auto mmap = MemoryMappedFile::create_new(path, 8192);

    // Should not crash or throw
    mmap.pre_touch();

    // Verify memory is zeroed (first and last page)
    auto* ptr = static_cast<const std::byte*>(mmap.addr());
    EXPECT_EQ(ptr[0], std::byte{0});
    EXPECT_EQ(ptr[8191], std::byte{0});
}

TEST_F(MmapTest, MoveConstruct)
{
    auto path = (test_dir_ / "test.bin").string();
    auto mmap1 = MemoryMappedFile::create_new(path, 4096);
    auto* addr = mmap1.addr();

    MemoryMappedFile mmap2 = std::move(mmap1);

    EXPECT_EQ(mmap2.addr(), addr);
    EXPECT_EQ(mmap2.size(), 4096);
    EXPECT_EQ(mmap1.addr(), nullptr);
    EXPECT_EQ(mmap1.size(), 0);
}

TEST_F(MmapTest, MoveAssign)
{
    auto path1 = (test_dir_ / "test1.bin").string();
    auto path2 = (test_dir_ / "test2.bin").string();
    auto mmap1 = MemoryMappedFile::create_new(path1, 4096);
    auto mmap2 = MemoryMappedFile::create_new(path2, 8192);

    auto* addr1 = mmap1.addr();
    mmap2 = std::move(mmap1);

    EXPECT_EQ(mmap2.addr(), addr1);
    EXPECT_EQ(mmap1.addr(), nullptr);
}

TEST_F(MmapTest, CreateNewInvalidPathThrows)
{
    EXPECT_THROW(
        MemoryMappedFile::create_new("/nonexistent/dir/file.bin", 4096),
        std::runtime_error);
}

TEST_F(MmapTest, MapExistingNonexistentThrows)
{
    EXPECT_THROW(
        MemoryMappedFile::map_existing("/nonexistent/dir/file.bin"),
        std::runtime_error);
}
