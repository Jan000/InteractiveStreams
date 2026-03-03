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
    if (m_apiKey.empty()) {
        spdlog::warn("[YouTube] Cannot connect: missing api_key.");
        return false;
    }

    // Auto-detect live chat ID if not manually provided
    if (m_liveChatId.empty() && !m_channelId.empty()) {
        spdlog::info("[YouTube] No live_chat_id configured — auto-detecting from channel '{}'...", m_channelId);
        m_liveChatId = fetchLiveChatId();
        if (m_liveChatId.empty()) {
            spdlog::info("[YouTube] No active livestream found yet on channel '{}'. "
                         "Will connect and retry auto-detection periodically.", m_channelId);
        } else {
            m_autoDetectedChatId = true;
            spdlog::info("[YouTube] Auto-detected liveChatId: {}", m_liveChatId);
        }
    }

    // Guard: disconnect first if already running (prevents assigning to a
    // joinable std::thread which is undefined behaviour / hangs on MSVC).
    if (m_shouldRun || m_thread.joinable()) {
        disconnect();
    }

    m_shouldRun = true;
    m_thread = std::thread(&YoutubePlatform::pollLoop, this);
    m_connected = true;

    return true;
}

std::string YoutubePlatform::fetchLiveChatId() {
    // Step 1: Find the live video on this channel.
    // GET https://www.googleapis.com/youtube/v3/search
    //   ?part=id&channelId={channelId}&eventType=live&type=video&key={apiKey}
    sf::Http http("www.googleapis.com");

    std::string searchPath = "/youtube/v3/search"
        "?part=id"
        "&channelId=" + m_channelId +
        "&eventType=live"
        "&type=video"
        "&key=" + m_apiKey;

    sf::Http::Request searchReq(searchPath);
    auto searchResp = http.sendRequest(searchReq, sf::seconds(15));

    if (searchResp.getStatus() != sf::Http::Response::Ok) {
        spdlog::warn("[YouTube] search.list failed (HTTP {})", static_cast<int>(searchResp.getStatus()));
        return "";
    }

    std::string videoId;
    try {
        auto json = nlohmann::json::parse(searchResp.getBody());
        if (!json.contains("items") || json["items"].empty()) {
            spdlog::warn("[YouTube] No active livestream found on channel '{}'.", m_channelId);
            return "";
        }
        // Take the first live video
        videoId = json["items"][0]["id"]["videoId"].get<std::string>();
        spdlog::info("[YouTube] Found live video: {}", videoId);
    } catch (const std::exception& e) {
        spdlog::warn("[YouTube] Failed to parse search response: {}", e.what());
        return "";
    }

    // Step 2: Get the activeLiveChatId from the video's liveStreamingDetails.
    // GET https://www.googleapis.com/youtube/v3/videos
    //   ?part=liveStreamingDetails&id={videoId}&key={apiKey}
    std::string videoPath = "/youtube/v3/videos"
        "?part=liveStreamingDetails"
        "&id=" + videoId +
        "&key=" + m_apiKey;

    sf::Http::Request videoReq(videoPath);
    auto videoResp = http.sendRequest(videoReq, sf::seconds(15));

    if (videoResp.getStatus() != sf::Http::Response::Ok) {
        spdlog::warn("[YouTube] videos.list failed (HTTP {})", static_cast<int>(videoResp.getStatus()));
        return "";
    }

    try {
        auto json = nlohmann::json::parse(videoResp.getBody());
        if (!json.contains("items") || json["items"].empty()) {
            spdlog::warn("[YouTube] Video {} not found.", videoId);
            return "";
        }
        auto& details = json["items"][0]["liveStreamingDetails"];
        if (!details.contains("activeLiveChatId")) {
            spdlog::warn("[YouTube] Video {} has no activeLiveChatId (stream may have ended).", videoId);
            return "";
        }
        return details["activeLiveChatId"].get<std::string>();
    } catch (const std::exception& e) {
        spdlog::warn("[YouTube] Failed to parse video response: {}", e.what());
        return "";
    }
}

void YoutubePlatform::disconnect() {
    m_shouldRun = false;
    if (m_thread.joinable()) {
        m_thread.join();
    }
    m_connected = false;
    m_nextPageToken.clear();

    // Clear auto-detected chat ID so it gets re-fetched on next connect
    if (m_autoDetectedChatId) {
        m_liveChatId.clear();
        m_autoDetectedChatId = false;
    }

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
        {"autoDetectedChatId", m_autoDetectedChatId},
        {"waitingForLivestream", m_connected.load() && m_liveChatId.empty()},
        {"messagesReceived", m_messagesReceived},
        {"messagesSent", m_messagesSent}
    };
}

void YoutubePlatform::pollLoop() {
    spdlog::info("[YouTube] Starting poll loop (interval: {}ms)...", m_pollIntervalMs);

    constexpr int RETRY_INTERVAL_MS = 30000; // retry auto-detection every 30s

    while (m_shouldRun) {
        // If we don't have a liveChatId yet, periodically retry auto-detection
        if (m_liveChatId.empty()) {
            if (!m_channelId.empty()) {
                spdlog::debug("[YouTube] No liveChatId yet — retrying auto-detection for channel '{}'...", m_channelId);
                std::string chatId = fetchLiveChatId();
                if (!chatId.empty()) {
                    m_liveChatId = chatId;
                    m_autoDetectedChatId = true;
                    spdlog::info("[YouTube] Livestream detected! liveChatId: {}", m_liveChatId);
                }
            }

            if (m_liveChatId.empty()) {
                // Still no livestream — wait and retry
                for (int waited = 0; waited < RETRY_INTERVAL_MS && m_shouldRun; waited += 500) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                }
                continue;
            }
        }

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
