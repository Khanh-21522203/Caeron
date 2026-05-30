#include "mmap.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <stdexcept>

namespace caeron::platform {

MemoryMappedFile MemoryMappedFile::create_new(const std::string& path, i64 size)
{
    int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        throw std::runtime_error("Failed to create file: " + path);

    if (::ftruncate(fd, size) < 0)
    {
        ::close(fd);
        throw std::runtime_error("Failed to set file size: " + path);
    }

    void* addr = ::mmap(nullptr, static_cast<size_t>(size), PROT_READ | PROT_WRITE,
                        MAP_SHARED, fd, 0);
    ::close(fd);

    if (addr == MAP_FAILED)
        throw std::runtime_error("Failed to mmap: " + path);

    return MemoryMappedFile{addr, size, true, path};
}

MemoryMappedFile MemoryMappedFile::map_existing(const std::string& path, bool read_only)
{
    int fd = ::open(path.c_str(), read_only ? O_RDONLY : O_RDWR);
    if (fd < 0)
        throw std::runtime_error("Failed to open file: " + path);

    struct stat st{};
    if (::fstat(fd, &st) < 0)
    {
        ::close(fd);
        throw std::runtime_error("Failed to stat file: " + path);
    }

    int prot = read_only ? PROT_READ : (PROT_READ | PROT_WRITE);
    void* addr = ::mmap(nullptr, static_cast<size_t>(st.st_size), prot, MAP_SHARED, fd, 0);
    ::close(fd);

    if (addr == MAP_FAILED)
        throw std::runtime_error("Failed to mmap: " + path);

    return MemoryMappedFile{addr, st.st_size, false, path};
}

MemoryMappedFile::~MemoryMappedFile()
{
    unmap();
}

MemoryMappedFile::MemoryMappedFile(MemoryMappedFile&& other) noexcept
    : addr_{other.addr_}, size_{other.size_}, owns_file_{other.owns_file_},
      path_{std::move(other.path_)}
{
    other.addr_ = nullptr;
    other.size_ = 0;
}

MemoryMappedFile& MemoryMappedFile::operator=(MemoryMappedFile&& other) noexcept
{
    if (this != &other)
    {
        unmap();
        addr_ = other.addr_;
        size_ = other.size_;
        owns_file_ = other.owns_file_;
        path_ = std::move(other.path_);
        other.addr_ = nullptr;
        other.size_ = 0;
    }
    return *this;
}

MemoryMappedFile::MemoryMappedFile(void* addr, i64 size, bool owns_file, std::string path)
    : addr_{addr}, size_{size}, owns_file_{owns_file}, path_{std::move(path)}
{}

void MemoryMappedFile::unmap()
{
    if (addr_ && addr_ != MAP_FAILED)
        ::munmap(addr_, static_cast<size_t>(size_));
    if (owns_file_ && !path_.empty())
        ::unlink(path_.c_str());
    addr_ = nullptr;
    size_ = 0;
}

} // namespace caeron::platform
