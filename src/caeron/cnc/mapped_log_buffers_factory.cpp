#include "mapped_log_buffers_factory.h"
#include "platform/posix/mmap.h"

namespace caeron::cnc {

std::unique_ptr<LogBuffers> MappedLogBuffersFactory::create(
    const std::string& path, i32 term_length, i32 page_size)
{
    auto log_length = LogBuffers::compute_log_length(term_length, page_size);
    auto mapped = std::make_unique<platform::MemoryMappedFile>(
        platform::MemoryMappedFile::create_new(path, log_length));

    auto lb = std::make_unique<LogBuffers>();
    auto span = mapped->span();
    auto* base = span.data();
    auto term_len = static_cast<i64>(term_length);

    for (i32 i = 0; i < LogBuffers::PARTITION_COUNT; ++i)
        lb->term_buffers[i] = concurrent::UnsafeBuffer{base + term_len * i, term_length};

    auto meta_offset = LogBuffers::compute_log_meta_data_offset(term_len);
    lb->log_meta_data_buffer = concurrent::UnsafeBuffer{base + meta_offset, page_size};

    return lb;
}

std::unique_ptr<LogBuffers> MappedLogBuffersFactory::map_existing(
    const std::string& path, i32 term_length, i32 page_size, bool read_only)
{
    auto mapped = std::make_unique<platform::MemoryMappedFile>(
        platform::MemoryMappedFile::map_existing(path, read_only));

    auto lb = std::make_unique<LogBuffers>();
    auto span = mapped->span();
    auto* base = span.data();
    auto term_len = static_cast<i64>(term_length);

    for (i32 i = 0; i < LogBuffers::PARTITION_COUNT; ++i)
        lb->term_buffers[i] = concurrent::UnsafeBuffer{base + term_len * i, term_length};

    auto meta_offset = LogBuffers::compute_log_meta_data_offset(term_len);
    lb->log_meta_data_buffer = concurrent::UnsafeBuffer{base + meta_offset, page_size};

    return lb;
}

} // namespace caeron::cnc
