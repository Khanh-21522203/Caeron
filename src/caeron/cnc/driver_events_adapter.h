#pragma once

#include "caeron/common/types.h"
#include "caeron/concurrent/broadcast_receiver.h"
#include "caeron/concurrent/unsafe_buffer.h"
#include "caeron/command/control_protocol_events.h"
#include "caeron/command/error_response_flyweight.h"
#include "caeron/command/publication_buffers_ready_flyweight.h"
#include "caeron/command/subscription_ready_flyweight.h"
#include "caeron/command/image_buffers_ready_flyweight.h"
#include "caeron/command/image_message_flyweight.h"
#include "caeron/command/operation_succeeded_flyweight.h"
#include "caeron/command/counter_update_flyweight.h"
#include "caeron/command/client_timeout_flyweight.h"
#include "caeron/command/static_counter_flyweight.h"
#include "caeron/command/next_available_session_id_flyweight.h"
#include "caeron/command/publication_error_frame_flyweight.h"

#include <functional>

namespace caeron::cnc {

/// Client-side adapter that reads responses from the to-clients broadcast
/// buffer and dispatches them to a handler (typically the ClientConductor).
///
/// The Handler template parameter must provide the following methods:
///
///   void on_error(i64 correlation_id, i32 error_code, std::string_view error_message);
///   void on_channel_endpoint_error(i64 correlation_id, std::string_view error_message);
///   void on_publication_ready(i64 correlation_id, i64 registration_id, i32 stream_id, i32 session_id, i32 position_counter_id, i32 channel_status_counter_id, std::string_view log_file_name);
///   void on_exclusive_publication_ready(i64 correlation_id, i64 registration_id, i32 stream_id, i32 session_id, i32 position_counter_id, i32 channel_status_counter_id, std::string_view log_file_name);
///   void on_subscription_ready(i64 correlation_id, i32 channel_status_counter_id);
///   void on_operation_success(i64 correlation_id);
///   void on_available_image(i64 correlation_id, i64 registration_id, i32 session_id, i32 position_counter_id, std::string_view log_file_name, std::string_view source_identity);
///   void on_unavailable_image(i64 correlation_id, i64 subscription_registration_id);
///   void on_counter_ready(i64 correlation_id, i32 counter_id);
///   void on_unavailable_counter(i64 correlation_id, i32 counter_id);
///   void on_client_timeout(i64 client_id);
///   void on_static_counter(i64 correlation_id, i32 counter_id);
///   void on_next_available_session_id(i64 correlation_id, i32 session_id);
///   void on_publication_error_frame(i64 registration_id, i64 destination_registration_id, i32 session_id, i32 stream_id, i64 receiver_id, i64 group_tag, i16 address_type, i16 udp_port, const u8* address, i32 error_code, std::string_view error_message);
template <typename Handler>
class DriverEventsAdapter
{
public:
    DriverEventsAdapter(concurrent::BroadcastReceiver& receiver,
                        Handler& handler,
                        i64 client_id)
        : receiver_{receiver}, handler_{handler}, client_id_{client_id}
    {}

    /// Receive and dispatch all available events from the broadcast buffer.
    ///
    /// @param active_correlation_id the correlation id currently being waited on.
    /// @return the number of events received.
    i32 receive(i64 active_correlation_id)
    {
        active_correlation_id_ = active_correlation_id;
        received_correlation_id_ = -1;

        return receiver_.receive([this](i32 msg_type_id, const std::byte* data, i32 length) {
            dispatch(msg_type_id, data, length);
        });
    }

