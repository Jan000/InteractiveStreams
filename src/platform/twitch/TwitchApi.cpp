#include "platform/twitch/TwitchApi.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <sstream>
#include <array>
#include <cstdio>

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

std::string TwitchApi::curlRequest(const std::string& method,
                                   const std::string& url,
                                   const std::string& token,
                                   const std::string& clientId,
                                   const std::string& body)
{
    std::ostringstream cmd;
    cmd << "curl -s -X " << method
        << " \"" << shellEscape(url) << "\""
        << " -H \"Authorization: Bearer " << shellEscape(token) << "\""
        << " -H \"Client-Id: " << shellEscape(clientId) << "\"";

    if (!body.empty()) {
        cmd << " -H \"Content-Type: application/json\""
            << " -d \"" << shellEscape(body) << "\"";
    }

    // Redirect stderr to suppress curl progress output
#ifdef _WIN32
    cmd << " 2>nul";
#else
    cmd << " 2>/dev/null";
#endif

    std::string cmdStr = cmd.str();
    spdlog::debug("[TwitchApi] Running: {}", cmdStr);

    FILE* pipe = popen(cmdStr.c_str(), "r");
    if (!pipe) {
        spdlog::error("[TwitchApi] Failed to run curl.");
        return "";
    }

    std::string result;
    std::array<char, 4096> buffer{};
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
        result += buffer.data();
    }
    int rc = pclose(pipe);
    if (rc != 0) {
        spdlog::warn("[TwitchApi] curl exited with code {}", rc);
    }

    spdlog::debug("[TwitchApi] Response: {}", result);
    return result;
}

// ── Public API ───────────────────────────────────────────────────────────────

bool TwitchApi::isCurlAvailable() {
#ifdef _WIN32
    int rc = std::system("curl --version >nul 2>nul");
#else
    int rc = std::system("curl --version >/dev/null 2>/dev/null");
#endif
    return rc == 0;
}

std::string TwitchApi::getBroadcasterId(const std::string& token,
                                        const std::string& clientId)
{
    std::string resp = curlRequest("GET",
        "https://api.twitch.tv/helix/users", token, clientId);
    if (resp.empty()) return "";

    try {
        auto j = nlohmann::json::parse(resp);
        if (j.contains("data") && j["data"].is_array() && !j["data"].empty()) {
            return j["data"][0].value("id", "");
        }
        if (j.contains("message")) {
            spdlog::error("[TwitchApi] getBroadcasterId error: {}", j["message"].get<std::string>());
        }
    } catch (const std::exception& e) {
        spdlog::error("[TwitchApi] Failed to parse getBroadcasterId response: {}", e.what());
    }
    return "";
}

std::string TwitchApi::getGameId(const std::string& token,
                                 const std::string& clientId,
                                 const std::string& categoryName)
{
    std::string url = "https://api.twitch.tv/helix/games?name=" + urlEncode(categoryName);
    std::string resp = curlRequest("GET", url, token, clientId);
    if (resp.empty()) return "";

    try {
        auto j = nlohmann::json::parse(resp);
        if (j.contains("data") && j["data"].is_array() && !j["data"].empty()) {
            return j["data"][0].value("id", "");
        }
        spdlog::warn("[TwitchApi] Category '{}' not found on Twitch.", categoryName);
    } catch (const std::exception& e) {
        spdlog::error("[TwitchApi] Failed to parse getGameId response: {}", e.what());
    }
    return "";
}

bool TwitchApi::updateChannelInfo(const std::string& token,
                                  const std::string& clientId,
                                  const std::string& broadcasterId,
                                  const std::string& title,
                                  const std::string& gameId)
{
    if (broadcasterId.empty()) {
        spdlog::error("[TwitchApi] Cannot update channel: broadcaster ID is empty.");
        return false;
    }

    std::string url = "https://api.twitch.tv/helix/channels?broadcaster_id=" + broadcasterId;

    nlohmann::json body = nlohmann::json::object();
    if (!title.empty())  body["title"]   = title;
    if (!gameId.empty()) body["game_id"] = gameId;

    if (body.empty()) {
        spdlog::debug("[TwitchApi] Nothing to update.");
        return true;
    }

    std::string resp = curlRequest("PATCH", url, token, clientId, body.dump());

    // Twitch returns 204 No Content on success (empty body)
    if (resp.empty()) {
        spdlog::info("[TwitchApi] Channel updated successfully (title={}, gameId={}).",
                     title, gameId);
        return true;
    }

    // Check for error in response
    try {
        auto j = nlohmann::json::parse(resp);
        if (j.contains("error") || j.contains("message")) {
            spdlog::error("[TwitchApi] updateChannelInfo error: {}",
                          j.value("message", j.value("error", "unknown")));
            return false;
        }
    } catch (...) {
        // Non-JSON response is fine (204 success returns empty)
    }

    return true;
}

} // namespace is::platform
