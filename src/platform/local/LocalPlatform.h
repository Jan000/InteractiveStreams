#pragma once

#include "platform/IPlatform.h"
#include "platform/ChatMessage.h"

#include <mutex>
#include <queue>
#include <string>
#include <atomic>
#include <thread>

namespace is::platform {

/// Local platform for testing – accepts chat messages via API and optional
/// console input. Always "connected" so messages can be injected at any time.
class LocalPlatform : public IPlatform {
public:
    LocalPlatform();
    ~LocalPlatform() override;

    std::string id() const override { return "local"; }
    std::string displayName() const override { return "Local (Test)"; }

    bool connect() override;
    void disconnect() override;
    bool isConnected() const override;

    std::vector<ChatMessage> pollMessages() override;
    bool sendMessage(const std::string& text) override;

    nlohmann::json getStatus() const override;
    void configure(const nlohmann::json& settings) override;

    // ──── Local-specific API ─────────────────────────────────────────────

    /// Inject a chat message (called from REST API or console).
    void injectMessage(const std::string& username, const std::string& text,
                       const std::string& avatarUrl = "");

    /// Get the outgoing message log (messages sent *to* local chat).
    std::vector<std::string> getMessageLog() const;

private:
    void consoleInputLoop();

    std::atomic<bool> m_connected{false};
    bool              m_consoleEnabled = true;

    mutable std::mutex            m_queueMutex;
    std::queue<ChatMessage>       m_messageQueue;

    mutable std::mutex            m_logMutex;
    std::vector<std::string>      m_messageLog;  ///< Outgoing messages (for display)

    std::unique_ptr<std::thread>  m_consoleThread;
    std::atomic<bool>             m_consoleRunning{false};

    int m_messagesReceived = 0;
};

} // namespace is::platform
