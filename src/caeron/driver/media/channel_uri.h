#pragma once

#include "caeron/common/types.h"

#include <optional>
#include <string>
#include <string_view>
#include <stdexcept>
#include <unordered_map>

namespace caeron::driver::media {

/// Minimal channel URI parser for "aeron:udp?param=value|param=value" format.
///
/// This is a simplified version that parses the key-value parameter list from
/// an Aeron channel URI string. The full ChannelUri from the client library
/// will replace this when it is ported.
class ChannelUri
{
public:
    /// Parse a channel URI string.
    /// Format: "aeron:transport?param=value|param=value"
    /// The prefix (e.g. "aeron:udp") and "?" are stripped; the rest is parsed
    /// as pipe-delimited key=value pairs.
    [[nodiscard]] static ChannelUri parse(std::string_view uri)
    {
        ChannelUri result;
        result.original_uri_ = std::string(uri);

        // Validate "aeron:" prefix
        if (uri.size() < 6 || uri.substr(0, 6) != "aeron:")
            throw std::invalid_argument("URI must start with 'aeron:': " + std::string(uri));

        // Find the '?' separator
        auto qmark = uri.find('?');
        if (qmark == std::string_view::npos)
        {
            result.prefix_ = std::string(uri);
            return result;
        }

        result.prefix_ = std::string(uri.substr(0, qmark));
        auto params = uri.substr(qmark + 1);

        // Parse pipe-delimited key=value pairs
        while (!params.empty())
        {
            auto pipe_pos = params.find('|');
            auto kv = (pipe_pos != std::string_view::npos)
                          ? params.substr(0, pipe_pos)
                          : params;

            if (!kv.empty())
            {
                auto eq_pos = kv.find('=');
                if (eq_pos == std::string_view::npos)
                    throw std::invalid_argument("pipe segment missing '=' separator: " + std::string(kv));
                auto key = std::string(kv.substr(0, eq_pos));
                auto val = std::string(kv.substr(eq_pos + 1));
                result.params_[std::move(key)] = std::move(val);
            }

            if (pipe_pos == std::string_view::npos)
                break;
            params = params.substr(pipe_pos + 1);
        }

        return result;
    }

    [[nodiscard]] const std::string& prefix() const noexcept { return prefix_; }
    [[nodiscard]] const std::string& original_uri() const noexcept { return original_uri_; }

    [[nodiscard]] bool has(std::string_view key) const
    {
        return params_.count(std::string(key)) > 0;
    }

    [[nodiscard]] std::optional<std::string> get(std::string_view key) const
    {
        auto it = params_.find(std::string(key));
        if (it != params_.end())
            return it->second;
        return std::nullopt;
    }

    /// Get a parameter value, returning default_val if not present.
    [[nodiscard]] std::string get(std::string_view key, std::string_view default_val) const
    {
        auto val = get(key);
        return val ? *val : std::string(default_val);
    }

    /// Get a parameter as an integer, returning default_val if not present.
    /// Throws std::out_of_range if the value is too large for int.
    [[nodiscard]] int get_int(std::string_view key, int default_val) const
    {
        auto val = get(key);
        if (!val)
            return default_val;
        try
        {
            return std::stoi(*val);
        }
        catch (const std::out_of_range&)
        {
            throw;  // Re-throw: out-of-range is a configuration error
        }
        catch (...)
        {
            return default_val;  // Non-numeric: use default
        }
    }

    /// Get a parameter as an i64, returning default_val if not present.
    /// Throws std::out_of_range if the value is too large for i64.
    [[nodiscard]] i64 get_i64(std::string_view key, i64 default_val) const
    {
        auto val = get(key);
        if (!val)
            return default_val;
        try
        {
            return std::stoll(*val);
        }
        catch (const std::out_of_range&)
        {
            throw;  // Re-throw: out-of-range is a configuration error
        }
        catch (...)
        {
            return default_val;  // Non-numeric: use default
        }
    }

private:
    std::string prefix_;
    std::string original_uri_;
    std::unordered_map<std::string, std::string> params_;
};

} // namespace caeron::driver::media
