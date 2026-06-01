#pragma once

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

#include <atomic>
#include <climits>
#include <cstring>
#include <stdexcept>

namespace caeron::cnc {

/// Client-side proxy for writing commands to the to-driver ring buffer.
///
/// Each method serializes a command flyweight into a scratch buffer and writes
/// it to the ring buffer. Correlation IDs are generated via an internal atomic
/// counter.
///
/// Thread safety: NOT thread safe. Callers must synchronize externally (matching
/// Java Aeron's assumption that DriverProxy is called under clientLock).
class DriverProxy
{
public:
    static constexpr i64 NULL_VALUE = -1;

    DriverProxy(concurrent::ManyToOneRingBuffer& ring_buffer, i64 client_id)
        : ring_buffer_{ring_buffer}, client_id_{client_id}
    {}

    /// Time of the last heartbeat from the driver.
    ///
    /// In Java Aeron this is stored as the consumer_heartbeat_value in the
    /// ManyToOneRingBuffer's metadata region. For now we return the ring buffer
    /// head position as a proxy for driver liveness — a non-advancing head
    /// indicates the driver is not consuming.
    [[nodiscard]] i64 time_of_last_driver_keepalive_ms() const noexcept
    {
        return ring_buffer_.consumer_heartbeat_time();
    }

    /// Instruct the driver to add a concurrent publication.
    ///
    /// @param channel  URI in string format.
    /// @param stream_id within the channel.
    /// @return the correlation id for the command.
    i64 add_publication(const char* channel, i32 stream_id)
    {
        if (channel == nullptr) channel = "";
        const i64 correlation_id = next_correlation_id();
        const i32 channel_len = safe_strlen(channel);
        const i32 msg_len = command::PublicationMessageFlyweight::compute_length(channel_len);
        validate_scratch_length(msg_len);

        concurrent::UnsafeBuffer scratch{scratch_, msg_len};
        command::PublicationMessageFlyweight fw{scratch, 0};
        fw.set_client_id(client_id_);
        fw.set_correlation_id(correlation_id);
        fw.set_stream_id(stream_id);
        fw.set_channel(channel, channel_len);

        if (!ring_buffer_.write(command::ADD_PUBLICATION, scratch_, msg_len))
            throw std::runtime_error("failed to write add publication command");

        return correlation_id;
    }

    /// Instruct the driver to add an exclusive publication.
    ///
    /// @param channel  URI in string format.
    /// @param stream_id within the channel.
    /// @return the correlation id for the command.
    i64 add_exclusive_publication(const char* channel, i32 stream_id)
    {
        if (channel == nullptr) channel = "";
        const i64 correlation_id = next_correlation_id();
        const i32 channel_len = safe_strlen(channel);
        const i32 msg_len = command::PublicationMessageFlyweight::compute_length(channel_len);
        validate_scratch_length(msg_len);

        concurrent::UnsafeBuffer scratch{scratch_, msg_len};
        command::PublicationMessageFlyweight fw{scratch, 0};
        fw.set_client_id(client_id_);
        fw.set_correlation_id(correlation_id);
        fw.set_stream_id(stream_id);
        fw.set_channel(channel, channel_len);

        if (!ring_buffer_.write(command::ADD_EXCLUSIVE_PUBLICATION, scratch_, msg_len))
            throw std::runtime_error("failed to write add exclusive publication command");

        return correlation_id;
    }

    /// Instruct the driver to remove a publication by its registration id.
    ///
    /// @param registration_id for the publication to be removed.
    /// @param revoke whether the publication is being revoked.
    /// @return the correlation id for the command.
    i64 remove_publication(i64 registration_id, bool revoke)
    {
        const i64 correlation_id = next_correlation_id();

        concurrent::UnsafeBuffer scratch{scratch_, command::REMOVE_PUBLICATION_LENGTH};
        command::RemovePublicationFlyweight fw{scratch, 0};
        fw.set_client_id(client_id_);
        fw.set_correlation_id(correlation_id);
        fw.set_registration_id(registration_id);
        fw.set_revoke(revoke);

        if (!ring_buffer_.write(command::REMOVE_PUBLICATION, scratch_, command::REMOVE_PUBLICATION_LENGTH))
            throw std::runtime_error("failed to write remove publication command");

        return correlation_id;
    }

