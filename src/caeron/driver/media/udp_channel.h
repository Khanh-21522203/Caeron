#pragma once

#include "caeron/common/types.h"
#include "caeron/driver/media/channel_uri.h"
#include "caeron/driver/media/control_mode.h"
#include "caeron/driver/media/interface_search_address.h"
#include "caeron/driver/media/named_interface.h"
#include "caeron/driver/media/network_util.h"
#include "caeron/driver/media/resolved_interface.h"
#include "caeron/driver/media/socket_address_parser.h"
#include "caeron/driver/media/unresolved_interface.h"

#include <cstring>
#include <functional>
#include <limits>
#include <netinet/in.h>
#include <optional>
#include <string>
#include <sys/socket.h>

namespace caeron::driver::media {

/// Parsed representation of an "aeron:udp?..." channel URI with resolved socket addresses.
///
/// This is the central media-layer type. A UdpChannel holds all resolved addresses
/// (local data, remote data, local control, remote control) and configuration
/// parameters extracted from the channel URI string.
class UdpChannel
{
public:
    static constexpr i32 RESERVED_VALUE_MESSAGE_OFFSET = -8;

    /// Parse a channel URI string. Throws on invalid configuration.
    [[nodiscard]] static UdpChannel parse(std::string_view channel_uri_string)
    {
        UdpChannel result;
        result.uri_str_ = std::string(channel_uri_string);

        auto uri = ChannelUri::parse(channel_uri_string);

        // Validate transport is UDP (not IPC or other)
        if (uri.prefix() != "aeron:udp")
            throw std::invalid_argument("UdpChannel requires 'aeron:udp' prefix, got: " + uri.prefix());

        // Extract parameters
        auto endpoint_str = uri.get("endpoint");
        auto control_str = uri.get("control");
        auto interface_str = uri.get("interface");
        auto multicast_ttl_str = uri.get("multicast-ttl");
        auto group_tag_str = uri.get("group-tag");
        auto nak_delay_str = uri.get("nak-delay");
        auto rcv_wnd_str = uri.get("rcv-wnd");
        auto so_rcvbuf_str = uri.get("so-rcvbuf");
        auto so_sndbuf_str = uri.get("so-sndbuf");
        auto control_mode_str = uri.get("control-mode");
        auto tag_str = uri.get("tag");

        // Parse control mode
        result.control_mode_ = control_mode_str
            ? parse_control_mode(*control_mode_str)
            : ControlMode::NONE;

        // Validate required params for the channel type
        if (!endpoint_str && !control_str && !tag_str &&
            result.control_mode_ != ControlMode::MANUAL &&
            result.control_mode_ != ControlMode::RESPONSE)
        {
            throw std::invalid_argument(
                "URIs for UDP must specify an endpoint, control, tags, or control-mode=manual/response: " +
                std::string(channel_uri_string));
        }

        if (result.control_mode_ == ControlMode::DYNAMIC && !control_str)
        {
            throw std::invalid_argument(
                "explicit control expected with dynamic control mode: " +
                std::string(channel_uri_string));
        }

        // Parse multicast TTL
        if (multicast_ttl_str)
        {
            try { result.multicast_ttl_ = std::stoi(*multicast_ttl_str); }
            catch (const std::exception& e) {
                throw std::invalid_argument("invalid multicast-ttl value: " + *multicast_ttl_str + " (" + e.what() + ")");
            }
            if (result.multicast_ttl_ < 0)
                throw std::invalid_argument("multicast-ttl must be non-negative: " + *multicast_ttl_str);
            result.has_multicast_ttl_ = true;
        }

        // Parse group tag
        if (group_tag_str)
        {
            try { result.group_tag_ = std::stoll(*group_tag_str); }
            catch (const std::exception& e) {
                throw std::invalid_argument("invalid group-tag value: " + *group_tag_str + " (" + e.what() + ")");
            }
        }

        // Parse nak delay
        if (nak_delay_str)
        {
            try { result.nak_delay_ns_ = std::stoll(*nak_delay_str); }
            catch (const std::exception& e) {
                throw std::invalid_argument("invalid nak-delay value: " + *nak_delay_str + " (" + e.what() + ")");
            }
        }

        // Parse buffer sizes (handle "k" and "m" suffixes)
        if (rcv_wnd_str)
        {
            result.receiver_window_length_ = parse_size_value(*rcv_wnd_str);
            if (result.receiver_window_length_ < 0)
                throw std::invalid_argument("rcv-wnd must be non-negative: " + *rcv_wnd_str);
        }
        if (so_rcvbuf_str)
        {
            result.socket_rcvbuf_length_ = parse_size_value(*so_rcvbuf_str);
            if (result.socket_rcvbuf_length_ < 0)
                throw std::invalid_argument("so-rcvbuf must be non-negative: " + *so_rcvbuf_str);
        }
        if (so_sndbuf_str)
        {
            result.socket_sndbuf_length_ = parse_size_value(*so_sndbuf_str);
            if (result.socket_sndbuf_length_ < 0)
                throw std::invalid_argument("so-sndbuf must be non-negative: " + *so_sndbuf_str);
        }

        // Parse tag
        if (tag_str)
        {
            try { result.tag_ = std::stoll(*tag_str); }
            catch (const std::exception& e) {
                throw std::invalid_argument("invalid tag value: " + *tag_str + " (" + e.what() + ")");
            }
            result.has_tag_ = true;
        }

        // Parse timestamp offsets
        auto rcv_ts_offset = uri.get("channel-rcv-ts-offset");
        if (rcv_ts_offset)
        {
            try { result.channel_receive_timestamp_offset_ = std::stoi(*rcv_ts_offset); }
            catch (const std::exception& e) {
                throw std::invalid_argument("invalid channel-rcv-ts-offset value: " + *rcv_ts_offset + " (" + e.what() + ")");
            }
        }
        auto snd_ts_offset = uri.get("channel-snd-ts-offset");
        if (snd_ts_offset)
        {
            try { result.channel_send_timestamp_offset_ = std::stoi(*snd_ts_offset); }
            catch (const std::exception& e) {
                throw std::invalid_argument("invalid channel-snd-ts-offset value: " + *snd_ts_offset + " (" + e.what() + ")");
            }
        }

        // Resolve endpoint address
        if (endpoint_str)
        {
            result.has_explicit_endpoint_ = true;
            auto parsed_ep = socket_address_parser::parse(*endpoint_str);
            result.remote_data_ = socket_address_parser::resolve_host(parsed_ep.host, parsed_ep.port);
            result.protocol_family_ = static_cast<int>(result.remote_data_.ss_family);

            // Check if multicast
            result.is_multicast_ = is_multicast_address(result.remote_data_);

            if (result.is_multicast_)
            {
                // Multicast: control address is data address + 1 in last octet
                result.remote_control_ = get_multicast_control_address(result.remote_data_);
            }
            else
            {
                // Unicast: control address defaults to data address
                result.remote_control_ = result.remote_data_;
            }
        }
        else if (!caeron::driver::media::is_multi_destination(result.control_mode_))
        {
            // For unicast without explicit endpoint, we need an endpoint
            // (unless it's MDC which uses control, or has a tag)
            if (!tag_str)
            {
                throw std::invalid_argument(
                    "unicast channel requires endpoint: " + std::string(channel_uri_string));
            }
        }

        // Resolve control address (for MDC)
        if (control_str)
        {
            result.has_explicit_control_ = true;
            auto parsed_ctrl = socket_address_parser::parse(*control_str);
            result.remote_control_ = socket_address_parser::resolve_host(
                parsed_ctrl.host, parsed_ctrl.port);

            // MDC requires both endpoint and control
            if (caeron::driver::media::is_multi_destination(result.control_mode_) && !endpoint_str)
            {
                throw std::invalid_argument(
                    "MDC channel requires both endpoint and control: " +
                    std::string(channel_uri_string));
            }
        }

        // Resolve local interface
        if (interface_str)
        {
            auto unresolved = parse_interface(*interface_str);
            bool multicast = result.is_multicast_;
            result.local_interface_ = resolve_interface(unresolved, multicast, result.protocol_family_);
            result.local_data_ = result.local_interface_.address;
        }
        else
        {
            // Default: bind to any address
            std::memset(&result.local_data_, 0, sizeof(result.local_data_));
            result.local_data_.ss_family = static_cast<sa_family_t>(result.protocol_family_);
        }

        // For multicast, set local data port to same as remote data port
        if (result.is_multicast_ && endpoint_str)
        {
            socket_address_parser::set_port(
                result.local_data_,
                socket_address_parser::get_port(result.remote_data_));
        }

        // Local control = same as local data by default
        result.local_control_ = result.local_data_;

        // Generate canonical form
        result.canonical_form_ = generate_canonical_form(result);

        return result;
    }