    /// The correlation id received in response to the active command.
    [[nodiscard]] i64 received_correlation_id() const noexcept { return received_correlation_id_; }

private:
    void dispatch(i32 msg_type_id, const std::byte* data, i32 length)
    {
        // SAFETY: const_cast is required because flyweight constructors take a mutable
        // UnsafeBuffer&, but the broadcast receiver delivers const data. The flyweights
        // are only used for reading here (no setters called), so no actual mutation occurs.
        // This matches Java Aeron where MutableDirectBuffer wraps the same const region.
        concurrent::UnsafeBuffer buffer{const_cast<std::byte*>(data), length};

        switch (msg_type_id)
        {
            case command::ON_ERROR:
            {
                if (length < command::ErrorResponseFlyweight::ERROR_MESSAGE_OFFSET)
                    break;
                command::ErrorResponseFlyweight fw{buffer, 0};
                const i64 correlation_id = fw.offending_correlation_id();
                const i32 error_code = fw.error_code();
                const i32 error_msg_len = fw.error_message_length();
                if (error_msg_len < 0 ||
                    error_msg_len > length - command::ErrorResponseFlyweight::ERROR_MESSAGE_OFFSET)
                    break;
                std::string_view error_message{fw.error_message(),
                                               static_cast<size_t>(error_msg_len)};

                if (correlation_id == active_correlation_id_)
                {
                    received_correlation_id_ = correlation_id;
                }

                if (error_code == caeron::cnc::ERROR_CODE_CHANNEL_ENDPOINT)
                {
                    handler_.on_channel_endpoint_error(correlation_id, error_message);
                }
                else
                {
                    handler_.on_error(correlation_id, error_code, error_message);
                }
                break;
            }

            case command::ON_AVAILABLE_IMAGE:
            {
                if (length < command::IMAGE_BUFFERS_READY_MINIMUM_LENGTH)
                    break;
                command::ImageBuffersReadyFlyweight fw{buffer, 0};
                const i32 log_len = fw.log_file_name_length();
                if (log_len < 0 ||
                    log_len > length - command::ImageBuffersReadyFlyweight::LOG_FILE_NAME_OFFSET - SIZE_OF_INT)
                    break;
                const i32 src_id_offset = command::ImageBuffersReadyFlyweight::LOG_FILE_NAME_OFFSET + log_len;
                const i32 src_len = fw.source_identity_length();
                if (src_len < 0 ||
                    src_id_offset + SIZE_OF_INT > length ||
                    src_len > length - src_id_offset - SIZE_OF_INT)
                    break;

                handler_.on_available_image(
                    fw.correlation_id(),
                    fw.subscription_registration_id(),
                    fw.session_id(),
                    fw.subscriber_position_id(),
                    std::string_view{fw.log_file_name(), static_cast<size_t>(log_len)},
                    std::string_view{fw.source_identity(), static_cast<size_t>(src_len)});
                break;
            }

            case command::ON_PUBLICATION_READY:
            {
                if (length < command::PublicationBuffersReadyFlyweight::LOG_FILE_NAME_OFFSET)
                    break;
                command::PublicationBuffersReadyFlyweight fw{buffer, 0};
                const i64 correlation_id = fw.correlation_id();
                const i32 log_len = fw.log_file_name_length();
                if (log_len < 0 ||
                    log_len > length - command::PublicationBuffersReadyFlyweight::LOG_FILE_NAME_OFFSET)
                    break;

                if (correlation_id == active_correlation_id_)
                {
                    received_correlation_id_ = correlation_id;
                    handler_.on_publication_ready(
                        correlation_id,
                        fw.registration_id(),
                        fw.stream_id(),
                        fw.session_id(),
                        fw.pub_limit_counter_id(),
                        fw.channel_status_counter_id(),
                        std::string_view{fw.log_file_name(),
                                         static_cast<size_t>(log_len)});
                }
                break;
            }

            case command::ON_EXCLUSIVE_PUBLICATION_READY:
            {
                if (length < command::PublicationBuffersReadyFlyweight::LOG_FILE_NAME_OFFSET)
                    break;
                command::PublicationBuffersReadyFlyweight fw{buffer, 0};
                const i64 correlation_id = fw.correlation_id();
                const i32 log_len = fw.log_file_name_length();
                if (log_len < 0 ||
                    log_len > length - command::PublicationBuffersReadyFlyweight::LOG_FILE_NAME_OFFSET)
                    break;

                if (correlation_id == active_correlation_id_)
                {
                    received_correlation_id_ = correlation_id;
                    handler_.on_exclusive_publication_ready(
                        correlation_id,
                        fw.registration_id(),
                        fw.stream_id(),
                        fw.session_id(),
                        fw.pub_limit_counter_id(),
                        fw.channel_status_counter_id(),
                        std::string_view{fw.log_file_name(),
                                         static_cast<size_t>(log_len)});
                }
                break;
            }

            case command::ON_SUBSCRIPTION_READY:
            {
                if (length < command::SUBSCRIPTION_READY_LENGTH)
                    break;
                command::SubscriptionReadyFlyweight fw{buffer, 0};
                const i64 correlation_id = fw.correlation_id();

                if (correlation_id == active_correlation_id_)
                {
                    received_correlation_id_ = correlation_id;
                    handler_.on_subscription_ready(correlation_id, fw.channel_status_counter_id());
                }
                break;
            }

            case command::ON_OPERATION_SUCCESS:
            {
                if (length < command::OPERATION_SUCCEEDED_LENGTH)
                    break;
                command::OperationSucceededFlyweight fw{buffer, 0};
                const i64 correlation_id = fw.correlation_id();

                if (correlation_id == active_correlation_id_)
                {
                    received_correlation_id_ = correlation_id;
                    handler_.on_operation_success(correlation_id);
                }
                break;
            }

            case command::ON_UNAVAILABLE_IMAGE:
            {
                if (length < command::IMAGE_MESSAGE_MINIMUM_LENGTH)
                    break;
                command::ImageMessageFlyweight fw{buffer, 0};

                handler_.on_unavailable_image(fw.correlation_id(), fw.subscription_registration_id());
                break;
            }

            case command::ON_COUNTER_READY:
            {
                if (length < command::COUNTER_UPDATE_LENGTH)
                    break;
                command::CounterUpdateFlyweight fw{buffer, 0};
                const i64 correlation_id = fw.correlation_id();
                const i32 counter_id = fw.counter_id();

                if (correlation_id == active_correlation_id_)
                {
                    received_correlation_id_ = correlation_id;
                    handler_.on_counter_ready(correlation_id, counter_id);
                }
                break;
            }

            case command::ON_UNAVAILABLE_COUNTER:
            {
                if (length < command::COUNTER_UPDATE_LENGTH)
                    break;
                command::CounterUpdateFlyweight fw{buffer, 0};

                handler_.on_unavailable_counter(fw.correlation_id(), fw.counter_id());
                break;
            }

            case command::ON_CLIENT_TIMEOUT:
            {
                if (length < command::CLIENT_TIMEOUT_LENGTH)
                    break;
                command::ClientTimeoutFlyweight fw{buffer, 0};

                if (fw.client_id() == client_id_)
                {
                    handler_.on_client_timeout(fw.client_id());
                }
                break;
            }

            case command::ON_STATIC_COUNTER:
            {
                if (length < command::STATIC_COUNTER_LENGTH)
                    break;
                command::StaticCounterFlyweight fw{buffer, 0};
                const i64 correlation_id = fw.correlation_id();

                if (correlation_id == active_correlation_id_)
                {
                    received_correlation_id_ = correlation_id;
                    handler_.on_static_counter(correlation_id, fw.counter_id());
                }
                break;
            }

            case command::ON_PUBLICATION_ERROR:
            {
                if (length < command::PUBLICATION_ERROR_FRAME_LENGTH)
                    break;
                command::PublicationErrorFrameFlyweight fw{buffer, 0};
                const i32 err_msg_len = fw.error_message_length();
                if (err_msg_len < 0 ||
                    err_msg_len > length - command::PublicationErrorFrameFlyweight::ERROR_MESSAGE_OFFSET)
                    break;

                handler_.on_publication_error_frame(
                    fw.registration_id(),
                    fw.destination_registration_id(),
                    fw.session_id(),
                    fw.stream_id(),
                    fw.receiver_id(),
                    fw.group_tag(),
                    fw.address_type(),
                    fw.udp_port(),
                    fw.address(),
                    fw.error_code(),
                    std::string_view{fw.error_message(),
                                     static_cast<size_t>(err_msg_len)});
                break;
            }

            case command::ON_NEXT_AVAILABLE_SESSION_ID:
            {
                if (length < command::NEXT_AVAILABLE_SESSION_ID_LENGTH)
                    break;
                command::NextAvailableSessionIdFlyweight fw{buffer, 0};
                const i64 correlation_id = fw.correlation_id();

                if (correlation_id == active_correlation_id_)
                {
                    received_correlation_id_ = correlation_id;
                    handler_.on_next_available_session_id(correlation_id, fw.next_session_id());
                }
                break;
            }

            default:
                // Unknown event type — ignore.
                break;
        }
    }

    concurrent::BroadcastReceiver& receiver_;
    Handler& handler_;
    i64 client_id_;
    i64 active_correlation_id_{-1};
    i64 received_correlation_id_{-1};
};

} // namespace caeron::cnc