    /// Instruct the driver to add a subscription.
    ///
    /// @param channel  URI in string format.
    /// @param stream_id within the channel.
    /// @return the correlation id for the command.
    i64 add_subscription(const char* channel, i32 stream_id)
    {
        if (channel == nullptr) channel = "";
        const i64 correlation_id = next_correlation_id();
        const i32 channel_len = safe_strlen(channel);
        const i32 msg_len = command::SubscriptionMessageFlyweight::compute_length(channel_len);
        validate_scratch_length(msg_len);

        concurrent::UnsafeBuffer scratch{scratch_, msg_len};
        command::SubscriptionMessageFlyweight fw{scratch, 0};
        fw.set_client_id(client_id_);
        fw.set_correlation_id(correlation_id);
        fw.set_registration_correlation_id(NULL_VALUE);
        fw.set_stream_id(stream_id);
        fw.set_channel(channel, channel_len);

        if (!ring_buffer_.write(command::ADD_SUBSCRIPTION, scratch_, msg_len))
            throw std::runtime_error("failed to write add subscription command");

        return correlation_id;
    }

    /// Instruct the driver to remove a subscription by its registration id.
    ///
    /// @param registration_id for the subscription to be removed.
    /// @return the correlation id for the command.
    i64 remove_subscription(i64 registration_id)
    {
        const i64 correlation_id = next_correlation_id();

        concurrent::UnsafeBuffer scratch{scratch_, command::REMOVE_SUBSCRIPTION_LENGTH};
        command::RemoveSubscriptionFlyweight fw{scratch, 0};
        fw.set_client_id(client_id_);
        fw.set_correlation_id(correlation_id);
        fw.set_registration_id(registration_id);

        if (!ring_buffer_.write(command::REMOVE_SUBSCRIPTION, scratch_, command::REMOVE_SUBSCRIPTION_LENGTH))
            throw std::runtime_error("failed to write remove subscription command");

        return correlation_id;
    }

    /// Add a destination to the send channel of an existing MDC Publication.
    ///
    /// @param registration_id of the Publication.
    /// @param endpoint_channel for the destination.
    /// @return the correlation id for the command.
    i64 add_destination(i64 registration_id, const char* endpoint_channel)
    {
        if (endpoint_channel == nullptr) endpoint_channel = "";
        const i64 correlation_id = next_correlation_id();
        const i32 channel_len = safe_strlen(endpoint_channel);
        const i32 msg_len = command::DestinationMessageFlyweight::compute_length(channel_len);
        validate_scratch_length(msg_len);

        concurrent::UnsafeBuffer scratch{scratch_, msg_len};
        command::DestinationMessageFlyweight fw{scratch, 0};
        fw.set_client_id(static_cast<i64>(client_id_));
        fw.set_correlation_id(correlation_id);
        fw.set_registration_correlation_id(registration_id);
        fw.set_channel(endpoint_channel, channel_len);

        if (!ring_buffer_.write(command::ADD_DESTINATION, scratch_, msg_len))
            throw std::runtime_error("failed to write add destination command");

        return correlation_id;
    }

    /// Remove a destination from the send channel of an existing MDC Publication.
    ///
    /// @param registration_id of the Publication.
    /// @param endpoint_channel used for the add_destination command.
    /// @return the correlation id for the command.
    i64 remove_destination(i64 registration_id, const char* endpoint_channel)
    {
        if (endpoint_channel == nullptr) endpoint_channel = "";
        const i64 correlation_id = next_correlation_id();
        const i32 channel_len = safe_strlen(endpoint_channel);
        const i32 msg_len = command::DestinationMessageFlyweight::compute_length(channel_len);
        validate_scratch_length(msg_len);

        concurrent::UnsafeBuffer scratch{scratch_, msg_len};
        command::DestinationMessageFlyweight fw{scratch, 0};
        fw.set_client_id(static_cast<i64>(client_id_));
        fw.set_correlation_id(correlation_id);
        fw.set_registration_correlation_id(registration_id);
        fw.set_channel(endpoint_channel, channel_len);

        if (!ring_buffer_.write(command::REMOVE_DESTINATION, scratch_, msg_len))
            throw std::runtime_error("failed to write remove destination command");

        return correlation_id;
    }

