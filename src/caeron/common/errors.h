#pragma once

#include "types.h"

#include <expected>
#include <string>

namespace caeron {

enum class ErrorCode : i32 {
    OK = 0,
    GENERIC_ERROR = -1,
    BUFFER_TOO_SMALL = -2,
    INVALID_ARGUMENT = -3,
    ALREADY_EXISTS = -4,
    NOT_FOUND = -5,
    TIMEOUT = -6,
    CLOSED = -7,
};

template <typename T>
using Result = std::expected<T, ErrorCode>;

} // namespace caeron
