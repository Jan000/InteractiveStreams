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
static std::string urlEncode(const std::string& s) {
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
    cmd << "curl -s -X " << method
        << " \"" << shellEscape(url) << "\""
        << " -H \"Authorization: Bearer " << shellEscape(oauthToken) << "\"";

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

} // namespace is::platform