    /// Remove a destination by registration id.
    ///
    /// @param publication_registration_id of the Publication.
    /// @param destination_registration_id of the destination.
    /// @return the correlation id for the command.
    i64 remove_destination_by_id(i64 publication_registration_id, i64 destination_registration_id)
    {
        const i64 correlation_id = next_correlation_id();

        concurrent::UnsafeBuffer scratch{scratch_, command::DESTINATION_BY_ID_MSG_LENGTH};
        command::DestinationByIdMessageFlyweight fw{scratch, 0};
        fw.set_client_id(static_cast<i64>(client_id_));
        fw.set_correlation_id(correlation_id);
        fw.set_resource_registration_id(publication_registration_id);
        fw.set_destination_registration_id(destination_registration_id);

        if (!ring_buffer_.write(command::REMOVE_DESTINATION_BY_ID, scratch_, command::DESTINATION_BY_ID_MSG_LENGTH))
            throw std::runtime_error("failed to write remove destination by id command");

        return correlation_id;
    }

    /// Add a destination to the receive channel endpoint of an existing MDS Subscription.
    ///
    /// @param registration_id of the Subscription.
    /// @param endpoint_channel for the destination.
    /// @return the correlation id for the command.
    i64 add_rcv_destination(i64 registration_id, const char* endpoint_channel)
    {
        if (endpoint_channel == nullptr) endpoint_channel = "";
        const i64 correlation_id = next_correlation_id();
        const i32 channel_len = safe_strlen(endpoint_channel);
        const i32 msg_len = command::DestinationMessageFlyweight::compute_length(channel_len);
        validate_scratch_length(msg_len);

        concurrent::UnsafeBuffer scratch{scratch_, msg_len};
        command::DestinationMessageFlyweight fw{scratch, 0};
        fw.set_client_id(static_cast<i64>(client_id_));
        fw.set_correlation_id(correlation_id);
        fw.set_registration_correlation_id(registration_id);
        fw.set_channel(endpoint_channel, channel_len);

        if (!ring_buffer_.write(command::ADD_RCV_DESTINATION, scratch_, msg_len))
            throw std::runtime_error("failed to write add rcv destination command");

        return correlation_id;
    }

    /// Remove a destination from the receive channel endpoint of an existing MDS Subscription.
    ///
    /// @param registration_id of the Subscription.
    /// @param endpoint_channel used for the add_rcv_destination command.
    /// @return the correlation id for the command.
    i64 remove_rcv_destination(i64 registration_id, const char* endpoint_channel)
    {
        if (endpoint_channel == nullptr) endpoint_channel = "";
        const i64 correlation_id = next_correlation_id();
        const i32 channel_len = safe_strlen(endpoint_channel);
        const i32 msg_len = command::DestinationMessageFlyweight::compute_length(channel_len);
        validate_scratch_length(msg_len);

        concurrent::UnsafeBuffer scratch{scratch_, msg_len};
        command::DestinationMessageFlyweight fw{scratch, 0};
        fw.set_client_id(static_cast<i64>(client_id_));
        fw.set_correlation_id(correlation_id);
        fw.set_registration_correlation_id(registration_id);
        fw.set_channel(endpoint_channel, channel_len);

        if (!ring_buffer_.write(command::REMOVE_RCV_DESTINATION, scratch_, msg_len))
            throw std::runtime_error("failed to write remove rcv destination command");

        return correlation_id;
    }

