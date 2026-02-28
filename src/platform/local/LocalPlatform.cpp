#include "platform/local/LocalPlatform.h"

#include <spdlog/spdlog.h>
#include <iostream>
#include <chrono>

namespace is::platform {

LocalPlatform::LocalPlatform() = default;

LocalPlatform::~LocalPlatform() {
    disconnect();
}

bool LocalPlatform::connect() {
    m_connected = true;
    spdlog::info("[Local] Local test platform connected.");

    // Start optional console input thread
    if (m_consoleEnabled && !m_consoleRunning) {
        m_consoleRunning = true;
        m_consoleThread = std::make_unique<std::thread>(&LocalPlatform::consoleInputLoop, this);
    }

    return true;
}

void LocalPlatform::disconnect() {
    m_connected = false;
    m_consoleRunning = false;

    if (m_consoleThread && m_consoleThread->joinable()) {
        // Console thread blocks on stdin – detach to avoid deadlock
        m_consoleThread->detach();
        m_consoleThread.reset();
    }

    spdlog::info("[Local] Local test platform disconnected.");
}

bool LocalPlatform::isConnected() const {
    return m_connected;
}

std::vector<ChatMessage> LocalPlatform::pollMessages() {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    std::vector<ChatMessage> messages;
    while (!m_messageQueue.empty()) {
        messages.push_back(std::move(m_messageQueue.front()));
        m_messageQueue.pop();
    }
    return messages;
}

bool LocalPlatform::sendMessage(const std::string& text) {
    // Store outgoing messages in log (visible via API/dashboard)
    {
        std::lock_guard<std::mutex> lock(m_logMutex);
        m_messageLog.push_back(text);
        // Keep only last 100 messages
        if (m_messageLog.size() > 100) {
            m_messageLog.erase(m_messageLog.begin());
        }
    }
    spdlog::info("[Local] > {}", text);
    return true;
}

nlohmann::json LocalPlatform::getStatus() const {
    return {
        {"platform", "local"},
        {"displayName", "Local (Test)"},
        {"connected", m_connected.load()},
        {"consoleInput", m_consoleEnabled},
        {"messagesReceived", m_messagesReceived}
    };
}

void LocalPlatform::configure(const nlohmann::json& settings) {
    if (settings.contains("console_input")) {
        m_consoleEnabled = settings["console_input"].get<bool>();
    }
}

void LocalPlatform::injectMessage(const std::string& username, const std::string& text) {
    ChatMessage msg;
    msg.platformId  = "local";
    msg.channelId   = "local";
    msg.userId      = ChatMessage::makeUserId("local", username);
    msg.displayName = username;
    msg.text        = text;
    msg.isModerator = false;
    msg.isSubscriber = false;
    msg.timestamp   = std::chrono::duration<double>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_messageQueue.push(std::move(msg));
    }
    m_messagesReceived++;

    spdlog::debug("[Local] Message injected: {} -> {}", username, text);
}

std::vector<std::string> LocalPlatform::getMessageLog() const {
    std::lock_guard<std::mutex> lock(m_logMutex);
    return m_messageLog;
}

void LocalPlatform::consoleInputLoop() {
    spdlog::info("[Local] Console input active. Type chat commands (e.g. '!join', '!attack').");
    spdlog::info("[Local] Format: [username] message  (default user: 'console').");

    while (m_consoleRunning) {
        std::string line;
        if (!std::getline(std::cin, line)) {
            break;  // stdin closed
        }

        if (line.empty()) continue;

        std::string username = "console";
        std::string text = line;

        // Optional: parse "[username] text" format
        if (line.front() == '[') {
            auto closeBracket = line.find(']');
            if (closeBracket != std::string::npos && closeBracket + 2 <= line.size()) {
                username = line.substr(1, closeBracket - 1);
                text = line.substr(closeBracket + 2);
            }
        }

        injectMessage(username, text);
    }
}

} // namespace is::platform
