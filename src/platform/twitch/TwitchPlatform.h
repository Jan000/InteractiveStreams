#pragma once

#include "platform/IPlatform.h"
#include <queue>
#include <mutex>
#include <atomic>
#include <thread>

namespace is::platform {

/// Twitch platform integration via IRC (TMI).
/// Connects to Twitch chat and reads/sends messages.
class TwitchPlatform : public IPlatform {
public:
    TwitchPlatform();
    ~TwitchPlatform() override;

    std::string id() const override { return "twitch"; }
    std::string displayName() const override { return "Twitch"; }

    bool connect() override;
    void disconnect() override;
    bool isConnected() const override;
    std::vector<ChatMessage> pollMessages() override;
    bool sendMessage(const std::string& text) override;
    nlohmann::json getStatus() const override;
    void configure(const nlohmann::json& settings) override;

private:
    void ircLoop();
    void parseIrcMessage(const std::string& raw);

    // Configuration
    std::string m_oauthToken;
    std::string m_botUsername;
    std::string m_channel;
    std::string m_server = "irc.chat.twitch.tv";
    int         m_port   = 6667;

    // State
    std::atomic<bool> m_connected{false};
    std::atomic<bool> m_shouldRun{false};
    std::thread       m_thread;

    // Thread-safe message queue
    std::queue<ChatMessage> m_messageQueue;
    mutable std::mutex      m_mutex;

    // Stats
    size_t m_messagesReceived = 0;
    size_t m_messagesSent     = 0;
};

} // namespace is::platform
