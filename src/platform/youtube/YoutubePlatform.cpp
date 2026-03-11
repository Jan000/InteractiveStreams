#include "platform/youtube/YoutubePlatform.h"
#include "platform/youtube/YouTubeApi.h"
#include "platform/youtube/YouTubeQuota.h"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <algorithm>
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
    // OAuth-based detection (liveBroadcasts.list mine=true) — costs 1 quota unit
    if (!m_oauthToken.empty()) {
        spdlog::info("[YouTube] Trying OAuth-based broadcast detection...");
        auto info = YouTubeApi::findActiveBroadcast(m_oauthToken);
        if (!info.empty()) {
            m_broadcastId = info.broadcastId;
            m_detectionStatus = "Found via OAuth (" + info.lifeCycleStatus + ")";
            spdlog::info("[YouTube] Cached broadcast ID: {} (status: {})",
                         m_broadcastId, info.lifeCycleStatus);
            return info.liveChatId;
        }
        m_detectionStatus = "No live broadcast found";
        spdlog::info("[YouTube] OAuth detection found no usable broadcast.");
    } else {
        m_detectionStatus = "No OAuth token configured";
        spdlog::warn("[YouTube] Cannot detect liveChatId: no OAuth token configured.");
    }

    return "";
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
        {"hasApiKey", !m_apiKey.empty()},
        {"oauthTokenExpiry", m_oauthTokenExpiry},
        {"waitingForLivestream", m_connected.load() && m_liveChatId.empty()},
        {"waitingForStreamStart", m_connected.load() && m_liveChatId.empty() && (!m_streamingChecker || !m_streamingChecker())},
        {"detectionStatus", m_detectionStatus},
        {"messagesReceived", m_messagesReceived},
        {"messagesSent", m_messagesSent},
        {"lastMessageTime", m_lastMessageTime.load()},
        {"quota", YouTubeQuota::instance().toJson()}
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

void YoutubePlatform::requestDetection() {
    m_detectionRequested.store(true);
    spdlog::info("[YouTube] Manual broadcast detection requested.");
}

