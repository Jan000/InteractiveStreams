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

    /// Find the activeLiveChatId using liveBroadcasts.list (requires OAuth token).
    /// Calls liveBroadcasts.list?mine=true&broadcastStatus=active&part=snippet
    /// and extracts snippet.liveChatId from the first active broadcast.
    /// Returns the liveChatId or empty string on failure.
    static std::string findLiveChatId(const std::string& oauthToken);

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

    // ── OAuth 2.0 helpers ────────────────────────────────────────────────

    /// Result of a token exchange or refresh.
    struct TokenResult {
        bool        success      = false;
        std::string accessToken;
        std::string refreshToken; ///< Only set on initial exchange (empty on refresh)
        int         expiresIn    = 0;      ///< Seconds until access_token expires
        std::string error;
    };

    /// Exchange an authorization code for an access + refresh token.
    /// This implements the "Authorization Code" grant of Google's OAuth 2.0 flow.
    static TokenResult exchangeAuthCode(const std::string& code,
                                        const std::string& clientId,
                                        const std::string& clientSecret,
                                        const std::string& redirectUri);

    /// Refresh an expired access token using a refresh token.
    static TokenResult refreshAccessToken(const std::string& refreshToken,
                                          const std::string& clientId,
                                          const std::string& clientSecret);

    /// URL-encode a string (percent-encoding for unsafe characters).
    static std::string urlEncode(const std::string& s);

private:
    /// Run curl and capture stdout.  Returns empty string on failure.
    static std::string curlRequest(const std::string& method,
                                   const std::string& url,
                                   const std::string& oauthToken,
                                   const std::string& body = "");

    /// Run a curl POST with application/x-www-form-urlencoded body (no Bearer auth).
    static std::string curlPostForm(const std::string& url,
                                    const std::string& formBody);
};

} // namespace is::platform
