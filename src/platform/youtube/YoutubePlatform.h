#pragma once

#include "platform/IPlatform.h"

#ifdef IS_YOUTUBE_GRPC_ENABLED
#include "platform/youtube/YouTubeGrpcChat.h"
#endif

#include <queue>
#include <mutex>
#include <atomic>
#include <thread>
#include <functional>

namespace is::platform {

/// YouTube Live Chat platform integration.
/// Uses the YouTube Live Streaming API to read/send chat messages.
class YoutubePlatform : public IPlatform {
public:
    YoutubePlatform();
    ~YoutubePlatform() override;

    std::string id() const override { return "youtube"; }
    std::string displayName() const override { return "YouTube"; }

    bool connect() override;
    void disconnect() override;
    bool isConnected() const override;
    std::vector<ChatMessage> pollMessages() override;
    bool sendMessage(const std::string& text) override;
    nlohmann::json getStatus() const override;
    void configure(const nlohmann::json& settings) override;
    nlohmann::json getCurrentSettings() const override;

    /// Set a callback that returns true when at least one stream is actively
    /// encoding.  Auto-detection of the liveChatId will be deferred until the
    /// checker returns true (+ a short stabilisation delay).
    void setStreamingChecker(std::function<bool()> checker);

private:
    void pollLoop();

    /// Auto-detect the activeLiveChatId for the current live broadcast.
    /// Tries OAuth (liveBroadcasts.list mine=true) first, then falls back
    /// to API key + channel ID (search + videos.list).
    /// Returns the chat ID or an empty string on failure.
    /// Also caches the broadcast ID for reuse by StreamInstance.
    std::string fetchLiveChatId();

    /// Human-readable status of the last detection attempt (for dashboard).
    std::string m_detectionStatus;

    /// Refresh the OAuth access token if it's expired or about to expire.
    void refreshTokenIfNeeded();

#ifdef IS_YOUTUBE_GRPC_ENABLED
    /// Run the gRPC streaming loop instead of REST polling.
    /// Called from the background thread when gRPC is available.
    /// Returns true if gRPC was used (even if it eventually disconnected).
    bool tryGrpcStream();
#endif

    // Configuration
    std::string m_apiKey;
    std::string m_oauthToken;          ///< OAuth 2.0 Bearer access token
    std::string m_oauthClientId;       ///< Google OAuth Client ID (for token refresh)
    std::string m_oauthClientSecret;   ///< Google OAuth Client Secret
    std::string m_oauthRefreshToken;   ///< Refresh token for auto-renewal
    int64_t     m_oauthTokenExpiry = 0;///< Unix timestamp when access token expires
    std::string m_liveChatId;
    std::string m_broadcastId;         ///< Cached broadcast ID from findActiveBroadcast
    std::string m_channelId;
    int         m_pollIntervalMs = 2000;
    bool        m_autoDetectedChatId = false; ///< true if liveChatId was resolved automatically

    // State
    std::atomic<bool> m_connected{false};
    std::atomic<bool> m_shouldRun{false};
    std::thread       m_thread;
    std::string       m_nextPageToken;

    // Thread-safe message queue
    std::queue<ChatMessage> m_messageQueue;
    mutable std::mutex      m_mutex;

    // Streaming gate – defer auto-detection until a stream is live
    std::function<bool()> m_streamingChecker;

    /// Block (in 500 ms increments) until m_streamingChecker returns true,
    /// then wait an additional stabilisation delay.  Returns false if
    /// m_shouldRun became false while waiting.
    bool waitForStreaming();

    // Stats
    size_t m_messagesReceived = 0;
    size_t m_messagesSent     = 0;
    std::atomic<int64_t> m_lastMessageTime{0}; ///< epoch seconds of last received message

#ifdef IS_YOUTUBE_GRPC_ENABLED
    /// The gRPC streaming client (created dynamically when gRPC mode is active).
    std::unique_ptr<YouTubeGrpcChat> m_grpcChat;
    bool m_grpcActive = false; ///< true while gRPC streaming is in use
#endif
};

} // namespace is::platform
