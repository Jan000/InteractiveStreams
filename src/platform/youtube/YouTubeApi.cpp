#include "platform/youtube/YouTubeApi.h"
#include "platform/youtube/YouTubeQuota.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <sstream>
#include <fstream>
#include <array>
#include <cstdio>
#include <filesystem>

#ifdef _WIN32
#define popen  _popen
#define pclose _pclose
#endif

namespace is::platform {

// ── Helpers ──────────────────────────────────────────────────────────────────

/// Escape a string for safe use inside a shell argument (double-quoted).
static std::string shellEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

/// URL-encode a string (only the unsafe characters).
std::string YouTubeApi::urlEncode(const std::string& s) {
    std::ostringstream out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out << c;
        } else {
            out << '%'
                << "0123456789ABCDEF"[c >> 4]
                << "0123456789ABCDEF"[c & 0x0F];
        }
    }
    return out.str();
}

// ── curl wrapper ─────────────────────────────────────────────────────────────

std::string YouTubeApi::curlRequest(const std::string& method,
                                    const std::string& url,
                                    const std::string& oauthToken,
                                    const std::string& body)
{
    // Write the JSON body to a temp file to avoid shell escaping issues.
    std::string tempBodyPath;
    if (!body.empty()) {
        try {
            auto tmp = std::filesystem::temp_directory_path() / "is_youtube_body.json";
            tempBodyPath = tmp.string();
            std::ofstream ofs(tempBodyPath, std::ios::trunc);
            ofs << body;
            ofs.close();
        } catch (const std::exception& e) {
            spdlog::error("[YouTubeApi] Failed to write temp body file: {}", e.what());
            return "";
        }
    }

    std::ostringstream cmd;
    cmd << "curl -sS -X " << method
        << " \"" << shellEscape(url) << "\"";

    // Only add Authorization header if we have an OAuth token
    if (!oauthToken.empty()) {
        cmd << " -H \"Authorization: Bearer " << shellEscape(oauthToken) << "\"";
    }

    if (!tempBodyPath.empty()) {
        cmd << " -H \"Content-Type: application/json\""
            << " -d @\"" << shellEscape(tempBodyPath) << "\"";
    }

    // Redirect stderr to suppress curl progress output
#ifdef _WIN32
    cmd << " 2>nul";
#else
    cmd << " 2>/dev/null";
#endif

    std::string cmdStr = cmd.str();
    spdlog::debug("[YouTubeApi] Running: {}", cmdStr);

    FILE* pipe = popen(cmdStr.c_str(), "r");
    if (!pipe) {
        spdlog::error("[YouTubeApi] Failed to run curl.");
        if (!tempBodyPath.empty()) std::filesystem::remove(tempBodyPath);
        return "";
    }

    std::string result;
    std::array<char, 4096> buffer{};
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
        result += buffer.data();
    }
    int rc = pclose(pipe);

    // Clean up temp file
    if (!tempBodyPath.empty()) {
        std::error_code ec;
        std::filesystem::remove(tempBodyPath, ec);
    }

    if (rc != 0) {
        spdlog::warn("[YouTubeApi] curl exited with code {}", rc);
    }

    spdlog::debug("[YouTubeApi] Response: {}", result);
    return result;
}

// ── Public API ───────────────────────────────────────────────────────────────

bool YouTubeApi::isCurlAvailable() {
#ifdef _WIN32
    int rc = std::system("curl --version >nul 2>nul");
#else
    int rc = std::system("curl --version >/dev/null 2>/dev/null");
#endif
    return rc == 0;
}

std::string YouTubeApi::findLiveChatId(const std::string& oauthToken)
{
    return findActiveBroadcast(oauthToken).liveChatId;
}

