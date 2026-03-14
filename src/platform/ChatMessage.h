#pragma once

#include <string>

namespace is::platform {

/// Represents a chat message from a viewer on any platform.
struct ChatMessage {
    std::string platformId;    ///< Platform identifier (e.g., "twitch", "youtube")
    std::string channelId;     ///< Channel/room ID
    std::string userId;        ///< Unique user identifier (platform-prefixed)
    std::string displayName;   ///< User's display name
    std::string avatarUrl;     ///< Optional profile image URL
    std::string text;          ///< Message content
    std::string eventType;     ///< Stream event type (e.g. "yt_subscribe", "yt_superchat", "")
    int         amount = 0;    ///< Monetary amount in micros/cents (super chat, bits, etc.)
    std::string currency;      ///< Currency code for monetary events (e.g. "USD")
    bool        isModerator = false;
    bool        isSubscriber = false;
    double      timestamp = 0.0;

    /// Create a platform-unique user ID.
    static std::string makeUserId(const std::string& platform, const std::string& rawId) {
        return platform + ":" + rawId;
    }
};

} // namespace is::platform
