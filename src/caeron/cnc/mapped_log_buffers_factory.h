#pragma once

#include "log_buffers.h"
#include "caeron/common/types.h"

#include <memory>
#include <string>

namespace caeron::cnc {

/// Factory for creating LogBuffers from memory-mapped files.
struct MappedLogBuffersFactory
{
    /// Create a new log buffer file and map it into memory.
    /// Returns a LogBuffers with term_buffers and log_meta_data_buffer initialized.
    static std::unique_ptr<LogBuffers> create(const std::string& path,
                                               i32 term_length,
                                               i32 page_size);

    /// Map an existing log buffer file into memory.
    static std::unique_ptr<LogBuffers> map_existing(const std::string& path,
                                                     i32 term_length,
                                                     i32 page_size,
                                                     bool read_only = false);
};

} // namespace caeron::cnc