YouTubeApi::BroadcastInfo YouTubeApi::findActiveBroadcast(const std::string& oauthToken)
{
    if (oauthToken.empty()) return {};

    // Costs 1 quota unit (liveBroadcasts.list)
    if (!YouTubeQuota::instance().consume(YouTubeQuota::COST_LIST)) {
        spdlog::warn("[YouTubeApi] findActiveBroadcast: quota budget exhausted.");
        return {};
    }

    // Use liveBroadcasts.list with mine=true to find a broadcast.
    // NOTE: mine=true and broadcastStatus are MUTUALLY EXCLUSIVE filters in
    //       the YouTube API – using both returns HTTP 400.  We fetch all of
    //       the authenticated user's broadcasts and pick the best one
    //       client-side.
    //
    // YouTube assigns a liveChatId as soon as a broadcast is CREATED
    // (status "ready"), so we can start reading chat before the broadcast
    // transitions to "live".  Priority: live > testing > ready.
    std::string url = "https://www.googleapis.com/youtube/v3/liveBroadcasts"
                      "?mine=true"
                      "&broadcastType=all"
                      "&part=id,snippet,status"
                      "&maxResults=50";

    std::string resp = curlRequest("GET", url, oauthToken);
    if (resp.empty()) {
        spdlog::warn("[YouTubeApi] findActiveBroadcast: empty response from liveBroadcasts.list");
        return {};
    }

    try {
        auto j = nlohmann::json::parse(resp);

        if (j.contains("error")) {
            auto msg = j["error"].value("message", "unknown error");
            int code = j["error"].value("code", 0);
            spdlog::warn("[YouTubeApi] liveBroadcasts.list error ({}): {}", code, msg);
            return {};
        }

        if (!j.contains("items") || !j["items"].is_array() || j["items"].empty()) {
            spdlog::debug("[YouTubeApi] No broadcasts found for this account.");
            return {};
        }

        // Status priority: live=3, testing=2, ready=1
        auto statusPriority = [](const std::string& s) -> int {
            if (s == "live")    return 3;
            if (s == "testing") return 2;
            if (s == "ready")   return 1;
            return 0;  // complete, revoked, etc. — skip
        };

        BroadcastInfo best;
        int bestPrio = 0;

        for (const auto& item : j["items"]) {
            if (!item.contains("snippet")) continue;
            auto& snippet = item["snippet"];

            // lifeCycleStatus lives in the "status" resource, not "snippet"
            std::string status;
            if (item.contains("status") && item["status"].is_object()) {
                status = item["status"].value("lifeCycleStatus", "");
            }
            int prio = statusPriority(status);
            if (prio == 0) continue;  // Skip completed/revoked broadcasts

            std::string liveChatId = snippet.value("liveChatId", "");
            if (liveChatId.empty()) continue;  // No chat available

            if (prio > bestPrio) {
                best.liveChatId      = liveChatId;
                best.broadcastId     = item.value("id", "");
                best.title           = snippet.value("title", "(unknown)");
                best.lifeCycleStatus = status;
                bestPrio = prio;
            }

            if (prio == 3) break;  // "live" is the highest priority, stop searching
        }

        if (!best.empty()) {
            spdlog::info("[YouTubeApi] Found broadcast '{}' (status={}) with liveChatId: {}",
                         best.title, best.lifeCycleStatus, best.liveChatId);
            return best;
        }

        // Log all statuses for debugging
        std::string statuses;
        for (const auto& item : j["items"]) {
            if (!statuses.empty()) statuses += ", ";
            if (item.contains("status") && item["status"].is_object()) {
                statuses += item["status"].value("lifeCycleStatus", "?");
            } else {
                statuses += "(no status)";
            }
        }
        spdlog::info("[YouTubeApi] No usable broadcast found ({} checked, statuses: {}).",
                      j["items"].size(), statuses);
    } catch (const std::exception& e) {
        spdlog::warn("[YouTubeApi] Failed to parse liveBroadcasts.list response: {}", e.what());
    }
    return {};
}

