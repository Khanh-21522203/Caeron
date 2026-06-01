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

// Media Driver to Clients (response event types)

inline constexpr u16 ON_ERROR                      = 0x0F01;
inline constexpr u16 ON_AVAILABLE_IMAGE             = 0x0F02;
inline constexpr u16 ON_PUBLICATION_READY           = 0x0F03;
inline constexpr u16 ON_OPERATION_SUCCESS           = 0x0F04;
inline constexpr u16 ON_UNAVAILABLE_IMAGE           = 0x0F05;
inline constexpr u16 ON_EXCLUSIVE_PUBLICATION_READY = 0x0F06;
inline constexpr u16 ON_SUBSCRIPTION_READY          = 0x0F07;
inline constexpr u16 ON_COUNTER_READY               = 0x0F08;
inline constexpr u16 ON_UNAVAILABLE_COUNTER         = 0x0F09;
inline constexpr u16 ON_CLIENT_TIMEOUT              = 0x0F0A;
inline constexpr u16 ON_STATIC_COUNTER              = 0x0F0B;
inline constexpr u16 ON_PUBLICATION_ERROR           = 0x0F0C;
inline constexpr u16 ON_NEXT_AVAILABLE_SESSION_ID   = 0x0F0D;

} // namespace caeron::command
