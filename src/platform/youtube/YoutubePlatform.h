#pragma once

#include "platform/IPlatform.h"

#ifdef IS_YOUTUBE_GRPC_ENABLED
#include "platform/youtube/YouTubeGrpcChat.h"
#endif

#include <queue>
#include <mutex>
#include <atomic>
#include <thread>

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

private:
    void pollLoop();

    /// Auto-detect the activeLiveChatId for the current live stream.
    /// Uses search.list (eventType=live) + videos.list (liveStreamingDetails).
    /// Returns the chat ID or an empty string on failure.
    std::string fetchLiveChatId();

#ifdef IS_YOUTUBE_GRPC_ENABLED
    /// Run the gRPC streaming loop instead of REST polling.
    /// Called from the background thread when gRPC is available.
    /// Returns true if gRPC was used (even if it eventually disconnected).
    bool tryGrpcStream();
#endif

    // Configuration
    std::string m_apiKey;
    std::string m_oauthToken;   ///< OAuth 2.0 Bearer token (for write operations like liveBroadcasts.update)
    std::string m_liveChatId;
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

    // Stats
    size_t m_messagesReceived = 0;
    size_t m_messagesSent     = 0;

#ifdef IS_YOUTUBE_GRPC_ENABLED
    /// The gRPC streaming client (created dynamically when gRPC mode is active).
    std::unique_ptr<YouTubeGrpcChat> m_grpcChat;
    bool m_grpcActive = false; ///< true while gRPC streaming is in use
#endif
};

} // namespace is::platform