YouTubeApi::BroadcastInfo YouTubeApi::findBroadcastByChannel(
    const std::string& apiKey,
    const std::string& channelId)
{
    if (apiKey.empty() || channelId.empty()) return {};

    // Step 1 costs 100 quota units (search.list)
    if (!YouTubeQuota::instance().consume(YouTubeQuota::COST_SEARCH)) {
        spdlog::warn("[YouTubeApi] findBroadcastByChannel: quota budget exhausted.");
        return {};
    }

    // Step 1: Search for a live video on this channel
    std::string searchUrl =
        "https://www.googleapis.com/youtube/v3/search"
        "?channelId=" + urlEncode(channelId) +
        "&eventType=live"
        "&type=video"
        "&part=id"
        "&maxResults=1"
        "&key=" + urlEncode(apiKey);

    std::string searchResp = curlRequest("GET", searchUrl, "" /*no auth header*/);
    if (searchResp.empty()) {
        spdlog::warn("[YouTubeApi] findBroadcastByChannel: empty response from search");
        return {};
    }

    std::string videoId;
    try {
        auto j = nlohmann::json::parse(searchResp);

        if (j.contains("error")) {
            auto msg = j["error"].value("message", "unknown error");
            int code = j["error"].value("code", 0);
            spdlog::warn("[YouTubeApi] search.list error ({}): {}", code, msg);
            return {};
        }

        if (!j.contains("items") || !j["items"].is_array() || j["items"].empty()) {
            spdlog::info("[YouTubeApi] No live videos found for channel {}.", channelId);
            return {};
        }

        videoId = j["items"][0]["id"].value("videoId", "");
        if (videoId.empty()) {
            spdlog::warn("[YouTubeApi] search.list returned item without videoId.");
            return {};
        }
        spdlog::info("[YouTubeApi] Found live video: {}", videoId);
    } catch (const std::exception& e) {
        spdlog::warn("[YouTubeApi] Failed to parse search.list response: {}", e.what());
        return {};
    }

    // Step 2: Get liveStreamingDetails for the video to extract activeLiveChatId
    // Costs 1 quota unit (videos.list)
    if (!YouTubeQuota::instance().consume(YouTubeQuota::COST_LIST)) {
        spdlog::warn("[YouTubeApi] findBroadcastByChannel step 2: quota budget exhausted.");
        return {};
    }
    std::string videoUrl =
        "https://www.googleapis.com/youtube/v3/videos"
        "?id=" + urlEncode(videoId) +
        "&part=liveStreamingDetails,snippet"
        "&key=" + urlEncode(apiKey);

    std::string videoResp = curlRequest("GET", videoUrl, "" /*no auth header*/);
    if (videoResp.empty()) {
        spdlog::warn("[YouTubeApi] findBroadcastByChannel: empty response from videos.list");
        return {};
    }

    try {
        auto j = nlohmann::json::parse(videoResp);

        if (j.contains("error")) {
            auto msg = j["error"].value("message", "unknown error");
            int code = j["error"].value("code", 0);
            spdlog::warn("[YouTubeApi] videos.list error ({}): {}", code, msg);
            return {};
        }

        if (!j.contains("items") || !j["items"].is_array() || j["items"].empty()) {
            spdlog::warn("[YouTubeApi] videos.list returned no items for video {}.", videoId);
            return {};
        }

        auto& item = j["items"][0];
        std::string chatId;
        if (item.contains("liveStreamingDetails")) {
            chatId = item["liveStreamingDetails"].value("activeLiveChatId", "");
        }

        if (chatId.empty()) {
            spdlog::warn("[YouTubeApi] Video {} has no activeLiveChatId.", videoId);
            return {};
        }

        BroadcastInfo info;
        info.liveChatId      = chatId;
        info.broadcastId     = videoId;
        info.title           = item.contains("snippet") ? item["snippet"].value("title", "(unknown)") : "(unknown)";
        info.lifeCycleStatus = "live";

        spdlog::info("[YouTubeApi] Found broadcast '{}' via channel search with liveChatId: {}",
                     info.title, info.liveChatId);
        return info;
    } catch (const std::exception& e) {
        spdlog::warn("[YouTubeApi] Failed to parse videos.list response: {}", e.what());
    }
    return {};
}

std::string YouTubeApi::getActiveBroadcastId(const std::string& oauthToken) {
    // Costs 1 quota unit (liveBroadcasts.list)
    if (!YouTubeQuota::instance().consume(YouTubeQuota::COST_LIST)) {
        spdlog::warn("[YouTubeApi] getActiveBroadcastId: quota budget exhausted.");
        return "";
    }

    // GET liveBroadcasts.list with broadcastStatus=active to find the
    // currently live broadcast.
    std::string url = "https://www.googleapis.com/youtube/v3/liveBroadcasts"
                      "?broadcastStatus=active"
                      "&broadcastType=all"
                      "&part=id,snippet"
                      "&maxResults=1";

    std::string resp = curlRequest("GET", url, oauthToken);
    if (resp.empty()) return "";

    try {
        auto j = nlohmann::json::parse(resp);

        // Check for API error
        if (j.contains("error")) {
            auto msg = j["error"].value("message", "unknown error");
            spdlog::error("[YouTubeApi] getActiveBroadcastId error: {}", msg);
            return "";
        }

        if (j.contains("items") && j["items"].is_array() && !j["items"].empty()) {
            std::string broadcastId = j["items"][0].value("id", "");
            spdlog::info("[YouTubeApi] Found active broadcast: {}", broadcastId);
            return broadcastId;
        }

        spdlog::warn("[YouTubeApi] No active broadcast found.");
    } catch (const std::exception& e) {
        spdlog::error("[YouTubeApi] Failed to parse getActiveBroadcastId response: {}", e.what());
    }
    return "";
}

