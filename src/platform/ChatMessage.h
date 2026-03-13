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
    bool        isModerator = false;
    bool        isSubscriber = false;
    double      timestamp = 0.0;

    /// Create a platform-unique user ID.
    static std::string makeUserId(const std::string& platform, const std::string& rawId) {
        return platform + ":" + rawId;
    }
};

} // namespace is::platform
