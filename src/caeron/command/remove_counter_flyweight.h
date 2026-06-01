#pragma once

#include "caeron/command/remove_message_flyweight.h"

namespace caeron::command {

/// Control message for removing a Counter.
/// Layout is identical to RemoveMessageFlyweight:
///   [ 0] i64  client_id
///   [ 8] i64  correlation_id
///   [16] i64  registration_id
///   [24] total length = 24
using RemoveCounterFlyweight = RemoveMessageFlyweight;

} // namespace caeron::command
