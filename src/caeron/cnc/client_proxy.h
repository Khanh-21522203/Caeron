#pragma once

#include "caeron/common/types.h"
#include "caeron/concurrent/broadcast_transmitter.h"
#include "caeron/concurrent/unsafe_buffer.h"
#include "caeron/command/control_protocol_events.h"
#include "caeron/command/error_response_flyweight.h"
#include "caeron/command/publication_buffers_ready_flyweight.h"
#include "caeron/command/subscription_ready_flyweight.h"
#include "caeron/command/image_buffers_ready_flyweight.h"
#include "caeron/command/operation_succeeded_flyweight.h"
#include "caeron/command/image_message_flyweight.h"
#include "caeron/command/counter_update_flyweight.h"
#include "caeron/command/client_timeout_flyweight.h"
#include "caeron/command/static_counter_flyweight.h"
#include "caeron/command/next_available_session_id_flyweight.h"
#include "caeron/command/publication_error_frame_flyweight.h"

#include <climits>
#include <cstring>

namespace caeron::cnc {

/// Driver-side proxy for writing responses to the to-clients broadcast buffer.
///
/// Each method serializes a response flyweight into a scratch buffer and
/// transmits it via the BroadcastTransmitter.
class ClientProxy
{
public:
    explicit ClientProxy(concurrent::BroadcastTransmitter& transmitter)
        : transmitter_{transmitter}
    {}

    /// Send an error response to the client.
    ///
    /// @param correlation_id of the offending command.
    /// @param error_code numeric error code.
    /// @param error_message human-readable error description.
    void on_error(i64 correlation_id, i32 error_code, const char* error_message)
    {
        if (error_message == nullptr) error_message = "";
        const i32 msg_len = command::ErrorResponseFlyweight::compute_length(safe_strlen(error_message));
        validate_scratch_length(msg_len);

        concurrent::UnsafeBuffer scratch{scratch_, msg_len};
        command::ErrorResponseFlyweight fw{scratch, 0};
        fw.set_offending_correlation_id(correlation_id);
        fw.set_error_code(error_code);
        fw.set_error_message(error_message, safe_strlen(error_message));

        transmit(command::ON_ERROR, msg_len);
    }

    /// Notify the client that a publication is ready.
    ///
    /// @param correlation_id of the original command.
    /// @param registration_id of the publication.
    /// @param stream_id of the publication.
    /// @param session_id of the publication.
    /// @param log_file_name path to the log buffer file.
    /// @param position_counter_id counter id for the publication limit position.
    /// @param channel_status_counter_id counter id for channel status.
    /// @param is_exclusive whether this is an exclusive publication.
    void on_publication_ready(i64 correlation_id,
                              i64 registration_id,
                              i32 stream_id,
                              i32 session_id,
                              const char* log_file_name,
                              i32 position_counter_id,
                              i32 channel_status_counter_id,
                              bool is_exclusive)
    {
        if (log_file_name == nullptr) log_file_name = "";
        const i32 log_len = safe_strlen(log_file_name);
        const i32 msg_len = command::PublicationBuffersReadyFlyweight::compute_length(log_len);
        validate_scratch_length(msg_len);

        concurrent::UnsafeBuffer scratch{scratch_, msg_len};
        command::PublicationBuffersReadyFlyweight fw{scratch, 0};
        fw.set_correlation_id(correlation_id);
        fw.set_registration_id(registration_id);
        fw.set_session_id(session_id);
        fw.set_stream_id(stream_id);
        fw.set_pub_limit_counter_id(position_counter_id);
        fw.set_channel_status_counter_id(channel_status_counter_id);
        fw.set_log_file_name(log_file_name, log_len);

        const i32 msg_type = is_exclusive ? command::ON_EXCLUSIVE_PUBLICATION_READY
                                          : command::ON_PUBLICATION_READY;
        transmit(static_cast<u16>(msg_type), msg_len);
    }

    /// Notify the client that a subscription is ready.
    ///
    /// @param correlation_id of the original command.
    /// @param channel_status_counter_id counter id for channel status.
    void on_subscription_ready(i64 correlation_id, i32 channel_status_counter_id)
    {
        concurrent::UnsafeBuffer scratch{scratch_, command::SUBSCRIPTION_READY_LENGTH};
        command::SubscriptionReadyFlyweight fw{scratch, 0};
        fw.set_correlation_id(correlation_id);
        fw.set_channel_status_counter_id(channel_status_counter_id);

        transmit(command::ON_SUBSCRIPTION_READY, command::SUBSCRIPTION_READY_LENGTH);
    }

