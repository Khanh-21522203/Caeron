#pragma once

#include "caeron/common/types.h"

#include <span>
#include <string>

namespace caeron::platform {

/// RAII wrapper for memory-mapped files.
class MemoryMappedFile
{
public:
    /// Create a new memory-mapped file with the given size.
    static MemoryMappedFile create_new(const std::string& path, i64 size);

    /// Map an existing file.
    static MemoryMappedFile map_existing(const std::string& path, bool read_only = false);

    ~MemoryMappedFile();
    MemoryMappedFile(MemoryMappedFile&& other) noexcept;
    MemoryMappedFile& operator=(MemoryMappedFile&& other) noexcept;
    MemoryMappedFile(const MemoryMappedFile&) = delete;
    MemoryMappedFile& operator=(const MemoryMappedFile&) = delete;

    [[nodiscard]] std::span<std::byte> span() noexcept
    {
        return {static_cast<std::byte*>(addr_), static_cast<size_t>(size_)};
    }

    [[nodiscard]] const std::span<const std::byte> span() const noexcept
    {
        return {static_cast<const std::byte*>(addr_), static_cast<size_t>(size_)};
    }

    [[nodiscard]] void* addr() noexcept { return addr_; }
    [[nodiscard]] i64 size() const noexcept { return size_; }

    void pre_touch();

private:
    MemoryMappedFile(void* addr, i64 size, bool owns_file, std::string path);
    void unmap();

    void* addr_ = nullptr;
    i64 size_ = 0;
    bool owns_file_ = false;
    std::string path_;
};

} // namespace caeron::platform