    // Accessors
    [[nodiscard]] const struct sockaddr_storage& remote_data() const noexcept { return remote_data_; }
    [[nodiscard]] const struct sockaddr_storage& local_data() const noexcept { return local_data_; }
    [[nodiscard]] const struct sockaddr_storage& remote_control() const noexcept { return remote_control_; }
    [[nodiscard]] const struct sockaddr_storage& local_control() const noexcept { return local_control_; }
    [[nodiscard]] const std::string& canonical_form() const noexcept { return canonical_form_; }
    [[nodiscard]] const std::string& original_uri_string() const noexcept { return uri_str_; }
    [[nodiscard]] bool is_multicast() const noexcept { return is_multicast_; }
    [[nodiscard]] bool has_explicit_endpoint() const noexcept { return has_explicit_endpoint_; }
    [[nodiscard]] bool has_explicit_control() const noexcept { return has_explicit_control_; }
    [[nodiscard]] bool has_tag() const noexcept { return has_tag_; }
    [[nodiscard]] i64 tag() const noexcept { return tag_; }
    [[nodiscard]] ControlMode control_mode() const noexcept { return control_mode_; }
    [[nodiscard]] bool is_manual_control_mode() const noexcept { return control_mode_ == ControlMode::MANUAL; }
    [[nodiscard]] bool is_dynamic_control_mode() const noexcept { return control_mode_ == ControlMode::DYNAMIC; }
    [[nodiscard]] bool is_response_control_mode() const noexcept { return control_mode_ == ControlMode::RESPONSE; }
    [[nodiscard]] bool is_multi_destination() const noexcept { return caeron::driver::media::is_multi_destination(control_mode_); }
    [[nodiscard]] bool has_group_semantics() const noexcept { return group_tag_.has_value() || has_tag_; }
    [[nodiscard]] bool has_multicast_ttl() const noexcept { return has_multicast_ttl_; }
    [[nodiscard]] int multicast_ttl() const noexcept { return multicast_ttl_; }
    [[nodiscard]] int socket_rcvbuf_length() const noexcept { return socket_rcvbuf_length_; }
    [[nodiscard]] int socket_rcvbuf_length_or_default(int default_val) const noexcept
    {
        return socket_rcvbuf_length_ > 0 ? socket_rcvbuf_length_ : default_val;
    }
    [[nodiscard]] int socket_sndbuf_length() const noexcept { return socket_sndbuf_length_; }
    [[nodiscard]] int socket_sndbuf_length_or_default(int default_val) const noexcept
    {
        return socket_sndbuf_length_ > 0 ? socket_sndbuf_length_ : default_val;
    }
    [[nodiscard]] int receiver_window_length() const noexcept { return receiver_window_length_; }
    [[nodiscard]] int protocol_family() const noexcept { return protocol_family_; }
    [[nodiscard]] const ResolvedInterface& local_interface() const noexcept { return local_interface_; }
    [[nodiscard]] std::optional<i64> group_tag() const noexcept { return group_tag_; }
    [[nodiscard]] std::optional<i64> nak_delay_ns() const noexcept { return nak_delay_ns_; }
    [[nodiscard]] int channel_receive_timestamp_offset() const noexcept { return channel_receive_timestamp_offset_; }
    [[nodiscard]] int channel_send_timestamp_offset() const noexcept { return channel_send_timestamp_offset_; }
    [[nodiscard]] bool is_channel_receive_timestamp_enabled() const noexcept { return channel_receive_timestamp_offset_ >= 0; }
    [[nodiscard]] bool is_channel_send_timestamp_enabled() const noexcept { return channel_send_timestamp_offset_ >= 0; }