bool YouTubeApi::updateBroadcast(const std::string& oauthToken,
                                 const std::string& broadcastId,
                                 const std::string& title,
                                 const std::string& description,
                                 const std::string& categoryId)
{
    if (broadcastId.empty()) {
        spdlog::error("[YouTubeApi] Cannot update broadcast: broadcast ID is empty.");
        return false;
    }

    if (title.empty() && description.empty() && categoryId.empty()) {
        spdlog::debug("[YouTubeApi] Nothing to update.");
        return true;
    }

    // Costs: 1 (list to read current) + 50 (update) = 51 quota units
    if (!YouTubeQuota::instance().consume(YouTubeQuota::COST_LIST + YouTubeQuota::COST_UPDATE)) {
        spdlog::warn("[YouTubeApi] updateBroadcast: quota budget exhausted.");
        return false;
    }

    // Step 1: Fetch the current broadcast snippet so we can preserve fields
    // that we don't want to change (YouTube requires the full snippet on update).
    std::string getUrl = "https://www.googleapis.com/youtube/v3/liveBroadcasts"
                         "?id=" + urlEncode(broadcastId) +
                         "&part=snippet";

    std::string getResp = curlRequest("GET", getUrl, oauthToken);
    if (getResp.empty()) {
        spdlog::error("[YouTubeApi] Failed to fetch current broadcast details.");
        return false;
    }

    nlohmann::json currentSnippet;
    try {
        auto j = nlohmann::json::parse(getResp);
        if (j.contains("error")) {
            spdlog::error("[YouTubeApi] Fetch broadcast error: {}",
                          j["error"].value("message", "unknown"));
            return false;
        }
        if (!j.contains("items") || j["items"].empty()) {
            spdlog::error("[YouTubeApi] Broadcast '{}' not found.", broadcastId);
            return false;
        }
        currentSnippet = j["items"][0]["snippet"];
    } catch (const std::exception& e) {
        spdlog::error("[YouTubeApi] Failed to parse broadcast details: {}", e.what());
        return false;
    }

    // Step 2: Build the update payload, merging our changes with existing values.
    nlohmann::json snippet = currentSnippet;
    if (!title.empty())       snippet["title"]       = title;
    if (!description.empty()) snippet["description"] = description;
    if (!categoryId.empty())  snippet["categoryId"]  = categoryId;

    nlohmann::json body;
    body["id"]      = broadcastId;
    body["snippet"] = snippet;

    std::string updateUrl = "https://www.googleapis.com/youtube/v3/liveBroadcasts"
                            "?part=snippet";

    std::string resp = curlRequest("PUT", updateUrl, oauthToken, body.dump());

    if (resp.empty()) {
        spdlog::error("[YouTubeApi] Empty response from updateBroadcast.");
        return false;
    }

    try {
        auto j = nlohmann::json::parse(resp);
        if (j.contains("error")) {
            auto msg = j["error"].value("message", "unknown error");
            spdlog::error("[YouTubeApi] updateBroadcast error: {}", msg);
            return false;
        }
        // Success — the response contains the updated broadcast resource
        spdlog::info("[YouTubeApi] Broadcast '{}' updated successfully (title='{}').",
                     broadcastId, snippet.value("title", ""));
        return true;
    } catch (const std::exception& e) {
        spdlog::error("[YouTubeApi] Failed to parse updateBroadcast response: {}", e.what());
    }

    return false;
}

// ── OAuth 2.0 helpers ────────────────────────────────────────────────────