    /// Add a new counter with a type id, key, and label.
    ///
    /// @param type_id for associating with the counter.
    /// @param key_buffer containing the metadata key.
    /// @param key_offset offset at which the key begins.
    /// @param key_length length in bytes for the key.
    /// @param label_buffer containing the label.
    /// @param label_offset offset at which the label begins.
    /// @param label_length length in bytes for the label.
    /// @return the correlation id for the command.
    i64 add_counter(i32 type_id,
                    const void* key_buffer, i32 key_offset, i32 key_length,
                    const void* label_buffer, i32 label_offset, i32 label_length)
    {
        if (key_length < 0 || label_length < 0)
            throw std::invalid_argument("negative buffer length in add_counter");
        if (key_offset < 0 || label_offset < 0)
            throw std::invalid_argument("negative buffer offset in add_counter");
        if (key_buffer == nullptr) key_length = 0;
        if (label_buffer == nullptr) label_length = 0;

        const i64 correlation_id = next_correlation_id();
        const i32 msg_len = command::CounterMessageFlyweight::compute_length(key_length, label_length);
        validate_scratch_length(msg_len);

        concurrent::UnsafeBuffer scratch{scratch_, msg_len};
        command::CounterMessageFlyweight fw{scratch, 0};
        fw.set_client_id(client_id_);
        fw.set_correlation_id(correlation_id);
        fw.set_counter_type(type_id);
        if (key_buffer != nullptr && key_length > 0)
            fw.set_key_buffer(static_cast<const u8*>(key_buffer) + key_offset, key_length);
        else
            fw.set_key_buffer(nullptr, 0);
        if (label_buffer != nullptr && label_length > 0)
            fw.set_label(static_cast<const u8*>(label_buffer) + label_offset, label_length);
        else
            fw.set_label(nullptr, 0);

        if (!ring_buffer_.write(command::ADD_COUNTER, scratch_, msg_len))
            throw std::runtime_error("failed to write add counter command");

        return correlation_id;
    }

    /// Add a new counter with a type id and label (no key).
    ///
    /// @param type_id for associating with the counter.
    /// @param label human-readable label for the counter.
    /// @return the correlation id for the command.
    i64 add_counter(i32 type_id, const char* label)
    {
        if (label == nullptr) label = "";
        const i64 correlation_id = next_correlation_id();
        const i32 label_len = safe_strlen(label);
        const i32 msg_len = command::CounterMessageFlyweight::compute_length(0, label_len);
        validate_scratch_length(msg_len);

        concurrent::UnsafeBuffer scratch{scratch_, msg_len};
        command::CounterMessageFlyweight fw{scratch, 0};
        fw.set_client_id(client_id_);
        fw.set_correlation_id(correlation_id);
        fw.set_counter_type(type_id);
        fw.set_key_buffer(nullptr, 0);
        fw.set_label(label, label_len);

        if (!ring_buffer_.write(command::ADD_COUNTER, scratch_, msg_len))
            throw std::runtime_error("failed to write add counter command");

        return correlation_id;
    }

    /// Instruct the media driver to remove an existing counter by its registration id.
    ///
    /// @param registration_id of counter to remove.
    /// @return the correlation id for the command.
    i64 remove_counter(i64 registration_id)
    {
        const i64 correlation_id = next_correlation_id();

        concurrent::UnsafeBuffer scratch{scratch_, command::REMOVE_MSG_LENGTH};
        command::RemoveCounterFlyweight fw{scratch, 0};
        fw.set_client_id(client_id_);
        fw.set_correlation_id(correlation_id);
        fw.set_registration_id(registration_id);

        if (!ring_buffer_.write(command::REMOVE_COUNTER, scratch_, command::REMOVE_MSG_LENGTH))
            throw std::runtime_error("failed to write remove counter command");

        return correlation_id;
    }

