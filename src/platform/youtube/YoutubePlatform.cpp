#include "platform/youtube/YoutubePlatform.h"
#include <spdlog/spdlog.h>
#include <SFML/Network.hpp>

namespace is::platform {

YoutubePlatform::YoutubePlatform() = default;

YoutubePlatform::~YoutubePlatform() {
    disconnect();
}

void YoutubePlatform::configure(const nlohmann::json& settings) {
    if (settings.contains("api_key"))       m_apiKey       = settings["api_key"];
    if (settings.contains("live_chat_id"))  m_liveChatId   = settings["live_chat_id"];
    if (settings.contains("channel_id"))    m_channelId    = settings["channel_id"];
    if (settings.contains("poll_interval")) m_pollIntervalMs = settings["poll_interval"];

    spdlog::info("[YouTube] Configured for channel: {}", m_channelId);
}

bool YoutubePlatform::connect() {
    if (m_apiKey.empty() || m_liveChatId.empty()) {
        spdlog::warn("[YouTube] Cannot connect: missing api_key or live_chat_id.");
        return false;
    }

    m_shouldRun = true;
    m_thread = std::thread(&YoutubePlatform::pollLoop, this);
    m_connected = true;

    return true;
}

void YoutubePlatform::disconnect() {
    m_shouldRun = false;
    if (m_thread.joinable()) {
        m_thread.join();
    }
    m_connected = false;
    spdlog::info("[YouTube] Disconnected.");
}

bool YoutubePlatform::isConnected() const {
    return m_connected;
}

std::vector<ChatMessage> YoutubePlatform::pollMessages() {
    std::vector<ChatMessage> messages;
    std::lock_guard<std::mutex> lock(m_mutex);
    while (!m_messageQueue.empty()) {
        messages.push_back(std::move(m_messageQueue.front()));
        m_messageQueue.pop();
    }
    return messages;
}

bool YoutubePlatform::sendMessage(const std::string& text) {
    // TODO: Implement via YouTube Live Streaming API
    spdlog::debug("[YouTube] Send: {}", text);
    m_messagesSent++;
    return true;
}

nlohmann::json YoutubePlatform::getStatus() const {
    return {
        {"platform", "youtube"},
        {"displayName", "YouTube"},
        {"connected", m_connected.load()},
        {"channelId", m_channelId},
        {"liveChatId", m_liveChatId},
        {"messagesReceived", m_messagesReceived},
        {"messagesSent", m_messagesSent}
    };
}

void YoutubePlatform::pollLoop() {
    spdlog::info("[YouTube] Starting poll loop (interval: {}ms)...", m_pollIntervalMs);

    while (m_shouldRun) {
        // YouTube Data API v3: liveChatMessages.list
        // GET https://www.googleapis.com/youtube/v3/liveChat/messages
        //   ?liveChatId={liveChatId}&part=snippet,authorDetails&key={apiKey}&pageToken={pageToken}

        sf::Http http("www.googleapis.com");
        std::string path = "/youtube/v3/liveChat/messages"
            "?liveChatId=" + m_liveChatId +
            "&part=snippet,authorDetails"
            "&key=" + m_apiKey;
        if (!m_nextPageToken.empty()) {
            path += "&pageToken=" + m_nextPageToken;
        }

        sf::Http::Request request(path);
        auto response = http.sendRequest(request, sf::seconds(10));

        if (response.getStatus() == sf::Http::Response::Ok) {
            try {
                auto json = nlohmann::json::parse(response.getBody());

                if (json.contains("nextPageToken")) {
                    m_nextPageToken = json["nextPageToken"];
                }

                if (json.contains("items")) {
                    for (const auto& item : json["items"]) {
                        ChatMessage msg;
                        msg.platformId = "youtube";
                        msg.channelId = m_channelId;

                        if (item.contains("authorDetails")) {
                            auto& author = item["authorDetails"];
                            std::string rawId = author.value("channelId", "unknown");
                            msg.userId = ChatMessage::makeUserId("youtube", rawId);
                            msg.displayName = author.value("displayName", "Unknown");
                            msg.isModerator = author.value("isChatModerator", false);
                        }

                        if (item.contains("snippet")) {
                            msg.text = item["snippet"].value("displayMessage", "");
                        }

                        if (!msg.text.empty()) {
                            m_messagesReceived++;
                            std::lock_guard<std::mutex> lock(m_mutex);
                            m_messageQueue.push(std::move(msg));
                        }
                    }
                }

                // Use pollingIntervalMillis from API response if available
                if (json.contains("pollingIntervalMillis")) {
                    int interval = json["pollingIntervalMillis"];
                    std::this_thread::sleep_for(std::chrono::milliseconds(interval));
                    continue;
                }
            } catch (const std::exception& e) {
                spdlog::warn("[YouTube] Failed to parse response: {}", e.what());
            }
        } else {
            spdlog::warn("[YouTube] API request failed with status: {}",
                static_cast<int>(response.getStatus()));
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(m_pollIntervalMs));
    }
}

} // namespace is::platform
