#pragma once

#include "core/Config.h"
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace is::streaming {

/// Represents a configured stream output target (RTMP endpoint).
struct StreamTarget {
    std::string name;        ///< Display name (e.g., "Twitch Primary")
    std::string platform;    ///< Platform type (twitch, youtube, custom)
    std::string url;         ///< RTMP URL
    std::string streamKey;   ///< Stream key (appended to URL)
    bool        enabled = true;

    std::string getFullUrl() const {
        if (streamKey.empty()) return url;
        return url + "/" + streamKey;
    }
};

/// Manages multiple stream output targets.
class StreamOutput {
public:
    explicit StreamOutput(core::Config& config);
    ~StreamOutput();

    /// Get all configured targets.
    const std::vector<StreamTarget>& targets() const { return m_targets; }

    /// Get the primary (first enabled) target URL.
    std::string getPrimaryUrl() const;

    /// Add a new target.
    void addTarget(const StreamTarget& target);

    /// Remove a target by name.
    void removeTarget(const std::string& name);

    /// Enable/disable a target.
    void setTargetEnabled(const std::string& name, bool enabled);

    /// Get status as JSON.
    nlohmann::json getStatus() const;

private:
    void loadTargets();

    core::Config& m_config;
    std::vector<StreamTarget> m_targets;
};

} // namespace is::streaming
