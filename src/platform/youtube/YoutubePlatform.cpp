#include "platform/youtube/YoutubePlatform.h"
#include "platform/youtube/YouTubeApi.h"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <array>
#include <chrono>
#include <cstdio>
#include <sstream>

#ifdef _WIN32
#define popen  _popen
#define pclose _pclose
#endif

namespace is::platform {

YoutubePlatform::YoutubePlatform() = default;

YoutubePlatform::~YoutubePlatform() {
    disconnect();
}

void YoutubePlatform::configure(const nlohmann::json& settings) {
    if (settings.contains("api_key"))       m_apiKey       = settings["api_key"];
    if (settings.contains("oauth_token"))   m_oauthToken   = settings["oauth_token"];
    if (settings.contains("live_chat_id"))  m_liveChatId   = settings["live_chat_id"];
    if (settings.contains("channel_id"))    m_channelId    = settings["channel_id"];
    if (settings.contains("poll_interval")) m_pollIntervalMs = settings["poll_interval"];

    // OAuth 2.0 credentials for token refresh
    if (settings.contains("oauth_client_id"))     m_oauthClientId     = settings["oauth_client_id"];
    if (settings.contains("oauth_client_secret"))  m_oauthClientSecret = settings["oauth_client_secret"];
    if (settings.contains("oauth_refresh_token"))  m_oauthRefreshToken = settings["oauth_refresh_token"];
    if (settings.contains("oauth_token_expiry"))   m_oauthTokenExpiry  = settings["oauth_token_expiry"];

    spdlog::info("[YouTube] Configured for channel: {}", m_channelId);
}

bool YoutubePlatform::connect() {
    if (m_apiKey.empty() && m_oauthToken.empty()) {
        spdlog::warn("[YouTube] Cannot connect: missing api_key and OAuth token.");
        return false;
    }

    // Refresh token before first API call (may be expired from previous session)
    refreshTokenIfNeeded();

    // If liveChatId was explicitly configured, keep it.  Otherwise auto-
    // detection is deferred to the background pollLoop where it will wait
    // until a stream is actually live (+ stabilisation delay).
    if (m_liveChatId.empty()) {
        spdlog::info("[YouTube] No live_chat_id configured — "
                     "will auto-detect once a stream is started.");
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
    // Use liveBroadcasts.list (OAuth) to find the liveChatId and broadcastId.
    // This requires an OAuth token — if we only have an API key, we can't auto-detect.
    if (m_oauthToken.empty()) {
        spdlog::warn("[YouTube] Cannot auto-detect liveChatId without OAuth token. "
                     "Please log in with YouTube or set live_chat_id manually.");
        return "";
    }
    auto info = YouTubeApi::findActiveBroadcast(m_oauthToken);
    if (!info.empty()) {
        // Cache the broadcast ID so StreamInstance can reuse it without a
        // separate API call to getActiveBroadcastId().
        m_broadcastId = info.broadcastId;
        spdlog::info("[YouTube] Cached broadcast ID: {} (status: {})",
                     m_broadcastId, info.lifeCycleStatus);
    }
    return info.liveChatId;
}

void YoutubePlatform::refreshTokenIfNeeded() {
    if (m_oauthRefreshToken.empty() || m_oauthClientId.empty() || m_oauthClientSecret.empty()) {
        return; // Can't refresh without credentials
    }

    auto now = std::chrono::system_clock::now().time_since_epoch();
    int64_t nowSec = std::chrono::duration_cast<std::chrono::seconds>(now).count();

    // Refresh 60 seconds before expiry to avoid race conditions
    if (m_oauthTokenExpiry > 0 && nowSec < (m_oauthTokenExpiry - 60)) {
        return; // Token still valid
    }

    spdlog::info("[YouTube] Access token expired or about to expire — refreshing...");
    auto result = YouTubeApi::refreshAccessToken(m_oauthRefreshToken, m_oauthClientId, m_oauthClientSecret);
    if (result.success) {
        m_oauthToken = result.accessToken;
        m_oauthTokenExpiry = nowSec + result.expiresIn;
        spdlog::info("[YouTube] Token refreshed successfully (expires in {}s).", result.expiresIn);
    } else {
        spdlog::error("[YouTube] Token refresh failed: {}", result.error);
    }
}

void YoutubePlatform::disconnect() {
    m_shouldRun = false;

#ifdef IS_YOUTUBE_GRPC_ENABLED
    // Stop gRPC streaming first (it may be blocking the poll thread)
    if (m_grpcChat) {
        m_grpcChat->stop();
        m_grpcChat.reset();
    }
    m_grpcActive = false;
#endif

    if (m_thread.joinable()) {
        m_thread.join();
    }
    m_connected = false;
    m_nextPageToken.clear();

    // Clear auto-detected chat ID so it gets re-fetched on next connect
    if (m_autoDetectedChatId) {
        m_liveChatId.clear();
        m_broadcastId.clear();
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
    nlohmann::json status = {
        {"platform", "youtube"},
        {"displayName", "YouTube"},
        {"connected", m_connected.load()},
        {"channelId", m_channelId},
        {"liveChatId", m_liveChatId},
        {"autoDetectedChatId", m_autoDetectedChatId},
        {"hasOauthToken", !m_oauthToken.empty()},
        {"hasRefreshToken", !m_oauthRefreshToken.empty()},
        {"oauthTokenExpiry", m_oauthTokenExpiry},
        {"waitingForLivestream", m_connected.load() && m_liveChatId.empty()},
        {"waitingForStreamStart", m_connected.load() && m_liveChatId.empty() && (!m_streamingChecker || !m_streamingChecker())},
        {"messagesReceived", m_messagesReceived},
        {"messagesSent", m_messagesSent}
    };

#ifdef IS_YOUTUBE_GRPC_ENABLED
    status["grpcEnabled"] = true;
    status["grpcActive"]  = m_grpcActive;
    if (m_grpcChat) {
        status["grpcConnected"]       = m_grpcChat->isConnected();
        status["grpcMessagesReceived"] = m_grpcChat->messagesReceived();
    }
#else
    status["grpcEnabled"] = false;
#endif

    return status;
}

nlohmann::json YoutubePlatform::getCurrentSettings() const {
    // Return the live (possibly refreshed) OAuth token and expiry so that
    // other components (e.g. StreamInstance) can use the latest credentials.
    // Also expose the cached broadcast ID discovered during chat auto-detection
    // so StreamInstance doesn't need a separate getActiveBroadcastId() call.
    nlohmann::json s = nlohmann::json::object();
    if (!m_oauthToken.empty()) {
        s["oauth_token"]        = m_oauthToken;
        s["oauth_token_expiry"] = m_oauthTokenExpiry;
    }
    if (!m_broadcastId.empty()) {
        s["broadcast_id"] = m_broadcastId;
    }
    return s;
}

void YoutubePlatform::setStreamingChecker(std::function<bool()> checker) {
    m_streamingChecker = std::move(checker);
}

bool YoutubePlatform::waitForStreaming() {
    // Wait until at least one stream is actively encoding.
    spdlog::info("[YouTube] Waiting for a stream to start before auto-detecting liveChatId...");
    while (m_shouldRun) {
        if (m_streamingChecker && m_streamingChecker()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    if (!m_shouldRun) return false;

    // Stabilisation delay – give the encoder a moment so YouTube registers
    // the ingest.  Since we now accept "ready" and "testing" broadcasts
    // (which already have a liveChatId), this can be kept short.
    constexpr int STABILISE_MS = 5000;
    spdlog::info("[YouTube] Stream detected — waiting {}s for YouTube to register the broadcast...",
                 STABILISE_MS / 1000);
    for (int waited = 0; waited < STABILISE_MS && m_shouldRun; waited += 500) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    return m_shouldRun;
}

void YoutubePlatform::pollLoop() {
    spdlog::info("[YouTube] Starting poll loop (interval: {}ms)...", m_pollIntervalMs);

    // Retry auto-detection every 60s to conserve API quota
    constexpr int RETRY_INTERVAL_MS = 60000;

    // ── Auto-detect liveChatId if needed ─────────────────────────────────
    if (m_liveChatId.empty()) {
        // Gate: wait until a stream is actually encoding before hitting the
        // YouTube API, otherwise we waste quota when no broadcast exists.
        if (!waitForStreaming()) return;
    }
    while (m_shouldRun && m_liveChatId.empty()) {
        refreshTokenIfNeeded();
        spdlog::debug("[YouTube] No liveChatId yet — retrying auto-detection via liveBroadcasts.list...");
        std::string chatId = fetchLiveChatId();
        if (!chatId.empty()) {
            m_liveChatId = chatId;
            m_autoDetectedChatId = true;
            spdlog::info("[YouTube] Broadcast detected! liveChatId: {}", m_liveChatId);
            break;
        }
        // Still no broadcast — wait and retry
        for (int waited = 0; waited < RETRY_INTERVAL_MS && m_shouldRun; waited += 500) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }

    if (!m_shouldRun) return;

    // ── Try gRPC streaming (preferred, near-instant message delivery) ────
#ifdef IS_YOUTUBE_GRPC_ENABLED
    if (tryGrpcStream()) {
        // gRPC ran until m_shouldRun became false or the stream ended.
        // If we should stop, return immediately.
        if (!m_shouldRun) return;

        // Otherwise fall through to REST polling as fallback
        spdlog::warn("[YouTube] gRPC streaming ended — falling back to REST polling.");
    }
#endif

    // ── REST polling fallback ────────────────────────────────────────────
    while (m_shouldRun) {
        // If we don't have a liveChatId yet, periodically retry auto-detection
        if (m_liveChatId.empty()) {
            // Gate: wait until a stream is actively encoding
            if (!waitForStreaming()) return;

            refreshTokenIfNeeded();
            spdlog::debug("[YouTube] No liveChatId yet — retrying auto-detection via liveBroadcasts.list...");
            std::string chatId = fetchLiveChatId();
            if (!chatId.empty()) {
                m_liveChatId = chatId;
                m_autoDetectedChatId = true;
                spdlog::info("[YouTube] Broadcast detected! liveChatId: {}", m_liveChatId);
            }

            if (m_liveChatId.empty()) {
                // Still no livestream — wait and retry
                for (int waited = 0; waited < RETRY_INTERVAL_MS && m_shouldRun; waited += 500) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                }
                continue;
            }
        }

        // YouTube Data API v3: liveChatMessages.list via curl (HTTPS)
        // Use OAuth Bearer token for authentication
        refreshTokenIfNeeded();

        std::string chatUrl = "https://www.googleapis.com/youtube/v3/liveChat/messages"
            "?liveChatId=" + YouTubeApi::urlEncode(m_liveChatId) +
            "&part=snippet,authorDetails";
        if (!m_nextPageToken.empty()) {
            chatUrl += "&pageToken=" + YouTubeApi::urlEncode(m_nextPageToken);
        }

        // Use curl with OAuth Bearer token
        std::string respBody;
        {
            std::ostringstream cmd;
            cmd << "curl -sS -X GET"
                << " -H \"Authorization: Bearer " << m_oauthToken << "\""
                << " \"" << chatUrl << "\"";
#ifdef _WIN32
            cmd << " 2>nul";
#else
            cmd << " 2>/dev/null";
#endif
            FILE* pipe = popen(cmd.str().c_str(), "r");
            if (pipe) {
                std::array<char, 4096> buf{};
                while (fgets(buf.data(), static_cast<int>(buf.size()), pipe))
                    respBody += buf.data();
                pclose(pipe);
            }
        }

        if (!respBody.empty()) {
            try {
                auto json = nlohmann::json::parse(respBody);

                // Check for API errors (quota exceeded, invalid chat ID, etc.)
                if (json.contains("error")) {
                    auto msg = json["error"].value("message", "unknown");
                    int code = json["error"].value("code", 0);
                    spdlog::warn("[YouTube] Chat API error ({}): {}", code, msg);
                    // If chat ended (403/404), clear so we can re-detect
                    if (code == 403 || code == 404) {
                        m_liveChatId.clear();
                        m_autoDetectedChatId = false;
                        m_nextPageToken.clear();
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(m_pollIntervalMs));
                    continue;
                }

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
                spdlog::warn("[YouTube] Failed to parse chat response: {}", e.what());
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(m_pollIntervalMs));
    }
}

// ── gRPC streaming ───────────────────────────────────────────────────────────

#ifdef IS_YOUTUBE_GRPC_ENABLED

bool YoutubePlatform::tryGrpcStream() {
    if (m_liveChatId.empty()) {
        spdlog::warn("[YouTube gRPC] Cannot start – no liveChatId.");
        return false;
    }

    if (m_apiKey.empty() && m_oauthToken.empty()) {
        spdlog::warn("[YouTube gRPC] Cannot start – no API key or OAuth token.");
        return false;
    }

    spdlog::info("[YouTube] Starting gRPC streaming mode for liveChatId '{}'.", m_liveChatId);
    m_grpcActive = true;

    m_grpcChat = std::make_unique<YouTubeGrpcChat>();
    m_grpcChat->start(
        m_apiKey, m_oauthToken, m_liveChatId, m_channelId,
        [this](ChatMessage msg) {
            // Callback from gRPC thread – push into shared queue
            m_messagesReceived++;
            std::lock_guard<std::mutex> lock(m_mutex);
            m_messageQueue.push(std::move(msg));
        }
    );

    // Wait while gRPC is running and we should keep going
    while (m_shouldRun && m_grpcChat && m_grpcChat->isRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    // Clean up
    if (m_grpcChat) {
        m_grpcChat->stop();
        m_grpcChat.reset();
    }
    m_grpcActive = false;

    return true; // We did run gRPC (success or failure)
}

#endif // IS_YOUTUBE_GRPC_ENABLED

} // namespace is::platform