    /// Notify the client that an operation succeeded.
    ///
    /// @param correlation_id of the original command.
    void operation_succeeded(i64 correlation_id)
    {
        concurrent::UnsafeBuffer scratch{scratch_, command::OPERATION_SUCCEEDED_LENGTH};
        command::OperationSucceededFlyweight fw{scratch, 0};
        fw.set_correlation_id(correlation_id);

        transmit(command::ON_OPERATION_SUCCESS, command::OPERATION_SUCCEEDED_LENGTH);
    }

    /// Notify the client that an image is available.
    ///
    /// @param correlation_id of the image.
    /// @param stream_id of the image.
    /// @param session_id of the image.
    /// @param subscription_registration_id of the subscription.
    /// @param position_counter_id counter id for the subscriber position.
    /// @param log_file_name path to the log buffer file.
    /// @param source_identity source identity string (may be empty).
    void on_available_image(i64 correlation_id,
                            i32 stream_id,
                            i32 session_id,
                            i64 subscription_registration_id,
                            i32 position_counter_id,
                            const char* log_file_name,
                            const char* source_identity = "")
    {
        if (log_file_name == nullptr) log_file_name = "";
        if (source_identity == nullptr) source_identity = "";
        const i32 log_len = safe_strlen(log_file_name);
        const i32 src_len = safe_strlen(source_identity);
        const i32 msg_len = command::ImageBuffersReadyFlyweight::compute_length(log_len, src_len);
        validate_scratch_length(msg_len);

        concurrent::UnsafeBuffer scratch{scratch_, msg_len};
        command::ImageBuffersReadyFlyweight fw{scratch, 0};
        fw.set_correlation_id(correlation_id);
        fw.set_session_id(session_id);
        fw.set_stream_id(stream_id);
        fw.set_subscription_registration_id(subscription_registration_id);
        fw.set_subscriber_position_id(position_counter_id);
        fw.set_log_file_name(log_file_name, log_len);
        fw.set_source_identity(source_identity, src_len);

        transmit(command::ON_AVAILABLE_IMAGE, msg_len);
    }

    /// Notify the client that an image has become unavailable.
    ///
    /// @param correlation_id of the image.
    /// @param subscription_registration_id of the subscription.
    void on_unavailable_image(i64 correlation_id, i64 subscription_registration_id)
    {
        const i32 msg_len = command::IMAGE_MESSAGE_MINIMUM_LENGTH;

        concurrent::UnsafeBuffer scratch{scratch_, msg_len};
        command::ImageMessageFlyweight fw{scratch, 0};
        fw.set_correlation_id(correlation_id);
        fw.set_subscription_registration_id(subscription_registration_id);
        fw.set_stream_id(0);

        transmit(command::ON_UNAVAILABLE_IMAGE, msg_len);
    }

    /// Notify the client that a counter is ready.
    ///
    /// @param correlation_id of the original command.
    /// @param counter_id of the new counter.
    void on_counter_ready(i64 correlation_id, i32 counter_id)
    {
        concurrent::UnsafeBuffer scratch{scratch_, command::COUNTER_UPDATE_LENGTH};
        command::CounterUpdateFlyweight fw{scratch, 0};
        fw.set_correlation_id(correlation_id);
        fw.set_counter_id(counter_id);

        transmit(command::ON_COUNTER_READY, command::COUNTER_UPDATE_LENGTH);
    }

    /// Notify the client that a counter is unavailable.
    ///
    /// @param registration_id of the counter.
    /// @param counter_id of the counter.
    void on_unavailable_counter(i64 registration_id, i32 counter_id)
    {
        concurrent::UnsafeBuffer scratch{scratch_, command::COUNTER_UPDATE_LENGTH};
        command::CounterUpdateFlyweight fw{scratch, 0};
        fw.set_correlation_id(registration_id);
        fw.set_counter_id(counter_id);

        transmit(command::ON_UNAVAILABLE_COUNTER, command::COUNTER_UPDATE_LENGTH);
    }

    /// Notify the client of a client timeout.
    ///
    /// @param client_id of the timed-out client.
    void on_client_timeout(i64 client_id)
    {
        concurrent::UnsafeBuffer scratch{scratch_, command::CLIENT_TIMEOUT_LENGTH};
        command::ClientTimeoutFlyweight fw{scratch, 0};
        fw.set_client_id(client_id);

        transmit(command::ON_CLIENT_TIMEOUT, command::CLIENT_TIMEOUT_LENGTH);
    }

