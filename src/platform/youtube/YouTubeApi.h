#pragma once

#include <string>

namespace is::platform {

/// YouTube Data API v3 helper — uses curl.exe to make HTTPS calls,
/// mirroring the pattern established by TwitchApi.
///
/// All methods are static and blocking; call them from a background thread
/// to avoid stalling the main loop.
class YouTubeApi {
public:
    /// Validate that curl is available on the system.
    static bool isCurlAvailable();

    /// Find the active live broadcast ID for the authenticated user.
    /// Calls liveBroadcasts.list with broadcastStatus=active.
    /// Returns empty string on failure or if no active broadcast exists.
    static std::string getActiveBroadcastId(const std::string& oauthToken);

    /// Update the title, description, and/or category of a live broadcast.
    /// Requires a valid broadcast ID (from getActiveBroadcastId).
    /// Pass empty strings to leave fields unchanged (they will be fetched first).
    /// categoryId is the numeric YouTube category (e.g. "20" = Gaming).
    /// Returns true on success.
    static bool updateBroadcast(const std::string& oauthToken,
                                const std::string& broadcastId,
                                const std::string& title,
                                const std::string& description = "",
                                const std::string& categoryId = "");

private:
    /// Run curl and capture stdout.  Returns empty string on failure.
    static std::string curlRequest(const std::string& method,
                                   const std::string& url,
                                   const std::string& oauthToken,
                                   const std::string& body = "");
};

} // namespace is::platform
