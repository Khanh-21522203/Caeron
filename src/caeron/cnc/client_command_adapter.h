#pragma once

#include "caeron/common/bit_util.h"
#include "caeron/common/types.h"
#include "caeron/concurrent/many_to_one_ring_buffer.h"
#include "caeron/concurrent/unsafe_buffer.h"
#include "caeron/command/control_protocol_events.h"
#include "caeron/command/publication_message_flyweight.h"
#include "caeron/command/subscription_message_flyweight.h"
#include "caeron/command/remove_publication_flyweight.h"
#include "caeron/command/remove_message_flyweight.h"
#include "caeron/command/remove_subscription_flyweight.h"
#include "caeron/command/destination_message_flyweight.h"
#include "caeron/command/destination_by_id_message_flyweight.h"
#include "caeron/command/counter_message_flyweight.h"
#include "caeron/command/remove_counter_flyweight.h"
#include "caeron/command/correlated_message_flyweight.h"
#include "caeron/command/terminate_driver_flyweight.h"
#include "caeron/command/reject_image_flyweight.h"
#include "caeron/command/get_next_available_session_id_message_flyweight.h"
#include "caeron/command/static_counter_message_flyweight.h"

#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace caeron::cnc {

/// Error codes matching Java Aeron's ErrorCode values.
inline constexpr i32 ERROR_CODE_GENERIC            = 1;
inline constexpr i32 ERROR_CODE_CHANNEL_ENDPOINT   = 2;
inline constexpr i32 ERROR_CODE_UNKNOWN_COMMAND     = 3;
inline constexpr i32 ERROR_CODE_MALFORMED_COMMAND   = 4;

/// Driver-side adapter that reads commands from the to-driver ring buffer
/// and dispatches them to a handler (typically the DriverConductor).
///
/// The Handler template parameter must provide the following methods:
///
///   void on_add_publication(std::string_view channel, i32 stream_id, i64 correlation_id, i64 client_id, bool is_exclusive);
///   void on_remove_publication(i64 registration_id, i64 correlation_id, bool revoke);
///   void on_add_subscription(std::string_view channel, i32 stream_id, i64 correlation_id, i64 client_id);
///   void on_remove_subscription(i64 registration_id, i64 correlation_id);
///   void on_add_send_destination(i64 registration_id, std::string_view channel, i64 correlation_id);
///   void on_remove_send_destination(i64 registration_id, std::string_view channel, i64 correlation_id);
///   void on_remove_send_destination_by_id(i64 resource_registration_id, i64 destination_registration_id, i64 correlation_id);
///   void on_add_rcv_destination(i64 registration_id, std::string_view channel, i64 correlation_id);
///   void on_remove_rcv_destination(i64 registration_id, std::string_view channel, i64 correlation_id);
///   void on_client_keepalive(i64 client_id);
///   void on_client_close(i64 client_id);
///   void on_add_counter(i32 type_id, const void* key, i32 key_offset, i32 key_length, const void* label, i32 label_offset, i32 label_length, i64 correlation_id, i64 client_id);
///   void on_remove_counter(i64 registration_id, i64 correlation_id);
///   void on_terminate_driver(const void* token, i32 token_offset, i32 token_length);
///   void on_add_static_counter(i32 type_id, const void* key, i32 key_offset, i32 key_length, const void* label, i32 label_offset, i32 label_length, i64 registration_id, i64 correlation_id, i64 client_id);
///   void on_reject_image(i64 correlation_id, i64 image_correlation_id, i64 position, std::string_view reason);
///   void on_next_available_session_id(i64 correlation_id, i32 stream_id);
template <typename Handler>
class ClientCommandAdapter
{
public:
    /// Callback type for error reporting.
    using ErrorCallback = std::function<void(i64 correlation_id, i32 error_code, std::string_view message)>;

    ClientCommandAdapter(concurrent::ManyToOneRingBuffer& ring_buffer,
                         Handler& handler,
                         ErrorCallback on_error = nullptr)
        : ring_buffer_{ring_buffer}, handler_{handler}, on_error_{std::move(on_error)}
    {}

    /// Read and dispatch all available commands from the ring buffer.
    ///
    /// @return the number of messages read.
    i32 receive()
    {
        return ring_buffer_.read([this](i32 msg_type_id, const std::byte* data, i32 length) {
            dispatch(msg_type_id, data, length);
        });
    }

private:
    /// Helper to construct a string_view from a flyweight string field.
    static std::string_view make_string_view(const std::byte* base, i32 offset, i32 length) noexcept
    {
        return {reinterpret_cast<const char*>(base + offset), static_cast<size_t>(length)};
    }