    /// Notify the client that a static counter is ready.
    ///
    /// @param correlation_id of the original command.
    /// @param counter_id of the static counter.
    void on_static_counter(i64 correlation_id, i32 counter_id)
    {
        concurrent::UnsafeBuffer scratch{scratch_, command::STATIC_COUNTER_LENGTH};
        command::StaticCounterFlyweight fw{scratch, 0};
        fw.set_correlation_id(correlation_id);
        fw.set_counter_id(counter_id);

        transmit(command::ON_STATIC_COUNTER, command::STATIC_COUNTER_LENGTH);
    }

    /// Notify the client of the next available session id.
    ///
    /// @param correlation_id of the original command.
    /// @param session_id the next available session id.
    void on_next_available_session_id(i64 correlation_id, i32 session_id)
    {
        concurrent::UnsafeBuffer scratch{scratch_, command::NEXT_AVAILABLE_SESSION_ID_LENGTH};
        command::NextAvailableSessionIdFlyweight fw{scratch, 0};
        fw.set_correlation_id(correlation_id);
        fw.set_next_session_id(session_id);

        transmit(command::ON_NEXT_AVAILABLE_SESSION_ID, command::NEXT_AVAILABLE_SESSION_ID_LENGTH);
    }

    /// Notify the client of a publication error frame.
    ///
    /// @param registration_id of the publication.
    /// @param destination_registration_id of the destination.
    /// @param session_id of the publication.
    /// @param stream_id of the publication.
    /// @param receiver_id of the receiver.
    /// @param group_tag of the group.
    /// @param address_type of the endpoint.
    /// @param udp_port of the endpoint.
    /// @param address 16-byte address (IPv4 or IPv6).
    /// @param error_code numeric error code.
    /// @param error_message human-readable error message.
    void on_publication_error_frame(i64 registration_id,
                                    i64 destination_registration_id,
                                    i32 session_id,
                                    i32 stream_id,
                                    i64 receiver_id,
                                    i64 group_tag,
                                    i16 address_type,
                                    i16 udp_port,
                                    const u8* address,
                                    i32 error_code,
                                    const char* error_message)
    {
        if (error_message == nullptr) error_message = "";
        const i32 err_msg_len = safe_strlen(error_message);
        const i32 total_len = command::PublicationErrorFrameFlyweight::compute_length(err_msg_len);
        validate_scratch_length(total_len);

        concurrent::UnsafeBuffer scratch{scratch_, total_len};
        command::PublicationErrorFrameFlyweight fw{scratch, 0};
        fw.set_registration_id(registration_id);
        fw.set_destination_registration_id(destination_registration_id);
        fw.set_session_id(session_id);
        fw.set_stream_id(stream_id);
        fw.set_receiver_id(receiver_id);
        fw.set_group_tag(group_tag);
        fw.set_address_type(address_type);
        fw.set_udp_port(udp_port);
        if (address != nullptr)
            fw.set_address(address, command::PublicationErrorFrameFlyweight::ADDRESS_LENGTH);
        else
            scratch.set_memory(command::PublicationErrorFrameFlyweight::ADDRESS_OFFSET,
                               command::PublicationErrorFrameFlyweight::ADDRESS_LENGTH, 0);
        fw.set_error_code(error_code);
        fw.set_error_message(error_message, err_msg_len);

        transmit(command::ON_PUBLICATION_ERROR, total_len);
    }

private:
    /// Safely convert strlen result to i32, throwing if the string is too long.
    /// Prevents silent truncation when size_t exceeds INT_MAX.
    static i32 safe_strlen(const char* str)
    {
        const size_t len = std::strlen(str);
        if (len > static_cast<size_t>(INT_MAX))
            throw std::runtime_error("string length exceeds INT_MAX");
        return static_cast<i32>(len);
    }

    void validate_scratch_length(i32 length) const
    {
        if (length < 0 || length > SCRATCH_BUFFER_SIZE)
            throw std::runtime_error("response exceeds scratch buffer size (" +
                                     std::to_string(SCRATCH_BUFFER_SIZE) + " bytes)");
    }

    void transmit(u16 msg_type_id, i32 length)
    {
        transmitter_.transmit(msg_type_id, scratch_, length);
    }

    concurrent::BroadcastTransmitter& transmitter_;

    /// Scratch buffer for serializing responses before transmitting.
    /// Size must be large enough for the largest possible response. If
    /// responses with variable-length fields (error message, log file name,
    /// etc.) exceed this limit, a runtime_error is thrown.
    static constexpr i32 SCRATCH_BUFFER_SIZE = 2048;
    std::byte scratch_[SCRATCH_BUFFER_SIZE]{};
};

} // namespace caeron::cnc
