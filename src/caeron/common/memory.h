#pragma once

#include "types.h"

#include <cstdlib>
#include <new>
#include <unistd.h>

namespace caeron {

[[nodiscard]] inline i32 page_size()
{
    static const i32 ps = static_cast<i32>(::sysconf(_SC_PAGESIZE));
    return ps;
}

[[nodiscard]] inline void* alloc_aligned(i32 alignment, i32 size)
{
    void* ptr = nullptr;
    if (::posix_memalign(&ptr, static_cast<size_t>(alignment), static_cast<size_t>(size)) != 0)
        throw std::bad_alloc();
    return ptr;
}

inline void free_aligned(void* ptr)
{
    std::free(ptr);
}

} // namespace caeron
