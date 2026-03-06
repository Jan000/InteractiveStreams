#include "platform/youtube/YouTubeApi.h"

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
    if (oauthToken.empty()) return "";

    // Use liveBroadcasts.list with mine=true to find the active broadcast.
    // This requires OAuth but directly returns snippet.liveChatId — a single
    // API call instead of search.list + videos.list, and avoids the search
    // quota / permission issues.
    std::string url = "https://www.googleapis.com/youtube/v3/liveBroadcasts"
                      "?mine=true"
                      "&broadcastStatus=active"
                      "&broadcastType=all"
                      "&part=snippet"
                      "&maxResults=1";

    std::string resp = curlRequest("GET", url, oauthToken);
    if (resp.empty()) {
        spdlog::warn("[YouTubeApi] findLiveChatId: empty response from liveBroadcasts.list");
        return "";
    }

    try {
        auto j = nlohmann::json::parse(resp);

        if (j.contains("error")) {
            auto msg = j["error"].value("message", "unknown error");
            int code = j["error"].value("code", 0);
            spdlog::warn("[YouTubeApi] liveBroadcasts.list error ({}): {}", code, msg);
            return "";
        }

        if (!j.contains("items") || !j["items"].is_array() || j["items"].empty()) {
            spdlog::debug("[YouTubeApi] No active broadcast found.");
            return "";
        }

        auto& snippet = j["items"][0]["snippet"];
        std::string liveChatId = snippet.value("liveChatId", "");
        if (liveChatId.empty()) {
            spdlog::warn("[YouTubeApi] Active broadcast has no liveChatId.");
            return "";
        }

        std::string title = snippet.value("title", "(unknown)");
        spdlog::info("[YouTubeApi] Found active broadcast '{}' with liveChatId: {}",
                     title, liveChatId);
        return liveChatId;
    } catch (const std::exception& e) {
        spdlog::warn("[YouTubeApi] Failed to parse liveBroadcasts.list response: {}", e.what());
    }
    return "";
}

std::string YouTubeApi::getActiveBroadcastId(const std::string& oauthToken) {
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
