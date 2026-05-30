#pragma once

#include "caeron/common/types.h"

namespace caeron::command {

inline constexpr u8 ADD_PUBLICATION            = 0x01;
inline constexpr u8 REMOVE_PUBLICATION         = 0x02;
inline constexpr u8 ADD_EXCLUSIVE_PUBLICATION  = 0x03;
inline constexpr u8 ADD_SUBSCRIPTION           = 0x04;
inline constexpr u8 REMOVE_SUBSCRIPTION        = 0x05;
inline constexpr u8 CLIENT_KEEPALIVE           = 0x06;
inline constexpr u8 ADD_DESTINATION            = 0x07;
inline constexpr u8 REMOVE_DESTINATION         = 0x08;
inline constexpr u8 ADD_COUNTER                = 0x09;
inline constexpr u8 REMOVE_COUNTER             = 0x0A;
inline constexpr u8 CLIENT_CLOSE               = 0x0B;
inline constexpr u8 ADD_RCV_DESTINATION        = 0x0C;
inline constexpr u8 REMOVE_RCV_DESTINATION     = 0x0D;
inline constexpr u8 TERMINATE_DRIVER           = 0x0E;
inline constexpr u8 ADD_STATIC_COUNTER         = 0x0F;
inline constexpr u8 REJECT_IMAGE               = 0x10;
inline constexpr u8 REMOVE_DESTINATION_BY_ID   = 0x11;
inline constexpr u8 GET_NEXT_AVAILABLE_SESSION_ID = 0x12;

} // namespace caeron::command
