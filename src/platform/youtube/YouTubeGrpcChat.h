#pragma once

// This file is only compiled when IS_USE_YOUTUBE_GRPC is ON in CMake.
// The preprocessor guard allows IntelliSense / includes to be harmless
// even when the flag is absent.
#ifdef IS_YOUTUBE_GRPC_ENABLED

#include "platform/ChatMessage.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

namespace is::platform {

/// Persistent gRPC streaming client for the YouTube Live Chat
/// `liveChatMessages.streamList` API.
///
/// Maintains a long-lived server-streamed RPC to
/// `youtube.googleapis.com:443` and pushes every received chat message
/// into an internal thread-safe queue that the owning `YoutubePlatform`
/// drains via `drainMessages()`.
///
/// Reconnection is handled automatically – when the stream ends (e.g.
/// network glitch, `offlineAt` field set) the client waits briefly and
/// re-opens the RPC with the last `nextPageToken`.
class YouTubeGrpcChat {
public:
    /// Callback invoked for every parsed ChatMessage (from the gRPC thread).
    /// The owner should push the message into its own queue.
    using MessageCallback = std::function<void(ChatMessage)>;

    YouTubeGrpcChat();
    ~YouTubeGrpcChat();

    // Non-copyable, non-movable (owns a thread)
    YouTubeGrpcChat(const YouTubeGrpcChat&) = delete;
    YouTubeGrpcChat& operator=(const YouTubeGrpcChat&) = delete;

    /// Start the streaming connection.
    ///
    /// @param apiKey      YouTube Data API v3 key (used if oauthToken is empty)
    /// @param oauthToken  OAuth 2.0 Bearer token (preferred auth method)
    /// @param liveChatId  The `activeLiveChatId` from the live broadcast
    /// @param channelId   Channel identifier (used for the ChatMessage metadata)
    /// @param onMessage   Callback invoked for each received message
    void start(const std::string& apiKey,
               const std::string& oauthToken,
               const std::string& liveChatId,
               const std::string& channelId,
               MessageCallback onMessage);

    /// Gracefully stop the streaming connection.
    void stop();

    /// Returns true while the stream loop is running (even during reconnect).
    bool isRunning() const { return m_running.load(); }

    /// Returns true if the gRPC stream is currently connected and receiving.
    bool isConnected() const { return m_connected.load(); }

    /// Total messages received since start().
    size_t messagesReceived() const { return m_messagesReceived.load(); }

private:
    /// Background thread entry point.
    void streamLoop();

    // Configuration (set once in start(), read-only from thread)
    std::string     m_apiKey;
    std::string     m_oauthToken;
    std::string     m_liveChatId;
    std::string     m_channelId;
    MessageCallback m_onMessage;

    // State
    std::atomic<bool>   m_running{false};
    std::atomic<bool>   m_connected{false};
    std::atomic<size_t> m_messagesReceived{0};
    std::thread         m_thread;

    // Pagination – carried across reconnects
    std::string m_nextPageToken;
};

} // namespace is::platform

#endif // IS_YOUTUBE_GRPC_ENABLED