    /// Tag matching for publication/subscription correlation.
    [[nodiscard]] bool matches_tag(
        const UdpChannel& other,
        const struct sockaddr_storage* local_address_override,
        const struct sockaddr_storage* remote_address_override) const
    {
        if (!has_tag_ || !other.has_tag_)
            return false;
        if (tag_ != other.tag_)
            return false;

        // Check local address match
        const auto& local_a = local_address_override ? *local_address_override : local_data_;
        const auto& local_b = other.local_data_;
        if (!socket_address_parser::addresses_equal(local_a, local_b))
            return false;

        // Check remote address match
        const auto& remote_a = remote_address_override ? *remote_address_override : remote_data_;
        const auto& remote_b = other.remote_data_;
        if (!socket_address_parser::addresses_equal(remote_a, remote_b))
            return false;

        return true;
    }

    /// Determine multicast control address (data address + 1 in last octet).
    /// Throws if the last octet is 255, as there is no valid control address.
    [[nodiscard]] static struct sockaddr_storage get_multicast_control_address(
        const struct sockaddr_storage& endpoint_address)
    {
        auto result = endpoint_address;

        if (result.ss_family == AF_INET)
        {
            auto* addr = reinterpret_cast<struct sockaddr_in*>(&result);
            auto& last = reinterpret_cast<u8*>(&addr->sin_addr.s_addr)[3];
            if (last >= 255)
                throw std::invalid_argument(
                    "multicast data address last octet is 255, no valid control address");
            ++last;
        }
        else if (result.ss_family == AF_INET6)
        {
            auto* addr = reinterpret_cast<struct sockaddr_in6*>(&result);
            auto& last = addr->sin6_addr.s6_addr[15];
            if (last >= 255)
                throw std::invalid_argument(
                    "multicast data address last octet is 255, no valid control address");
            ++last;
        }

        return result;
    }

