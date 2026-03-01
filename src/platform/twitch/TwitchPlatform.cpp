#include "platform/twitch/TwitchPlatform.h"
#include <spdlog/spdlog.h>
#include <SFML/Network.hpp>
#include <sstream>

namespace is::platform {

TwitchPlatform::TwitchPlatform() = default;

TwitchPlatform::~TwitchPlatform() {
    disconnect();
}

void TwitchPlatform::configure(const nlohmann::json& settings) {
    if (settings.contains("oauth_token"))  m_oauthToken  = settings["oauth_token"];
    if (settings.contains("bot_username")) m_botUsername  = settings["bot_username"];
    if (settings.contains("channel"))      m_channel      = settings["channel"];
    if (settings.contains("server"))       m_server       = settings["server"];
    if (settings.contains("port"))         m_port         = settings["port"];

    spdlog::info("[Twitch] Configured for channel: #{}", m_channel);
}

bool TwitchPlatform::connect() {
    if (m_oauthToken.empty() || m_channel.empty()) {
        spdlog::warn("[Twitch] Cannot connect: missing oauth_token or channel.");
        return false;
    }

    // Guard: disconnect first if already running (prevents assigning to a
    // joinable std::thread which is undefined behaviour / hangs on MSVC).
    if (m_shouldRun || m_thread.joinable()) {
        disconnect();
    }

    m_shouldRun = true;
    m_thread = std::thread(&TwitchPlatform::ircLoop, this);

    return true;
}

void TwitchPlatform::disconnect() {
    m_shouldRun = false;
    if (m_thread.joinable()) {
        m_thread.join();
    }
    m_connected = false;
    spdlog::info("[Twitch] Disconnected.");
}

bool TwitchPlatform::isConnected() const {
    return m_connected;
}

std::vector<ChatMessage> TwitchPlatform::pollMessages() {
    std::vector<ChatMessage> messages;
    std::lock_guard<std::mutex> lock(m_mutex);
    while (!m_messageQueue.empty()) {
        messages.push_back(std::move(m_messageQueue.front()));
        m_messageQueue.pop();
    }
    return messages;
}

bool TwitchPlatform::sendMessage(const std::string& text) {
    // TODO: Send via IRC socket
    spdlog::debug("[Twitch] Send: {}", text);
    m_messagesSent++;
    return true;
}

nlohmann::json TwitchPlatform::getStatus() const {
    return {
        {"platform", "twitch"},
        {"displayName", "Twitch"},
        {"connected", m_connected.load()},
        {"channel", m_channel},
        {"messagesReceived", m_messagesReceived},
        {"messagesSent", m_messagesSent}
    };
}

void TwitchPlatform::ircLoop() {
    spdlog::info("[Twitch] Connecting to {}:{}...", m_server, m_port);

    sf::TcpSocket socket;
    auto status = socket.connect(m_server, m_port, sf::seconds(10));
    if (status != sf::Socket::Done) {
        spdlog::error("[Twitch] Failed to connect to IRC server.");
        return;
    }

    // Authenticate
    std::string auth = "PASS oauth:" + m_oauthToken + "\r\n";
    auth += "NICK " + m_botUsername + "\r\n";
    auth += "JOIN #" + m_channel + "\r\n";
    // Request tags for user info
    auth += "CAP REQ :twitch.tv/tags twitch.tv/commands\r\n";

    socket.send(auth.c_str(), auth.size());
    m_connected = true;
    spdlog::info("[Twitch] Connected to #{}", m_channel);

    socket.setBlocking(false);
    char buffer[4096];

    while (m_shouldRun) {
        std::size_t received = 0;
        auto recvStatus = socket.receive(buffer, sizeof(buffer) - 1, received);

        if (recvStatus == sf::Socket::Done && received > 0) {
            buffer[received] = '\0';
            std::string data(buffer);

            // Split into lines
            std::istringstream stream(data);
            std::string line;
            while (std::getline(stream, line)) {
                if (line.empty()) continue;
                if (line.back() == '\r') line.pop_back();

                // Respond to PING
                if (line.substr(0, 4) == "PING") {
                    std::string pong = "PONG" + line.substr(4) + "\r\n";
                    socket.send(pong.c_str(), pong.size());
                    continue;
                }

                parseIrcMessage(line);
            }
        }

        // Small sleep to avoid busy-waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    socket.disconnect();
}

void TwitchPlatform::parseIrcMessage(const std::string& raw) {
    // Parse Twitch IRC message format:
    // @tags :user!user@user.tmi.twitch.tv PRIVMSG #channel :message
    if (raw.find("PRIVMSG") == std::string::npos) return;

    ChatMessage msg;
    msg.platformId = "twitch";
    msg.channelId = m_channel;

    // Parse tags
    std::string tags;
    std::string rest = raw;
    if (rest[0] == '@') {
        auto spacePos = rest.find(' ');
        if (spacePos != std::string::npos) {
            tags = rest.substr(1, spacePos - 1);
            rest = rest.substr(spacePos + 1);
        }
    }

    // Parse user
    if (rest[0] == ':') {
        auto bangPos = rest.find('!');
        if (bangPos != std::string::npos) {
            std::string rawUser = rest.substr(1, bangPos - 1);
            msg.userId = ChatMessage::makeUserId("twitch", rawUser);
            msg.displayName = rawUser;  // Will be overridden by tags if available
        }
    }

    // Parse message text
    auto msgStart = rest.find(" :", 1);
    if (msgStart != std::string::npos) {
        msg.text = rest.substr(msgStart + 2);
    }

    // Parse interesting tags
    if (!tags.empty()) {
        std::istringstream tagStream(tags);
        std::string tag;
        while (std::getline(tagStream, tag, ';')) {
            auto eqPos = tag.find('=');
            if (eqPos == std::string::npos) continue;
            std::string key = tag.substr(0, eqPos);
            std::string val = tag.substr(eqPos + 1);

            if (key == "display-name" && !val.empty()) {
                msg.displayName = val;
            } else if (key == "mod") {
                msg.isModerator = (val == "1");
            } else if (key == "subscriber") {
                msg.isSubscriber = (val == "1");
            } else if (key == "user-id") {
                msg.userId = ChatMessage::makeUserId("twitch", val);
            }
        }
    }

    if (msg.text.empty()) return;

    m_messagesReceived++;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_messageQueue.push(std::move(msg));
}

} // namespace is::platform