    /// Notify the media driver that this client is closing.
    void client_close()
    {
        concurrent::UnsafeBuffer scratch{scratch_, command::CORRELATED_MSG_LENGTH};
        command::CorrelatedMessageFlyweight fw{scratch, 0};
        fw.set_client_id(client_id_);
        fw.set_correlation_id(NULL_VALUE);

        if (!ring_buffer_.write(command::CLIENT_CLOSE, scratch_, command::CORRELATED_MSG_LENGTH))
            throw std::runtime_error("failed to write client close command");
    }

    /// Instruct the media driver to terminate.
    ///
    /// @param token_buffer containing the authentication token.
    /// @param token_offset at which the token begins.
    /// @param token_length in bytes.
    /// @return true if successfully sent.
    bool terminate_driver(const void* token_buffer, i32 token_offset, i32 token_length)
    {
        if (token_length < 0)
            throw std::invalid_argument("negative buffer length in terminate_driver");
        if (token_offset < 0)
            throw std::invalid_argument("negative buffer offset in terminate_driver");
        if (token_buffer == nullptr) token_length = 0;

        const i32 msg_len = command::TerminateDriverFlyweight::compute_length(token_length);
        validate_scratch_length(msg_len);

        concurrent::UnsafeBuffer scratch{scratch_, msg_len};
        command::TerminateDriverFlyweight fw{scratch, 0};
        fw.set_client_id(client_id_);
        fw.set_correlation_id(NULL_VALUE);
        if (token_buffer != nullptr && token_length > 0)
            fw.set_token_buffer(static_cast<const char*>(token_buffer) + token_offset, token_length);
        else
            fw.set_token_buffer(nullptr, 0);

        return ring_buffer_.write(command::TERMINATE_DRIVER, scratch_, msg_len);
    }

    /// Reject a specific image.
    ///
    /// @param image_correlation_id of the image to be rejected.
    /// @param position of the image when rejection occurred.
    /// @param reason user supplied reason for rejection.
    /// @return the correlation id of the request.
    i64 reject_image(i64 image_correlation_id, i64 position, const char* reason)
    {
        if (reason == nullptr) reason = "";
        const i64 correlation_id = next_correlation_id();
        const i32 reason_len = safe_strlen(reason);
        const i32 msg_len = command::RejectImageFlyweight::compute_length(reason_len);
        validate_scratch_length(msg_len);

        concurrent::UnsafeBuffer scratch{scratch_, msg_len};
        command::RejectImageFlyweight fw{scratch, 0};
        fw.set_client_id(static_cast<i64>(client_id_));
        fw.set_correlation_id(correlation_id);
        fw.set_image_correlation_id(image_correlation_id);
        fw.set_position(position);
        fw.set_reason(reason, reason_len);

        if (!ring_buffer_.write(command::REJECT_IMAGE, scratch_, msg_len))
            throw std::runtime_error("failed to write reject image command");

        return correlation_id;
    }

    /// Add a static counter with a type id, key, label, and registration id.
    i64 add_static_counter(i32 type_id,
                           const void* key_buffer, i32 key_offset, i32 key_length,
                           const void* label_buffer, i32 label_offset, i32 label_length,
                           i64 registration_id)
    {
        if (key_length < 0 || label_length < 0)
            throw std::invalid_argument("negative buffer length in add_static_counter");
        if (key_offset < 0 || label_offset < 0)
            throw std::invalid_argument("negative buffer offset in add_static_counter");
        if (key_buffer == nullptr) key_length = 0;
        if (label_buffer == nullptr) label_length = 0;

        const i64 correlation_id = next_correlation_id();
        const i32 msg_len = command::StaticCounterMessageFlyweight::compute_length(key_length, label_length);
        validate_scratch_length(msg_len);

        concurrent::UnsafeBuffer scratch{scratch_, msg_len};
        command::StaticCounterMessageFlyweight fw{scratch, 0};
        fw.set_client_id(static_cast<i64>(client_id_));
        fw.set_correlation_id(correlation_id);
        fw.set_registration_id(registration_id);
        fw.set_counter_type_id(type_id);
        if (key_buffer != nullptr && key_length > 0)
            fw.set_key_buffer(static_cast<const u8*>(key_buffer) + key_offset, key_length);
        else
            fw.set_key_buffer(nullptr, 0);
        if (label_buffer != nullptr && label_length > 0)
            fw.set_label(static_cast<const u8*>(label_buffer) + label_offset, label_length);
        else
            fw.set_label(nullptr, 0);

        if (!ring_buffer_.write(command::ADD_STATIC_COUNTER, scratch_, msg_len))
            throw std::runtime_error("failed to write add static counter command");

        return correlation_id;
    }