    [[nodiscard]] std::string description() const
    {
        return "UdpChannel{uri=" + uri_str_ +
               ", multicast=" + (is_multicast_ ? "true" : "false") +
               ", canonical=" + canonical_form_ + "}";
    }

    bool operator==(const UdpChannel& other) const noexcept
    {
        return canonical_form_ == other.canonical_form_;
    }

private:
    UdpChannel() = default;

    /// Parse a size value string that may have 'k' or 'm' suffix.
    [[nodiscard]] static int parse_size_value(std::string_view str)
    {
        if (str.empty())
            return 0;

        i64 multiplier = 1;
        auto num_str = str;

        if (str.back() == 'k' || str.back() == 'K')
        {
            multiplier = 1024;
            num_str = str.substr(0, str.size() - 1);
        }
        else if (str.back() == 'm' || str.back() == 'M')
        {
            multiplier = 1024 * 1024;
            num_str = str.substr(0, str.size() - 1);
        }

        auto base = std::stoll(std::string(num_str));

        // Reject negative values early to prevent signed overflow UB
        if (base < 0)
            throw std::invalid_argument("size value must be non-negative: " + std::string(str));

        // Check for overflow before multiplication
        if (multiplier > 1)
        {
            constexpr i64 i64_max = std::numeric_limits<i64>::max();
            if (base > i64_max / multiplier)
                return std::numeric_limits<int>::max();
        }

        auto val = base * multiplier;
        if (val > std::numeric_limits<int>::max())
            return std::numeric_limits<int>::max();
        return static_cast<int>(val);
    }

    [[nodiscard]] static bool is_multicast_address(const struct sockaddr_storage& addr)
    {
        if (addr.ss_family == AF_INET)
        {
            const auto* a4 = reinterpret_cast<const struct sockaddr_in*>(&addr);
            const auto first_octet = reinterpret_cast<const u8*>(&a4->sin_addr.s_addr)[0];
            return (first_octet & 0xF0) == 0xE0;
        }
        if (addr.ss_family == AF_INET6)
        {
            const auto* a6 = reinterpret_cast<const struct sockaddr_in6*>(&addr);
            return a6->sin6_addr.s6_addr[0] == 0xFF;
        }
        return false;
    }

    /// Generate deterministic canonical form matching Java Aeron's UdpChannel.canonicalise().
    /// Format: "UDP-localAddr:localPort-remoteAddr:remotePort[-controlAddr:controlPort]"
    /// No counter -- same URI parameters always produce the same canonical form.
    [[nodiscard]] static std::string generate_canonical_form(const UdpChannel& ch)
    {
        std::string result;
        result += "UDP-";
        result += socket_address_parser::format_address_and_port(ch.local_data_);
        result += "-";
        result += socket_address_parser::format_address_and_port(ch.remote_data_);
        if (ch.has_explicit_control_)
        {
            result += "-";
            result += socket_address_parser::format_address_and_port(ch.remote_control_);
        }
        return result;
    }

    struct sockaddr_storage remote_data_{};
    struct sockaddr_storage local_data_{};
    struct sockaddr_storage remote_control_{};
    struct sockaddr_storage local_control_{};
    ResolvedInterface local_interface_{};
    std::string uri_str_;
    std::string canonical_form_;
    ControlMode control_mode_ = ControlMode::NONE;
    int protocol_family_ = AF_INET;
    int multicast_ttl_ = 0;
    int socket_rcvbuf_length_ = 0;
    int socket_sndbuf_length_ = 0;
    int receiver_window_length_ = 0;
    int channel_receive_timestamp_offset_ = -1;
    int channel_send_timestamp_offset_ = -1;
    i64 tag_ = 0;
    bool has_explicit_endpoint_ = false;
    bool has_explicit_control_ = false;
    bool is_multicast_ = false;
    bool has_multicast_ttl_ = false;
    bool has_tag_ = false;
    std::optional<i64> group_tag_;
    std::optional<i64> nak_delay_ns_;
};

/// std::hash specialization for UdpChannel
struct UdpChannelHash
{
    [[nodiscard]] size_t operator()(const UdpChannel& ch) const noexcept
    {
        return std::hash<std::string>{}(ch.canonical_form());
    }
};

} // namespace caeron::driver::media