    void dispatch(i32 msg_type_id, const std::byte* data, i32 length)
    {
        // SAFETY: const_cast is required because flyweight constructors take a mutable
        // UnsafeBuffer&, but the ring buffer delivers const data. The flyweights are only
        // used for reading here (no setters called), so no actual mutation occurs. This
        // matches Java Aeron where MutableDirectBuffer wraps the same const region.
        concurrent::UnsafeBuffer buffer{const_cast<std::byte*>(data), length};

        i64 correlation_id = 0;

        try
        {
            switch (msg_type_id)
            {
                case command::ADD_PUBLICATION:
                {
                    if (length < command::PUBLICATION_MSG_MINIMUM_LENGTH)
                    {
                        report_error(0, ERROR_CODE_MALFORMED_COMMAND, "message too short for ADD_PUBLICATION");
                        break;
                    }
                    command::PublicationMessageFlyweight fw{buffer, 0};
                    correlation_id = fw.correlation_id();
                    const i64 client_id = fw.client_id();
                    const i32 stream_id = fw.stream_id();
                    const i32 channel_len = fw.channel_length();
                    if (channel_len < 0 ||
                        length < command::PublicationMessageFlyweight::CHANNEL_OFFSET ||
                        channel_len > length - command::PublicationMessageFlyweight::CHANNEL_OFFSET)
                    {
                        report_error(correlation_id, ERROR_CODE_MALFORMED_COMMAND, "invalid channel length in ADD_PUBLICATION");
                        break;
                    }
                    auto channel = make_string_view(data, command::PublicationMessageFlyweight::CHANNEL_OFFSET, channel_len);

                    handler_.on_add_publication(channel, stream_id, correlation_id, client_id, false);
                    break;
                }

                case command::ADD_EXCLUSIVE_PUBLICATION:
                {
                    if (length < command::PUBLICATION_MSG_MINIMUM_LENGTH)
                    {
                        report_error(0, ERROR_CODE_MALFORMED_COMMAND, "message too short for ADD_EXCLUSIVE_PUBLICATION");
                        break;
                    }
                    command::PublicationMessageFlyweight fw{buffer, 0};
                    correlation_id = fw.correlation_id();
                    const i64 client_id = fw.client_id();
                    const i32 stream_id = fw.stream_id();
                    const i32 channel_len = fw.channel_length();
                    if (channel_len < 0 ||
                        length < command::PublicationMessageFlyweight::CHANNEL_OFFSET ||
                        channel_len > length - command::PublicationMessageFlyweight::CHANNEL_OFFSET)
                    {
                        report_error(correlation_id, ERROR_CODE_MALFORMED_COMMAND, "invalid channel length in ADD_EXCLUSIVE_PUBLICATION");
                        break;
                    }
                    auto channel = make_string_view(data, command::PublicationMessageFlyweight::CHANNEL_OFFSET, channel_len);

                    handler_.on_add_publication(channel, stream_id, correlation_id, client_id, true);
                    break;
                }

                case command::REMOVE_PUBLICATION:
                {
                    if (length < command::REMOVE_PUBLICATION_LEGACY_LENGTH)
                    {
                        report_error(0, ERROR_CODE_MALFORMED_COMMAND, "message too short for REMOVE_PUBLICATION");
                        break;
                    }
                    command::RemovePublicationFlyweight fw{buffer, 0};
                    correlation_id = fw.correlation_id();
                    const i64 registration_id = fw.registration_id();
                    const bool revoke = fw.revoke(length);

                    handler_.on_remove_publication(registration_id, correlation_id, revoke);
                    break;
                }

                case command::ADD_SUBSCRIPTION:
                {
                    if (length < command::SubscriptionMessageFlyweight::CHANNEL_OFFSET)
                    {
                        report_error(0, ERROR_CODE_MALFORMED_COMMAND, "message too short for ADD_SUBSCRIPTION");
                        break;
                    }
                    command::SubscriptionMessageFlyweight fw{buffer, 0};
                    correlation_id = fw.correlation_id();
                    const i64 client_id = fw.client_id();
                    const i32 stream_id = fw.stream_id();
                    const i32 channel_len = fw.channel_length();
                    if (channel_len < 0 ||
                        length < command::SubscriptionMessageFlyweight::CHANNEL_OFFSET ||
                        channel_len > length - command::SubscriptionMessageFlyweight::CHANNEL_OFFSET)
                    {
                        report_error(correlation_id, ERROR_CODE_MALFORMED_COMMAND, "invalid channel length in ADD_SUBSCRIPTION");
                        break;
                    }
                    auto channel = make_string_view(data, command::SubscriptionMessageFlyweight::CHANNEL_OFFSET, channel_len);

                    handler_.on_add_subscription(channel, stream_id, correlation_id, client_id);
                    break;
                }

                case command::REMOVE_SUBSCRIPTION:
                {
                    if (length < command::REMOVE_SUBSCRIPTION_LENGTH)
                    {
                        report_error(0, ERROR_CODE_MALFORMED_COMMAND, "message too short for REMOVE_SUBSCRIPTION");
                        break;
                    }
                    command::RemoveSubscriptionFlyweight fw{buffer, 0};
                    correlation_id = fw.correlation_id();
                    const i64 registration_id = fw.registration_id();

                    handler_.on_remove_subscription(registration_id, correlation_id);
                    break;
                }

                case command::ADD_DESTINATION:
                {
                    if (length < command::DESTINATION_MSG_MINIMUM_LENGTH)
                    {
                        report_error(0, ERROR_CODE_MALFORMED_COMMAND, "message too short for ADD_DESTINATION");
                        break;
                    }
                    command::DestinationMessageFlyweight fw{buffer, 0};
                    correlation_id = fw.correlation_id();
                    const i32 channel_len = fw.channel_length();
                    if (channel_len < 0 ||
                        length < command::DestinationMessageFlyweight::CHANNEL_OFFSET ||
                        channel_len > length - command::DestinationMessageFlyweight::CHANNEL_OFFSET)
                    {
                        report_error(correlation_id, ERROR_CODE_MALFORMED_COMMAND, "invalid channel length in ADD_DESTINATION");
                        break;
                    }
                    const i64 channel_registration_id = fw.registration_correlation_id();
                    auto channel = make_string_view(data, command::DestinationMessageFlyweight::CHANNEL_OFFSET, channel_len);

                    handler_.on_add_send_destination(channel_registration_id, channel, correlation_id);
                    break;
                }

                case command::REMOVE_DESTINATION:
                {
                    if (length < command::DESTINATION_MSG_MINIMUM_LENGTH)
                    {
                        report_error(0, ERROR_CODE_MALFORMED_COMMAND, "message too short for REMOVE_DESTINATION");
                        break;
                    }
                    command::DestinationMessageFlyweight fw{buffer, 0};
                    correlation_id = fw.correlation_id();
                    const i32 channel_len = fw.channel_length();
                    if (channel_len < 0 ||
                        length < command::DestinationMessageFlyweight::CHANNEL_OFFSET ||
                        channel_len > length - command::DestinationMessageFlyweight::CHANNEL_OFFSET)
                    {
                        report_error(correlation_id, ERROR_CODE_MALFORMED_COMMAND, "invalid channel length in REMOVE_DESTINATION");
                        break;
                    }
                    const i64 channel_registration_id = fw.registration_correlation_id();
                    auto channel = make_string_view(data, command::DestinationMessageFlyweight::CHANNEL_OFFSET, channel_len);

                    handler_.on_remove_send_destination(channel_registration_id, channel, correlation_id);
                    break;
                }

                case command::ADD_RCV_DESTINATION:
                {
                    if (length < command::DESTINATION_MSG_MINIMUM_LENGTH)
                    {
                        report_error(0, ERROR_CODE_MALFORMED_COMMAND, "message too short for ADD_RCV_DESTINATION");
                        break;
                    }
                    command::DestinationMessageFlyweight fw{buffer, 0};
                    correlation_id = fw.correlation_id();
                    const i32 channel_len = fw.channel_length();
                    if (channel_len < 0 ||
                        length < command::DestinationMessageFlyweight::CHANNEL_OFFSET ||
                        channel_len > length - command::DestinationMessageFlyweight::CHANNEL_OFFSET)
                    {
                        report_error(correlation_id, ERROR_CODE_MALFORMED_COMMAND, "invalid channel length in ADD_RCV_DESTINATION");
                        break;
                    }
                    const i64 channel_registration_id = fw.registration_correlation_id();
                    auto channel = make_string_view(data, command::DestinationMessageFlyweight::CHANNEL_OFFSET, channel_len);

                    handler_.on_add_rcv_destination(channel_registration_id, channel, correlation_id);
                    break;
                }

                case command::REMOVE_RCV_DESTINATION:
                {
                    if (length < command::DESTINATION_MSG_MINIMUM_LENGTH)
                    {
                        report_error(0, ERROR_CODE_MALFORMED_COMMAND, "message too short for REMOVE_RCV_DESTINATION");
                        break;
                    }
                    command::DestinationMessageFlyweight fw{buffer, 0};
                    correlation_id = fw.correlation_id();
                    const i32 channel_len = fw.channel_length();
                    if (channel_len < 0 ||
                        length < command::DestinationMessageFlyweight::CHANNEL_OFFSET ||
                        channel_len > length - command::DestinationMessageFlyweight::CHANNEL_OFFSET)
                    {
                        report_error(correlation_id, ERROR_CODE_MALFORMED_COMMAND, "invalid channel length in REMOVE_RCV_DESTINATION");
                        break;
                    }
                    const i64 channel_registration_id = fw.registration_correlation_id();
                    auto channel = make_string_view(data, command::DestinationMessageFlyweight::CHANNEL_OFFSET, channel_len);

                    handler_.on_remove_rcv_destination(channel_registration_id, channel, correlation_id);
                    break;
                }

                case command::CLIENT_KEEPALIVE:
                {
                    if (length < command::CORRELATED_MSG_LENGTH)
                    {
                        report_error(0, ERROR_CODE_MALFORMED_COMMAND, "message too short for CLIENT_KEEPALIVE");
                        break;
                    }
                    command::CorrelatedMessageFlyweight fw{buffer, 0};
                    handler_.on_client_keepalive(fw.client_id());
                    break;
                }

                case command::CLIENT_CLOSE:
                {
                    if (length < command::CORRELATED_MSG_LENGTH)
                    {
                        report_error(0, ERROR_CODE_MALFORMED_COMMAND, "message too short for CLIENT_CLOSE");
                        break;
                    }
                    command::CorrelatedMessageFlyweight fw{buffer, 0};
                    handler_.on_client_close(fw.client_id());
                    break;
                }

                case command::ADD_COUNTER:
                {
                    if (length < command::CounterMessageFlyweight::KEY_OFFSET)
                    {
                        report_error(0, ERROR_CODE_MALFORMED_COMMAND, "message too short for ADD_COUNTER");
                        break;
                    }
                    command::CounterMessageFlyweight fw{buffer, 0};
                    correlation_id = fw.correlation_id();
                    const i64 client_id = fw.client_id();

                    // Validate key_buffer_length is non-negative and key fits within message
                    const i32 key_len = fw.key_buffer_length();
                    if (key_len < 0 ||
                        key_len > length - command::CounterMessageFlyweight::KEY_OFFSET - SIZE_OF_INT)
                    {
                        report_error(correlation_id, ERROR_CODE_MALFORMED_COMMAND, "ADD_COUNTER key overflow");
                        break;
                    }

                    // Now safe to read label_length (stored at KEY_OFFSET + key_len)
                    const i32 label_len = fw.label_length();
                    const i32 label_offset = fw.label_buffer_offset();
                    if (label_len < 0 ||
                        label_offset > length ||
                        label_len > length - label_offset)
                    {
                        report_error(correlation_id, ERROR_CODE_MALFORMED_COMMAND, "ADD_COUNTER label overflow");
                        break;
                    }

                    handler_.on_add_counter(
                        fw.counter_type(),
                        data, fw.key_buffer_offset(), key_len,
                        data, label_offset, label_len,
                        correlation_id, client_id);
                    break;
                }

                case command::REMOVE_COUNTER:
                {
                    if (length < command::REMOVE_MSG_LENGTH)
                    {
                        report_error(0, ERROR_CODE_MALFORMED_COMMAND, "message too short for REMOVE_COUNTER");
                        break;
                    }
                    command::RemoveCounterFlyweight fw{buffer, 0};
                    correlation_id = fw.correlation_id();
                    const i64 registration_id = fw.registration_id();

                    handler_.on_remove_counter(registration_id, correlation_id);
                    break;
                }

                case command::TERMINATE_DRIVER:
                {
                    if (length < command::TerminateDriverFlyweight::TOKEN_OFFSET)
                    {
                        report_error(0, ERROR_CODE_MALFORMED_COMMAND, "message too short for TERMINATE_DRIVER");
                        break;
                    }
                    command::TerminateDriverFlyweight fw{buffer, 0};
                    const i32 token_len = fw.token_buffer_length();
                    if (token_len < 0 ||
                        token_len > length - command::TerminateDriverFlyweight::TOKEN_OFFSET)
                    {
                        report_error(0, ERROR_CODE_MALFORMED_COMMAND, "invalid token length in TERMINATE_DRIVER");
                        break;
                    }

                    handler_.on_terminate_driver(
                        data, command::TerminateDriverFlyweight::TOKEN_OFFSET, token_len);
                    break;
                }

                case command::ADD_STATIC_COUNTER:
                {
                    if (length < command::STATIC_COUNTER_MSG_MINIMUM_LENGTH)
                    {
                        report_error(0, ERROR_CODE_MALFORMED_COMMAND, "message too short for ADD_STATIC_COUNTER");
                        break;
                    }
                    command::StaticCounterMessageFlyweight fw{buffer, 0};
                    correlation_id = fw.correlation_id();
                    const i64 client_id = fw.client_id();

                    // Validate key_length is non-negative and key + aligned padding + label_length field fits within message
                    const i32 key_len = fw.key_length();
                    const i32 available_for_key = length - command::StaticCounterMessageFlyweight::KEY_BUFFER_OFFSET;
                    if (key_len < 0 || key_len > available_for_key ||
                        align(key_len, SIZE_OF_INT) + SIZE_OF_INT > available_for_key)
                    {
                        report_error(correlation_id, ERROR_CODE_MALFORMED_COMMAND, "ADD_STATIC_COUNTER key overflow");
                        break;
                    }

                    // Validate label_buffer_offset and label_length fit within message
                    const i32 label_buf_off = fw.label_buffer_offset();
                    const i32 label_len = fw.label_length();
                    if (label_buf_off < 0 || label_len < 0 ||
                        label_buf_off > length ||
                        label_len > length - label_buf_off)
                    {
                        report_error(correlation_id, ERROR_CODE_MALFORMED_COMMAND, "ADD_STATIC_COUNTER label overflow");
                        break;
                    }

                    handler_.on_add_static_counter(
                        fw.counter_type_id(),
                        data, command::StaticCounterMessageFlyweight::KEY_BUFFER_OFFSET, key_len,
                        data, label_buf_off, label_len,
                        fw.registration_id(), correlation_id, client_id);
                    break;
                }

                case command::REJECT_IMAGE:
                {
                    if (length < command::REJECT_IMAGE_MINIMUM_SIZE)
                    {
                        report_error(0, ERROR_CODE_MALFORMED_COMMAND, "message too short for REJECT_IMAGE");
                        break;
                    }
                    command::RejectImageFlyweight fw{buffer, 0};
                    correlation_id = fw.correlation_id();
                    const i32 reason_len = fw.reason_length();
                    if (reason_len < 0 ||
                        reason_len > length - command::RejectImageFlyweight::REASON_OFFSET)
                    {
                        report_error(correlation_id, ERROR_CODE_MALFORMED_COMMAND, "invalid reason length in REJECT_IMAGE");
                        break;
                    }
                    auto reason = make_string_view(data, command::RejectImageFlyweight::REASON_OFFSET, reason_len);

                    handler_.on_reject_image(
                        correlation_id,
                        fw.image_correlation_id(),
                        fw.position(),
                        reason);
                    break;
                }

                case command::REMOVE_DESTINATION_BY_ID:
                {
                    if (length < command::DESTINATION_BY_ID_MSG_LENGTH)
                    {
                        report_error(0, ERROR_CODE_MALFORMED_COMMAND, "message too short for REMOVE_DESTINATION_BY_ID");
                        break;
                    }
                    command::DestinationByIdMessageFlyweight fw{buffer, 0};
                    correlation_id = fw.correlation_id();

                    handler_.on_remove_send_destination_by_id(
                        fw.resource_registration_id(),
                        fw.destination_registration_id(),
                        correlation_id);
                    break;
                }

                case command::GET_NEXT_AVAILABLE_SESSION_ID:
                {
                    if (length < command::GET_NEXT_SESSION_ID_MSG_LENGTH)
                    {
                        report_error(0, ERROR_CODE_MALFORMED_COMMAND, "message too short for GET_NEXT_AVAILABLE_SESSION_ID");
                        break;
                    }
                    command::GetNextAvailableSessionIdMessageFlyweight fw{buffer, 0};
                    correlation_id = fw.correlation_id();

                    handler_.on_next_available_session_id(correlation_id, fw.stream_id());
                    break;
                }

                default:
                {
                    std::string msg = "unknown command typeId=" + std::to_string(msg_type_id);
                    report_error(correlation_id, ERROR_CODE_UNKNOWN_COMMAND, msg);
                    break;
                }
            }
        }
        catch (const std::exception& ex)
        {
            report_error(correlation_id, ERROR_CODE_GENERIC, ex.what());
        }
    }

    void report_error(i64 correlation_id, i32 error_code, std::string_view message)
    {
        if (on_error_)
            on_error_(correlation_id, error_code, message);
    }

    concurrent::ManyToOneRingBuffer& ring_buffer_;
    Handler& handler_;
    ErrorCallback on_error_;
};

} // namespace caeron::cnc