std::string YouTubeApi::curlPostForm(const std::string& url,
                                     const std::string& formBody)
{
    // Write the form body to a temp file to avoid shell escaping issues
    // (same strategy as curlRequest for JSON bodies).
    std::string tempBodyPath;
    try {
        auto tmp = std::filesystem::temp_directory_path() / "is_youtube_form.txt";
        tempBodyPath = tmp.string();
        std::ofstream ofs(tempBodyPath, std::ios::trunc);
        ofs << formBody;
        ofs.close();
    } catch (const std::exception& e) {
        spdlog::error("[YouTubeApi] Failed to write temp form file: {}", e.what());
        return "";
    }

    // Also capture stderr to a temp file for diagnostic logging
    std::string tempStderrPath;
    try {
        auto tmp = std::filesystem::temp_directory_path() / "is_youtube_curl_err.txt";
        tempStderrPath = tmp.string();
    } catch (...) {
        // Non-fatal; just won't capture stderr
    }

    std::ostringstream cmd;
    cmd << "curl -sS -X POST"
        << " \"" << shellEscape(url) << "\""
        << " -H \"Content-Type: application/x-www-form-urlencoded\""
        << " -d @\"" << shellEscape(tempBodyPath) << "\"";

    if (!tempStderrPath.empty()) {
        cmd << " 2>\"" << shellEscape(tempStderrPath) << "\"";
    } else {
#ifdef _WIN32
        cmd << " 2>nul";
#else
        cmd << " 2>/dev/null";
#endif
    }

    std::string cmdStr = cmd.str();
    spdlog::debug("[YouTubeApi] Running: {}", cmdStr);

    FILE* pipe = popen(cmdStr.c_str(), "r");
    if (!pipe) {
        spdlog::error("[YouTubeApi] Failed to run curl.");
        std::filesystem::remove(tempBodyPath);
        return "";
    }

    std::string result;
    std::array<char, 4096> buffer{};
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
        result += buffer.data();
    }
    int rc = pclose(pipe);

    // Log stderr if curl failed or returned empty
    if ((rc != 0 || result.empty()) && !tempStderrPath.empty()) {
        try {
            std::ifstream ifs(tempStderrPath);
            std::string errStr((std::istreambuf_iterator<char>(ifs)),
                                std::istreambuf_iterator<char>());
            if (!errStr.empty()) {
                spdlog::error("[YouTubeApi] curl stderr (rc={}): {}", rc, errStr);
            }
        } catch (...) {}
        std::filesystem::remove(tempStderrPath);
    } else if (!tempStderrPath.empty()) {
        std::filesystem::remove(tempStderrPath);
    }

    // Clean up temp body file
    std::filesystem::remove(tempBodyPath);

    if (result.empty()) {
        spdlog::error("[YouTubeApi] curlPostForm got empty response (rc={})", rc);
    }

    return result;
}

YouTubeApi::TokenResult YouTubeApi::exchangeAuthCode(
    const std::string& code,
    const std::string& clientId,
    const std::string& clientSecret,
    const std::string& redirectUri)
{
    TokenResult tr;

    std::string body =
        "code="          + urlEncode(code) +
        "&client_id="    + urlEncode(clientId) +
        "&client_secret="+ urlEncode(clientSecret) +
        "&redirect_uri=" + urlEncode(redirectUri) +
        "&grant_type=authorization_code";

    std::string resp = curlPostForm("https://oauth2.googleapis.com/token", body);
    if (resp.empty()) {
        tr.error = "Empty response from Google token endpoint";
        spdlog::error("[YouTubeApi] {}", tr.error);
        return tr;
    }

    try {
        auto j = nlohmann::json::parse(resp);
        if (j.contains("error")) {
            tr.error = j.value("error_description", j.value("error", "unknown"));
            spdlog::error("[YouTubeApi] Token exchange failed: {}", tr.error);
            return tr;
        }
        tr.accessToken  = j.value("access_token", "");
        tr.refreshToken = j.value("refresh_token", "");
        tr.expiresIn    = j.value("expires_in", 0);
        tr.success      = !tr.accessToken.empty();

        if (tr.success) {
            spdlog::info("[YouTubeApi] Token exchange successful (expires_in={}s, has_refresh={})",
                         tr.expiresIn, !tr.refreshToken.empty());
        }
    } catch (const std::exception& e) {
        tr.error = std::string("JSON parse error: ") + e.what();
        spdlog::error("[YouTubeApi] {}", tr.error);
    }
    return tr;
}

YouTubeApi::TokenResult YouTubeApi::refreshAccessToken(
    const std::string& refreshToken,
    const std::string& clientId,
    const std::string& clientSecret)
{
    TokenResult tr;

    std::string body =
        "refresh_token=" + urlEncode(refreshToken) +
        "&client_id="    + urlEncode(clientId) +
        "&client_secret="+ urlEncode(clientSecret) +
        "&grant_type=refresh_token";

    std::string resp = curlPostForm("https://oauth2.googleapis.com/token", body);
    if (resp.empty()) {
        tr.error = "Empty response from Google token endpoint";
        spdlog::error("[YouTubeApi] {}", tr.error);
        return tr;
    }

    try {
        auto j = nlohmann::json::parse(resp);
        if (j.contains("error")) {
            tr.error = j.value("error_description", j.value("error", "unknown"));
            spdlog::error("[YouTubeApi] Token refresh failed: {}", tr.error);
            return tr;
        }
        tr.accessToken = j.value("access_token", "");
        tr.expiresIn   = j.value("expires_in", 0);
        tr.success     = !tr.accessToken.empty();

        if (tr.success) {
            spdlog::info("[YouTubeApi] Token refresh successful (expires_in={}s)", tr.expiresIn);
        }
    } catch (const std::exception& e) {
        tr.error = std::string("JSON parse error: ") + e.what();
        spdlog::error("[YouTubeApi] {}", tr.error);
    }
    return tr;
}

} // namespace is::platform