    /// Add a static counter with a type id and label (no key).
    i64 add_static_counter(i32 type_id, const char* label, i64 registration_id)
    {
        if (label == nullptr) label = "";
        const i64 correlation_id = next_correlation_id();
        const i32 label_len = safe_strlen(label);
        const i32 msg_len = command::StaticCounterMessageFlyweight::compute_length(0, label_len);
        validate_scratch_length(msg_len);

        concurrent::UnsafeBuffer scratch{scratch_, msg_len};
        command::StaticCounterMessageFlyweight fw{scratch, 0};
        fw.set_client_id(static_cast<i64>(client_id_));
        fw.set_correlation_id(correlation_id);
        fw.set_registration_id(registration_id);
        fw.set_counter_type_id(type_id);
        fw.set_key_buffer(nullptr, 0);
        fw.set_label(label, label_len);

        if (!ring_buffer_.write(command::ADD_STATIC_COUNTER, scratch_, msg_len))
            throw std::runtime_error("failed to write add static counter command");

        return correlation_id;
    }

    /// Get the next available session id from the media driver.
    ///
    /// @param stream_id to get the next session id for.
    /// @return the correlation id for the command.
    i64 next_available_session_id(i32 stream_id)
    {
        const i64 correlation_id = next_correlation_id();

        concurrent::UnsafeBuffer scratch{scratch_, command::GET_NEXT_SESSION_ID_MSG_LENGTH};
        command::GetNextAvailableSessionIdMessageFlyweight fw{scratch, 0};
        fw.set_client_id(static_cast<i64>(client_id_));
        fw.set_correlation_id(correlation_id);
        fw.set_stream_id(stream_id);

        if (!ring_buffer_.write(command::GET_NEXT_AVAILABLE_SESSION_ID, scratch_, command::GET_NEXT_SESSION_ID_MSG_LENGTH))
            throw std::runtime_error("failed to write next session id command");

        return correlation_id;
    }

    /// Send a client keepalive to the driver.
    void keepalive()
    {
        concurrent::UnsafeBuffer scratch{scratch_, command::CORRELATED_MSG_LENGTH};
        command::CorrelatedMessageFlyweight fw{scratch, 0};
        fw.set_client_id(client_id_);
        fw.set_correlation_id(NULL_VALUE);

        if (!ring_buffer_.write(command::CLIENT_KEEPALIVE, scratch_, command::CORRELATED_MSG_LENGTH))
            throw std::runtime_error("failed to write client keepalive command");
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

    i64 next_correlation_id() noexcept
    {
        return correlation_counter_.fetch_add(1, std::memory_order_relaxed);
    }

    void validate_scratch_length(i32 length) const
    {
        if (length < 0 || length > SCRATCH_BUFFER_SIZE)
            throw std::runtime_error("command exceeds scratch buffer size (" +
                                     std::to_string(SCRATCH_BUFFER_SIZE) + " bytes)");
    }

    concurrent::ManyToOneRingBuffer& ring_buffer_;
    i64 client_id_;
    std::atomic<i64> correlation_counter_{1};

    /// Scratch buffer for serializing commands before writing to the ring buffer.
    /// Size must be large enough for the largest possible command. If commands
    /// with variable-length fields (channel, label, token, etc.) exceed this
    /// limit, a runtime_error is thrown.
    static constexpr i32 SCRATCH_BUFFER_SIZE = 2048;
    std::byte scratch_[SCRATCH_BUFFER_SIZE]{};
};

} // namespace caeron::cnc
