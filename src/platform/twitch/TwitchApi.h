#pragma once

#include <string>

namespace is::platform {

/// Twitch Helix API helper — uses curl.exe (shipped with Windows 10+) to
/// make HTTPS calls, so no OpenSSL dependency is needed.
///
/// All methods are static and blocking; call them from a background thread
/// or an async context to avoid stalling the main loop.
class TwitchApi {
public:
    /// Validate that curl is available on the system.
    static bool isCurlAvailable();

    /// Get the broadcaster (user) ID for the given OAuth token.
    /// Returns empty string on failure.
    static std::string getBroadcasterId(const std::string& token,
                                        const std::string& clientId);

    /// Look up a Twitch game/category ID by name (e.g. "Just Chatting").
    /// Returns empty string if not found.
    static std::string getGameId(const std::string& token,
                                 const std::string& clientId,
                                 const std::string& categoryName);

    /// Update the channel's title and/or game category.
    /// Pass empty strings to leave a field unchanged.
    /// Returns true on success.
    static bool updateChannelInfo(const std::string& token,
                                  const std::string& clientId,
                                  const std::string& broadcasterId,
                                  const std::string& title,
                                  const std::string& gameId);

private:
    /// Run curl and capture stdout.  Returns empty string on failure.
    static std::string curlRequest(const std::string& method,
                                   const std::string& url,
                                   const std::string& token,
                                   const std::string& clientId,
                                   const std::string& body = "");
};

} // namespace is::platform