bool YoutubePlatform::waitForStreaming() {
    // Wait until at least one stream is actively encoding, or until a
    // generous timeout expires.  The timeout ensures we eventually try
    // the YouTube API even when no local encoder is running (e.g.
    // the user streams via OBS but reads chat through InteractiveStreams).
    constexpr int MAX_WAIT_MS = 300000; // 5 minutes
    spdlog::info("[YouTube] Waiting for a stream to start before auto-detecting liveChatId...");
    int totalWaited = 0;
    bool streamDetected = false;
    while (m_shouldRun) {
        if (m_streamingChecker && m_streamingChecker()) {
            streamDetected = true;
            break;
        }
        if (totalWaited >= MAX_WAIT_MS) {
            spdlog::info("[YouTube] Streaming wait timed out after {}s "
                         "— proceeding with broadcast detection.",
                         MAX_WAIT_MS / 1000);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        totalWaited += 500;
    }
    if (!m_shouldRun) return false;

    // Stabilisation delay – give the encoder a moment so YouTube registers
    // the ingest.  Only needed when we actually detected a local stream;
    // on timeout we skip this since we don't know if an ingest is happening.
    if (streamDetected) {
        constexpr int STABILISE_MS = 5000;
        spdlog::info("[YouTube] Stream detected — waiting {}s for YouTube to register the broadcast...",
                     STABILISE_MS / 1000);
        for (int waited = 0; waited < STABILISE_MS && m_shouldRun; waited += 500) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }
    return m_shouldRun;
}

void YoutubePlatform::pollLoop() {
    spdlog::info("[YouTube] Starting poll loop (interval: {}ms)...", m_pollIntervalMs);

    // ── Outer loop: cycles through detect → chat → reset for 24/7 operation ──
    // When a stream ends (gRPC chatEnded or REST 403/404), m_liveChatId is
    // cleared and we loop back here to wait for the next broadcast.
    while (m_shouldRun) {

    // ── Auto-detect liveChatId if needed ─────────────────────────────────
    // Strategy: wait for a local stream to start, then wait 10s for YouTube
    // to register the ingest, attempt detection twice (with 30s gap), then
    // stop and wait for a manual detection request from the dashboard.
    // This ensures we never burn quota in the background.
    constexpr int MAX_AUTO_ATTEMPTS = 2;
    constexpr int DELAY_BEFORE_FIRST_MS = 10000;  // 10s after stream start
    constexpr int DELAY_BETWEEN_ATTEMPTS_MS = 30000; // 30s between attempts

    if (m_liveChatId.empty()) {
        // Wait for a local stream to start encoding.  Do NOT try the API
        // before that — this is what was burning quota.
        if (!waitForStreaming()) return;

        // Stream started — wait 10 seconds for YouTube to register the ingest
        spdlog::info("[YouTube] Stream detected — waiting {}s for YouTube to register...",
                     DELAY_BEFORE_FIRST_MS / 1000);
        for (int w = 0; w < DELAY_BEFORE_FIRST_MS && m_shouldRun; w += 500)
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (!m_shouldRun) return;

        // Try detection up to MAX_AUTO_ATTEMPTS times
        // Auto-detection only uses cheap OAuth path (1 unit).
        for (int attempt = 0; attempt < MAX_AUTO_ATTEMPTS && m_shouldRun && m_liveChatId.empty(); ++attempt) {
            refreshTokenIfNeeded();
            spdlog::info("[YouTube] Auto-detection attempt {}/{} ...", attempt + 1, MAX_AUTO_ATTEMPTS);
            std::string chatId = fetchLiveChatId();
            if (!chatId.empty()) {
                m_liveChatId = chatId;
                m_autoDetectedChatId = true;
                spdlog::info("[YouTube] Broadcast detected! liveChatId: {}", m_liveChatId);
                break;
            }
            if (attempt + 1 < MAX_AUTO_ATTEMPTS) {
                spdlog::info("[YouTube] Attempt {} failed — retrying in {}s...",
                             attempt + 1, DELAY_BETWEEN_ATTEMPTS_MS / 1000);
                for (int w = 0; w < DELAY_BETWEEN_ATTEMPTS_MS && m_shouldRun; w += 500)
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        }

        if (m_liveChatId.empty() && m_shouldRun) {
            m_detectionStatus = "Auto-detection exhausted (2 attempts). Use manual detect.";
            spdlog::warn("[YouTube] Auto-detection exhausted after {} attempts. "
                         "Waiting for manual detection request from dashboard.",
                         MAX_AUTO_ATTEMPTS);
        }
    }

    // ── Wait for manual detection requests if auto failed ────────────────
    while (m_shouldRun && m_liveChatId.empty()) {
        // Sleep in 500ms increments, checking for manual detection request
        for (int w = 0; w < 1000 && m_shouldRun; w += 500) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        if (!m_shouldRun) return;

        if (m_detectionRequested.exchange(false)) {
            refreshTokenIfNeeded();
            spdlog::info("[YouTube] Running manually-requested broadcast detection...");
            m_detectionStatus = "Manual detection in progress...";
            std::string chatId = fetchLiveChatId();
            if (!chatId.empty()) {
                m_liveChatId = chatId;
                m_autoDetectedChatId = true;
                m_detectionStatus = "Found via manual detection";
                spdlog::info("[YouTube] Manual detection succeeded! liveChatId: {}", m_liveChatId);
            } else {
                m_detectionStatus = "Manual detection: no broadcast found. Try again.";
                spdlog::warn("[YouTube] Manual detection found no broadcast.");
            }
        }
    }

    if (!m_shouldRun) return;

    // ── Try gRPC streaming (preferred, near-instant message delivery) ────
#ifdef IS_YOUTUBE_GRPC_ENABLED
    if (tryGrpcStream()) {
        // gRPC ran until m_shouldRun became false or the stream ended.
        if (!m_shouldRun) return;

        // If the live chat ended, cycle back to wait-for-stream mode
        if (m_liveChatId.empty()) {
            spdlog::info("[YouTube] Chat ended — returning to wait-for-stream mode.");
            continue; // outer while(m_shouldRun) loop
        }

        // Otherwise fall through to REST polling as fallback
        spdlog::warn("[YouTube] gRPC streaming ended — falling back to REST polling.");
    }
#endif

    // ── REST polling fallback ────────────────────────────────────────────
    while (m_shouldRun) {
        // If we don't have a liveChatId, wait for manual detection request
        if (m_liveChatId.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            if (m_detectionRequested.exchange(false)) {
                refreshTokenIfNeeded();
                spdlog::info("[YouTube] REST fallback: manual detection requested...");
                m_detectionStatus = "Manual detection in progress...";
                std::string chatId = fetchLiveChatId();
                if (!chatId.empty()) {
                    m_liveChatId = chatId;
                    m_autoDetectedChatId = true;
                    m_detectionStatus = "Found via manual detection";
                    spdlog::info("[YouTube] REST fallback: Broadcast detected! liveChatId: {}", m_liveChatId);
                } else {
                    m_detectionStatus = "Manual detection: no broadcast found. Try again.";
                    spdlog::warn("[YouTube] REST fallback: manual detection found no broadcast.");
                }
            }
            continue;
        }

        // YouTube Data API v3: liveChatMessages.list via curl (HTTPS)
        // Prefer OAuth Bearer token, fall back to API key
        refreshTokenIfNeeded();

        // liveChatMessages.list costs 1 quota unit
        if (!YouTubeQuota::instance().consume(YouTubeQuota::COST_LIST, "liveChatMessages.list (pollChat)")) {
            spdlog::warn("[YouTube] Daily quota budget exhausted — pausing REST chat polling.");
            std::this_thread::sleep_for(std::chrono::seconds(60));
            continue;
        }

        std::string chatUrl = "https://www.googleapis.com/youtube/v3/liveChat/messages"
            "?liveChatId=" + YouTubeApi::urlEncode(m_liveChatId) +
            "&part=snippet,authorDetails";
        if (!m_nextPageToken.empty()) {
            chatUrl += "&pageToken=" + YouTubeApi::urlEncode(m_nextPageToken);
        }
        // Append API key to URL when using key-based auth
        bool useOAuth = !m_oauthToken.empty();
        if (!useOAuth && !m_apiKey.empty()) {
            chatUrl += "&key=" + YouTubeApi::urlEncode(m_apiKey);
        }

        std::string respBody;
        {
            std::ostringstream cmd;
            cmd << "curl -sS -X GET";
            if (useOAuth) {
                cmd << " -H \"Authorization: Bearer " << m_oauthToken << "\"";
            }
            cmd << " \"" << chatUrl << "\"";
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
                    // If chat ended (403/404), clear and return to wait-for-stream
                    if (code == 403 || code == 404) {
                        spdlog::info("[YouTube] Chat closed (HTTP {}) — resetting to wait for next broadcast.", code);
                        m_liveChatId.clear();
                        m_broadcastId.clear();
                        m_autoDetectedChatId = false;
                        m_nextPageToken.clear();
                        m_detectionStatus = "Stream ended. Waiting for next broadcast.";
                    }
                    // Back off on errors to avoid spin-looping
                    std::this_thread::sleep_for(std::chrono::milliseconds(5000));
                    // If chat ID was cleared, break REST loop to cycle outer loop
                    if (m_liveChatId.empty()) break;
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
                            m_lastMessageTime.store(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
                            std::lock_guard<std::mutex> lock(m_mutex);
                            m_messageQueue.push(std::move(msg));
                        }
                    }
                }

                // Use pollingIntervalMillis from API response if available,
                // but enforce a minimum of 4000ms to limit quota burn.
                if (json.contains("pollingIntervalMillis")) {
                    int interval = std::max(4000, json["pollingIntervalMillis"].get<int>());
                    std::this_thread::sleep_for(std::chrono::milliseconds(interval));
                    continue;
                }
            } catch (const std::exception& e) {
                spdlog::warn("[YouTube] Failed to parse chat response: {}", e.what());
                std::this_thread::sleep_for(std::chrono::milliseconds(5000));
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(std::max(4000, m_pollIntervalMs)));
    }

    } // end outer while(m_shouldRun) — cycles back to wait-for-stream
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
            m_lastMessageTime.store(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
            std::lock_guard<std::mutex> lock(m_mutex);
            m_messageQueue.push(std::move(msg));
        }
    );

    // Wait while gRPC is running and we should keep going.
    // Periodically refresh the OAuth token and propagate it to the gRPC client.
    while (m_shouldRun && m_grpcChat && m_grpcChat->isRunning()) {
        refreshTokenIfNeeded();
        if (m_grpcChat) {
            m_grpcChat->updateToken(m_oauthToken);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    // Clean up
    bool streamEnded = false;
    bool quotaHit = false;
    if (m_grpcChat) {
        streamEnded = m_grpcChat->chatEnded();
        quotaHit = m_grpcChat->quotaExhausted();
        m_grpcChat->stop();
        m_grpcChat.reset();
    }
    m_grpcActive = false;

    // If the live chat ended (offline / CHAT_ENDED_EVENT / NOT_FOUND),
    // clear the chat ID so pollLoop returns to wait-for-stream mode.
    if (streamEnded) {
        spdlog::info("[YouTube] Live chat ended — resetting chat ID to wait for next broadcast.");
        m_liveChatId.clear();
        m_broadcastId.clear();
        m_autoDetectedChatId = false;
        m_nextPageToken.clear();
        m_detectionStatus = "Stream ended. Waiting for next broadcast.";
    }

    // YouTube-side quota exhausted — REST would hit the same limit.
    // Pause for 1 hour instead of burning quota on REST retries.
    if (quotaHit) {
        constexpr int QUOTA_PAUSE_SEC = 3600;
        spdlog::warn("[YouTube] YouTube quota exhausted — pausing for {} min before retrying.",
                     QUOTA_PAUSE_SEC / 60);
        m_detectionStatus = "YouTube quota exhausted. Pausing for 1 hour.";
        for (int i = 0; i < QUOTA_PAUSE_SEC && m_shouldRun; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        // After pause, cycle back to outer loop (will retry gRPC)
        m_liveChatId.clear();
        m_broadcastId.clear();
        m_autoDetectedChatId = false;
        m_nextPageToken.clear();
        m_detectionStatus = "Quota pause ended. Waiting for next broadcast.";
    }

    return true; // We did run gRPC (success or failure)
}

#endif // IS_YOUTUBE_GRPC_ENABLED

} // namespace is::platform
