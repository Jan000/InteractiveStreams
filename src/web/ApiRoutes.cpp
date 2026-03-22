#include "web/ApiRoutes.h"
#include "web/WebServer.h"
#include "core/Application.h"
#include "core/Config.h"
#include "core/ChannelManager.h"
#include "core/StreamManager.h"
#include "core/StreamInstance.h"
#include "core/StreamProfile.h"
#include "core/PlayerDatabase.h"
#include "core/PerfMonitor.h"
#include "core/SettingsDatabase.h"
#include "core/AudioManager.h"
#include "core/AudioMixer.h"
#include "core/Sha256.h"
#include "games/GameRegistry.h"
#include "platform/twitch/TwitchApi.h"
#include "platform/youtube/YouTubeApi.h"
#include "platform/youtube/YoutubePlatform.h"
#include "platform/youtube/YouTubeQuota.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <unordered_set>

namespace is::web {

// Helper: extract first regex capture from path
static std::string pathParam(const httplib::Request& req, size_t idx = 1) {
    if (idx < req.matches.size()) return std::string(req.matches[idx]);
    return "";
}

static std::string normalizeGitHash(std::string value) {
    value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    }), value.end());

    if (value.empty() || value == "unknown") {
        return "unknown";
    }

    if (value.size() > 8) {
        value = value.substr(0, 8);
    }
    return value;
}

static std::string resolveGitHash() {
#ifdef IS_GIT_HASH
    auto compileHash = normalizeGitHash(IS_GIT_HASH);
    if (compileHash != "unknown") {
        return compileHash;
    }
#endif

    constexpr const char* ENV_NAMES[] = {
        "IS_GIT_HASH",
        "GIT_COMMIT_HASH",
        "SOURCE_COMMIT",
        "COOLIFY_GIT_COMMIT_SHA",
        "COMMIT_SHA",
        "GITHUB_SHA"
    };

    for (const char* envName : ENV_NAMES) {
        if (const char* value = std::getenv(envName)) {
            auto resolved = normalizeGitHash(value);
            if (resolved != "unknown") {
                return resolved;
            }
        }
    }

    return "unknown";
}

void ApiRoutes::setup(httplib::Server& server, core::Application& app) {

    // ══════════════════════════════════════════════════════════════════════
    //  System
    // ══════════════════════════════════════════════════════════════════════

    server.Get("/api/status", [&app](const httplib::Request&, httplib::Response& res) {
        nlohmann::json s;
        const auto gitHash = resolveGitHash();
        s["version"]  = gitHash != "unknown" ? std::string("0.2.0+") + gitHash : "0.2.0";
        s["gitHash"]  = gitHash;
        s["streams"]  = app.streamManager().getStatus();
        s["channels"] = app.channelManager().getStatus();

        // Available games
        nlohmann::json gamesArr = nlohmann::json::array();
        for (const auto& name : games::GameRegistry::instance().list()) {
            auto tmp = games::GameRegistry::instance().create(name);
            if (tmp) {
                gamesArr.push_back({
                    {"id",          tmp->id()},
                    {"name",        tmp->displayName()},
                    {"description", tmp->description()}
                });
            }
        }
        s["games"] = gamesArr;

        res.set_content(s.dump(2), "application/json");
    });

    server.Post("/api/shutdown", [&app](const httplib::Request&, httplib::Response& res) {
        spdlog::warn("[API] Shutdown requested via web dashboard.");
        app.requestShutdown();
        res.set_content(R"({"success":true,"message":"Shutting down..."})", "application/json");
    });

    // ══════════════════════════════════════════════════════════════════════
    //  Auth  (password login, session tokens)
    // ══════════════════════════════════════════════════════════════════════

    // GET /api/auth/status – check if password is set and if user is authenticated
    server.Get("/api/auth/status", [&app](const httplib::Request& req, httplib::Response& res) {
        bool passwordSet = app.settingsDb().has("auth_password");

        // Check if current request has a valid session
        bool authenticated = false;
        auto it = req.headers.find("Authorization");
        if (it != req.headers.end()) {
            const auto& val = it->second;
            if (val.size() > 7 && val.substr(0, 7) == "Bearer ") {
                std::string token = val.substr(7);
                authenticated = app.webServer().hasSession(token);
                // Also accept API key
                if (!authenticated) {
                    std::string apiKey = app.config().get<std::string>("web.api_key", "");
                    if (!apiKey.empty() && token == apiKey) authenticated = true;
                }
            }
        }
        // If no auth is configured at all, consider authenticated
        if (!passwordSet && app.config().get<std::string>("web.api_key", "").empty()) {
            authenticated = true;
        }

        nlohmann::json s;
        s["passwordSet"]   = passwordSet;
        s["authenticated"] = authenticated;
        res.set_content(s.dump(), "application/json");
    });

    // POST /api/auth/setup – set password for first time (only if no password set yet)
    server.Post("/api/auth/setup", [&app](const httplib::Request& req, httplib::Response& res) {
        if (app.settingsDb().has("auth_password")) {
            res.status = 400;
            res.set_content(R"({"error":"Password already set. Use the console to reset."})",
                            "application/json");
            return;
        }
        try {
            auto body = nlohmann::json::parse(req.body);
            std::string password = body.value("password", "");
            if (password.size() < 4) {
                res.status = 400;
                res.set_content(R"({"error":"Password must be at least 4 characters."})",
                                "application/json");
                return;
            }
            std::string salt = core::generateSalt();
            std::string hash = core::hashPassword(password, salt);
            nlohmann::json authData;
            authData["salt"] = salt;
            authData["hash"] = hash;
            app.settingsDb().save("auth_password", authData);

            // Issue session token
            std::string token = core::generateToken();
            app.webServer().addSession(token);

            spdlog::info("[Auth] Password set up successfully.");
            nlohmann::json r;
            r["success"] = true;
            r["token"]   = token;
            res.set_content(r.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // POST /api/auth/login – login with password, returns session token
    server.Post("/api/auth/login", [&app](const httplib::Request& req, httplib::Response& res) {
        if (!app.settingsDb().has("auth_password")) {
            res.status = 400;
            res.set_content(R"({"error":"No password configured. Use /api/auth/setup first."})",
                            "application/json");
            return;
        }
        try {
            auto body = nlohmann::json::parse(req.body);
            std::string password = body.value("password", "");

            auto authData = app.settingsDb().load("auth_password");
            std::string salt = authData.value("salt", "");
            std::string storedHash = authData.value("hash", "");

            std::string hash = core::hashPassword(password, salt);
            if (hash != storedHash) {
                spdlog::warn("[Auth] Failed login attempt.");
                res.status = 401;
                res.set_content(R"({"error":"Invalid password."})", "application/json");
                return;
            }

            std::string token = core::generateToken();
            app.webServer().addSession(token);
            spdlog::info("[Auth] Successful login.");

            nlohmann::json r;
            r["success"] = true;
            r["token"]   = token;
            res.set_content(r.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // POST /api/auth/logout – invalidate current session token
    server.Post("/api/auth/logout", [&app](const httplib::Request& req, httplib::Response& res) {
        auto it = req.headers.find("Authorization");
        if (it != req.headers.end()) {
            const auto& val = it->second;
            if (val.size() > 7 && val.substr(0, 7) == "Bearer ") {
                app.webServer().removeSession(val.substr(7));
            }
        }
        res.set_content(R"({"success":true})", "application/json");
    });

    // POST /api/auth/change-password – change the password (requires current session)
    server.Post("/api/auth/change-password", [&app](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = nlohmann::json::parse(req.body);
            std::string oldPassword = body.value("oldPassword", "");
            std::string newPassword = body.value("newPassword", "");

            if (newPassword.size() < 4) {
                res.status = 400;
                res.set_content(R"({"error":"Password must be at least 4 characters."})",
                                "application/json");
                return;
            }

            // Verify old password
            if (app.settingsDb().has("auth_password")) {
                auto authData = app.settingsDb().load("auth_password");
                std::string salt = authData.value("salt", "");
                std::string storedHash = authData.value("hash", "");
                if (core::hashPassword(oldPassword, salt) != storedHash) {
                    res.status = 401;
                    res.set_content(R"({"error":"Current password is incorrect."})",
                                    "application/json");
                    return;
                }
            }

            // Set new password
            std::string salt = core::generateSalt();
            std::string hash = core::hashPassword(newPassword, salt);
            nlohmann::json authData;
            authData["salt"] = salt;
            authData["hash"] = hash;
            app.settingsDb().save("auth_password", authData);
            spdlog::info("[Auth] Password changed successfully.");
            res.set_content(R"({"success":true})", "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // ══════════════════════════════════════════════════════════════════════
    //  Channels  (CRUD + connect/disconnect)
    // ══════════════════════════════════════════════════════════════════════

    server.Get("/api/channels", [&app](const httplib::Request&, httplib::Response& res) {
        res.set_content(app.channelManager().getStatus().dump(2), "application/json");
    });

    server.Post("/api/channels", [&app](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = nlohmann::json::parse(req.body);
            core::ChannelConfig cfg;
            cfg.id       = body.value("id", "");
            cfg.platform = body.value("platform", "");
            cfg.name     = body.value("name", cfg.platform);
            cfg.enabled  = body.value("enabled", false);
            if (body.contains("settings")) cfg.settings = body["settings"];

            std::string id = app.channelManager().addChannel(cfg);
            app.persistChannels();
            res.set_content(nlohmann::json({{"success",true},{"id",id}}).dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json({{"error",e.what()}}).dump(), "application/json");
        }
    });

    server.Put(R"(/api/channels/([^/]+))", [&app](const httplib::Request& req, httplib::Response& res) {
        try {
            std::string id = pathParam(req);
            auto body = nlohmann::json::parse(req.body);
            core::ChannelConfig cfg;
            cfg.platform = body.value("platform", "");
            cfg.name     = body.value("name", "");
            cfg.enabled  = body.value("enabled", false);
            if (body.contains("settings")) cfg.settings = body["settings"];

            // Preserve redacted secrets – the GET response replaces them
            // with "***"; don't let that overwrite real values.
            const auto* existing = app.channelManager().getChannelConfig(id);
            if (existing && cfg.settings.is_object() && existing->settings.is_object()) {
                for (const char* key : {"oauth_token", "api_key", "stream_key",
                                          "oauth_client_secret", "oauth_refresh_token"}) {
                    if (cfg.settings.contains(key)
                        && cfg.settings[key].is_string()
                        && cfg.settings[key].get<std::string>() == "***"
                        && existing->settings.contains(key)) {
                        cfg.settings[key] = existing->settings[key];
                    }
                }
            }

            app.channelManager().updateChannel(id, cfg);
            app.persistChannels();
            res.set_content(R"({"success":true})", "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json({{"error",e.what()}}).dump(), "application/json");
        }
    });

    server.Delete(R"(/api/channels/([^/]+))", [&app](const httplib::Request& req, httplib::Response& res) {
        app.channelManager().removeChannel(pathParam(req));
        app.persistChannels();
        res.set_content(R"({"success":true})", "application/json");
    });

    server.Post(R"(/api/channels/([^/]+)/connect)", [&app](const httplib::Request& req, httplib::Response& res) {
        std::string chId = pathParam(req);
        app.channelManager().connectChannel(chId);
        // If any stream subscribes to this channel, refresh Twitch/YouTube info
        for (auto* si : app.streamManager().allStreams()) {
            const auto& ids = si->config().channelIds;
            if (std::find(ids.begin(), ids.end(), chId) != ids.end()) {
                si->triggerPlatformInfoUpdate();
            }
        }
        res.set_content(R"({"success":true})", "application/json");
    });

    server.Post(R"(/api/channels/([^/]+)/disconnect)", [&app](const httplib::Request& req, httplib::Response& res) {
        app.channelManager().disconnectChannel(pathParam(req));
        res.set_content(R"({"success":true})", "application/json");
    });

    // ══════════════════════════════════════════════════════════════════════
    //  Streams  (CRUD + start/stop + game switch)
    // ══════════════════════════════════════════════════════════════════════

    server.Get("/api/streams", [&app](const httplib::Request&, httplib::Response& res) {
        res.set_content(app.streamManager().getStatus().dump(2), "application/json");
    });

    server.Post("/api/streams", [&app](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = nlohmann::json::parse(req.body);
            // Resolve profile inheritance before parsing config
            auto resolved = app.profileManager().resolveStreamConfig(body);
            auto cfg  = core::StreamInstance::configFromJson(resolved);
            std::string id = app.streamManager().addStream(cfg);
            app.persistStreams();
            res.set_content(nlohmann::json({{"success",true},{"id",id}}).dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json({{"error",e.what()}}).dump(), "application/json");
        }
    });

    server.Put(R"(/api/streams/([^/]+))", [&app](const httplib::Request& req, httplib::Response& res) {
        try {
            std::string id = pathParam(req);
            auto body = nlohmann::json::parse(req.body);
            // Resolve profile inheritance before parsing config
            auto resolved = app.profileManager().resolveStreamConfig(body);
            auto cfg  = core::StreamInstance::configFromJson(resolved);
            cfg.id = id;  // preserve the path-param ID
            // Preserve the profile_id from the raw body (not from resolved)
            cfg.profileId = body.value("profile_id", "");
            app.streamManager().updateStream(id, cfg);
            app.persistStreams();

            // Push updated Twitch/YouTube titles/categories immediately
            if (auto* si = app.streamManager().getStream(id)) {
                si->triggerPlatformInfoUpdate();
            }

            res.set_content(R"({"success":true})", "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json({{"error",e.what()}}).dump(), "application/json");
        }
    });

    server.Delete(R"(/api/streams/([^/]+))", [&app](const httplib::Request& req, httplib::Response& res) {
        app.streamManager().removeStream(pathParam(req));
        app.persistStreams();
        res.set_content(R"({"success":true})", "application/json");
    });

    // Start / stop streaming (FFmpeg encoding)
    server.Post(R"(/api/streams/([^/]+)/start)", [&app](const httplib::Request& req, httplib::Response& res) {
        std::string id = pathParam(req);
        auto* stream = app.streamManager().getStream(id);
        if (!stream) {
            res.status = 404;
            res.set_content(R"({"error":"Stream not found"})", "application/json");
            return;
        }
        std::string err = app.streamManager().startStreaming(id);
        if (!err.empty()) {
            res.status = 400;
            res.set_content(nlohmann::json({{"error", "Cannot start streaming: " + err}}).dump(), "application/json");
            return;
        }
        res.set_content(R"({"success":true})", "application/json");
    });

    server.Post(R"(/api/streams/([^/]+)/stop)", [&app](const httplib::Request& req, httplib::Response& res) {
        app.streamManager().stopStreaming(pathParam(req));
        res.set_content(R"({"success":true})", "application/json");
    });

    // Manually trigger platform info update (Twitch/YouTube title/category)
    server.Post(R"(/api/streams/([^/]+)/update-metadata)", [&app](const httplib::Request& req, httplib::Response& res) {
        std::string id = pathParam(req);
        auto* stream = app.streamManager().getStream(id);
        if (!stream) {
            res.status = 404;
            res.set_content(R"({"error":"Stream not found"})", "application/json");
            return;
        }
        stream->triggerPlatformInfoUpdate();
        res.set_content(R"({"success":true})", "application/json");
    });

    // Switch the game running on a specific stream
    server.Post(R"(/api/streams/([^/]+)/game)", [&app](const httplib::Request& req, httplib::Response& res) {
        try {
            std::string id = pathParam(req);
            auto body = nlohmann::json::parse(req.body);
            std::string gameName = body.at("game");
            std::string modeStr  = body.value("mode", "immediate");

            core::SwitchMode mode = core::SwitchMode::Immediate;
            if (modeStr == "after_round")     mode = core::SwitchMode::AfterRound;
            else if (modeStr == "after_game") mode = core::SwitchMode::AfterGame;

            auto* stream = app.streamManager().getStream(id);
            if (!stream) {
                res.status = 404;
                res.set_content(R"({"error":"Stream not found"})", "application/json");
                return;
            }
            stream->gameManager().requestSwitch(gameName, mode);
            res.set_content(R"({"success":true})", "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json({{"error",e.what()}}).dump(), "application/json");
        }
    });

    server.Post(R"(/api/streams/([^/]+)/cancel-switch)", [&app](const httplib::Request& req, httplib::Response& res) {
        auto* stream = app.streamManager().getStream(pathParam(req));
        if (!stream) {
            res.status = 404;
            res.set_content(R"({"error":"Stream not found"})", "application/json");
            return;
        }
        stream->gameManager().cancelPendingSwitch();
        res.set_content(R"({"success":true})", "application/json");
    });

    // JPEG frame snapshot for web live-preview
    server.Get(R"(/api/streams/([^/]+)/frame)", [&app](const httplib::Request& req, httplib::Response& res) {
        auto* stream = app.streamManager().getStream(pathParam(req));
        if (!stream) {
            res.status = 404;
            res.set_content(R"({"error":"Stream not found"})", "application/json");
            return;
        }
        auto jpeg = stream->getJpegFrame();
        if (jpeg.empty()) {
            res.status = 503;
            res.set_content(R"({"error":"No frame available"})", "application/json");
            return;
        }
        res.set_header("Cache-Control", "no-cache, no-store, must-revalidate");
        res.set_header("Pragma", "no-cache");
        res.set_content(std::string(jpeg.begin(), jpeg.end()), "image/jpeg");
    });

    // ══════════════════════════════════════════════════════════════════════
    //  Profiles (config inheritance / templates)
    // ══════════════════════════════════════════════════════════════════════

    server.Get("/api/profiles", [&app](const httplib::Request&, httplib::Response& res) {
        res.set_content(app.profileManager().toJson().dump(2), "application/json");
    });

    server.Post("/api/profiles", [&app](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = nlohmann::json::parse(req.body);
            core::StreamProfile p;
            p.id       = body.value("id", "");
            p.name     = body.value("name", "Profile");
            p.parentId = body.value("parent_id", "");
            p.config   = body.value("config", nlohmann::json::object());

            std::string id = app.profileManager().addProfile(p);
            app.persistProfiles();
            res.set_content(nlohmann::json({{"success",true},{"id",id}}).dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json({{"error",e.what()}}).dump(), "application/json");
        }
    });

    server.Put(R"(/api/profiles/([^/]+))", [&app](const httplib::Request& req, httplib::Response& res) {
        try {
            std::string id = pathParam(req);
            auto body = nlohmann::json::parse(req.body);

            core::StreamProfile p;
            p.name     = body.value("name", "Profile");
            p.parentId = body.value("parent_id", "");
            p.config   = body.value("config", nlohmann::json::object());

            app.profileManager().updateProfile(id, p);
            app.persistProfiles();

            // Re-resolve all streams referencing this profile
            for (auto* s : app.streamManager().allStreams()) {
                if (s->config().profileId == id) {
                    auto rawJson = s->toJson();
                    auto resolved = app.profileManager().resolveStreamConfig(rawJson);
                    auto newCfg = core::StreamInstance::configFromJson(resolved);
                    newCfg.id = s->config().id; // preserve ID
                    s->updateConfig(newCfg);
                }
            }
            app.persistStreams();

            res.set_content(R"({"success":true})", "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json({{"error",e.what()}}).dump(), "application/json");
        }
    });

    server.Delete(R"(/api/profiles/([^/]+))", [&app](const httplib::Request& req, httplib::Response& res) {
        std::string id = pathParam(req);
        app.profileManager().removeProfile(id);
        app.persistProfiles();

        // Clear profileId from streams that referenced this profile
        for (auto* s : app.streamManager().allStreams()) {
            if (s->config().profileId == id) {
                auto cfg       = s->config();
                cfg.profileId  = "";
                s->updateConfig(cfg);
            }
        }
        app.persistStreams();

        res.set_content(R"({"success":true})", "application/json");
    });

    // Resolve: preview what a profile chain produces
    server.Get(R"(/api/profiles/([^/]+)/resolved)", [&app](const httplib::Request& req, httplib::Response& res) {
        std::string id = pathParam(req);
        auto resolved = app.profileManager().resolveProfileChain(id);
        res.set_content(resolved.dump(2), "application/json");
    });

    // ══════════════════════════════════════════════════════════════════════
    //  Games
    // ══════════════════════════════════════════════════════════════════════

    server.Get("/api/games", [](const httplib::Request&, httplib::Response& res) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& name : games::GameRegistry::instance().list()) {
            auto tmp = games::GameRegistry::instance().create(name);
            if (tmp) {
                arr.push_back({
                    {"id",          tmp->id()},
                    {"name",        tmp->displayName()},
                    {"description", tmp->description()}
                });
            }
        }
        res.set_content(arr.dump(2), "application/json");
    });

    // ── Game Settings ────────────────────────────────────────────────────

    // GET /api/games/text-elements - Get default text elements for all games
    server.Get("/api/games/text-elements", [&app](const httplib::Request&, httplib::Response& res) {
        auto& registry = is::games::GameRegistry::instance();
        nlohmann::json result = nlohmann::json::object();

        // Get stored settings for applying overrides
        nlohmann::json storedSettings;
        auto streams = app.streamManager().allStreams();
        if (!streams.empty()) {
            storedSettings = streams[0]->gameManager().getAllGameSettings();
        }

        for (const auto& gameId : registry.list()) {
            auto game = registry.create(gameId);
            if (game) {
                game->initialize();
                // Apply any stored overrides to reflect current state
                if (storedSettings.contains(gameId)) {
                    game->configure(storedSettings[gameId]);
                }
                result[gameId] = game->textElementsJson();
                game->shutdown();
            }
        }

        res.set_content(result.dump(2), "application/json");
    });

    // GET /api/games/settings - Get all per-game settings
    server.Get("/api/games/settings", [&app](const httplib::Request&, httplib::Response& res) {
        // Collect from all stream instances
        nlohmann::json result = nlohmann::json::object();

        // Use first stream's GameManager as source of truth for game settings
        auto streams = app.streamManager().allStreams();
        if (!streams.empty()) {
            result = streams[0]->gameManager().getAllGameSettings();
        }

        res.set_content(result.dump(2), "application/json");
    });

    // PUT /api/games/settings - Update settings for a specific game
    server.Put("/api/games/settings", [&app](const httplib::Request& req, httplib::Response& res) {
        auto body = nlohmann::json::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.is_object()) {
            res.status = 400;
            res.set_content(R"({"error":"Invalid JSON"})", "application/json");
            return;
        }

        // Apply settings to all stream instances
        auto streams = app.streamManager().allStreams();
        for (auto* stream : streams) {
            for (auto& [gameId, settings] : body.items()) {
                stream->gameManager().setGameSettings(gameId, settings);
            }
        }

        // Persist in global config
        auto& cfg = app.config().rawMut();
        if (!cfg.contains("game_settings") || !cfg["game_settings"].is_object()) {
            cfg["game_settings"] = nlohmann::json::object();
        }
        for (auto& [gameId, settings] : body.items()) {
            cfg["game_settings"][gameId] = settings;
        }
        app.persistGlobalConfig();

        res.set_content(R"({"ok":true})", "application/json");
    });

    // ══════════════════════════════════════════════════════════════════════
    //  Settings  (read / write / save)
    // ══════════════════════════════════════════════════════════════════════

    server.Get("/api/settings", [&app](const httplib::Request&, httplib::Response& res) {
        auto cfg = app.config().raw();  // copy
        // Redact secrets
        if (cfg.contains("channels")) {
            for (auto& ch : cfg["channels"]) {
                if (ch.contains("settings")) {
                    auto& s = ch["settings"];
                    if (s.contains("oauth_token")) s["oauth_token"] = "***";
                    if (s.contains("api_key"))     s["api_key"]     = "***";
                    if (s.contains("stream_key"))  s["stream_key"]  = "***";
                    if (s.contains("oauth_client_secret")) s["oauth_client_secret"] = "***";
                    if (s.contains("oauth_refresh_token")) s["oauth_refresh_token"] = "***";
                }
            }
        }
        if (cfg.contains("streams")) {
            // stream_key is now per-channel, not per-stream
        }
        res.set_content(cfg.dump(2), "application/json");
    });

    server.Put("/api/settings", [&app](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = nlohmann::json::parse(req.body);
            // Merge top-level keys into config
            for (auto it = body.begin(); it != body.end(); ++it) {
                app.config().rawMut()[it.key()] = it.value();
            }
            app.persistGlobalConfig();
            res.set_content(R"({"success":true})", "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json({{"error",e.what()}}).dump(), "application/json");
        }
    });

    server.Post("/api/config/save", [&app](const httplib::Request&, httplib::Response& res) {
        // Persist to SQLite (primary storage)
        app.persistChannels();
        app.persistStreams();
        app.persistGlobalConfig();

        // Also write to JSON file as a human-readable backup
        app.config().rawMut()["channels"] = app.channelManager().toJson();
        app.config().rawMut()["streams"]  = app.streamManager().toJson();
        app.config().save();
        res.set_content(R"({"success":true})", "application/json");
    });

    // ══════════════════════════════════════════════════════════════════════
    //  Config Export / Import
    // ══════════════════════════════════════════════════════════════════════

    // Export: returns a full JSON snapshot of ALL settings (config, channels,
    // streams, profiles, audio).  Secrets (tokens, keys) are included so the
    // export file can be used for a 1:1 restore.
    server.Get("/api/config/export", [&app](const httplib::Request&, httplib::Response& res) {
        nlohmann::json out;
        // Global config (without channels/streams – those come separately)
        out["config"] = app.config().raw();
        // Remove channels/streams from the config copy (exported separately)
        out["config"].erase("channels");
        out["config"].erase("streams");
        out["config"].erase("profiles");
        // Channels, Streams, Profiles as top-level arrays
        out["channels"] = app.channelManager().toJson();
        out["streams"]  = app.streamManager().toJson();
        out["profiles"] = app.profileManager().toJson();
        // Audio settings
        nlohmann::json audio;
        audio["music_volume"]      = app.audioManager().musicVolume();
        audio["sfx_volume"]        = app.audioManager().sfxVolume();
        audio["muted"]             = app.audioManager().isMuted();
        audio["fade_in_seconds"]   = app.audioManager().fadeInDuration();
        audio["fade_out_seconds"]  = app.audioManager().fadeOutDuration();
        audio["crossfade_overlap"] = app.audioManager().crossfadeOverlap();
        out["audio"] = audio;
        // Metadata
        out["_export_version"] = 1;
        out["_exported_at"] = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        res.set_header("Content-Disposition",
                       "attachment; filename=\"interactive_streams_config.json\"");
        res.set_content(out.dump(2), "application/json");
    });

    // Import: accepts a full JSON snapshot and replaces current settings.
    server.Post("/api/config/import", [&app](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = nlohmann::json::parse(req.body);

            // Validate export version
            int version = body.value("_export_version", 0);
            if (version < 1) {
                res.status = 400;
                res.set_content(R"({"error":"Invalid or missing _export_version in import file"})", "application/json");
                return;
            }

            // 1. Import global config
            if (body.contains("config") && body["config"].is_object()) {
                auto& raw = app.config().rawMut();
                auto importCfg = body["config"];
                // Preserve channels/streams/profiles keys (they are handled separately)
                importCfg.erase("channels");
                importCfg.erase("streams");
                importCfg.erase("profiles");
                for (auto it = importCfg.begin(); it != importCfg.end(); ++it) {
                    raw[it.key()] = it.value();
                }
                app.persistGlobalConfig();
            }

            // 2. Import channels
            // ChannelManager::loadFromJson is mutex-protected and safe from web thread.
            if (body.contains("channels") && body["channels"].is_array()) {
                app.channelManager().loadFromJson(body["channels"]);
                app.channelManager().connectAllEnabled();
                app.persistChannels();
            }

            // 3. Import streams – persist to SQLite only.
            // StreamManager::loadFromJson() destroys and recreates StreamInstance
            // objects (which own games, render textures, etc.). Doing this on the
            // web-server thread while the main loop is running causes data races
            // and potential use-after-free crashes.
            // Instead, write the new stream config to SQLite and request a restart.
            bool needsRestart = false;
            if (body.contains("streams") && body["streams"].is_array()) {
                app.settingsDb().save("streams", body["streams"]);
                // Prevent shutdown() from overwriting the imported streams
                // with the old in-memory state.
                app.setSkipStreamPersistOnShutdown(true);
                needsRestart = true;
            }

            // 4. Import profiles
            if (body.contains("profiles") && body["profiles"].is_array()) {
                app.profileManager().loadFromJson(body["profiles"]);
                app.settingsDb().save("profiles", app.profileManager().toJson());
            }

            // 5. Import audio settings (apply in-memory + sync to config + persist)
            if (body.contains("audio") && body["audio"].is_object()) {
                auto& a = body["audio"];
                auto& audio = app.audioManager();
                if (a.contains("music_volume"))      audio.setMusicVolume(a["music_volume"].get<float>());
                if (a.contains("sfx_volume"))        audio.setSfxVolume(a["sfx_volume"].get<float>());
                if (a.contains("muted"))             audio.setMuted(a["muted"].get<bool>());
                if (a.contains("fade_in_seconds"))   audio.setFadeInDuration(a["fade_in_seconds"].get<float>());
                if (a.contains("fade_out_seconds"))  audio.setFadeOutDuration(a["fade_out_seconds"].get<float>());
                if (a.contains("crossfade_overlap")) audio.setCrossfadeOverlap(a["crossfade_overlap"].get<float>());
                // Sync to config and persist so values survive restart
                auto& cfg = app.config();
                cfg.set("audio.music_volume",      audio.musicVolume());
                cfg.set("audio.sfx_volume",        audio.sfxVolume());
                cfg.set("audio.muted",             audio.isMuted());
                cfg.set("audio.fade_in_seconds",   audio.fadeInDuration());
                cfg.set("audio.fade_out_seconds",  audio.fadeOutDuration());
                cfg.set("audio.crossfade_overlap", audio.crossfadeOverlap());
                app.persistGlobalConfig();
            }

            spdlog::info("[API] Config imported successfully (restart_required={})", needsRestart);
            nlohmann::json result = {{"success", true}};
            if (needsRestart) {
                result["restart_required"] = true;
                result["message"] = "Streams config updated. Restart the application to apply stream changes.";
                // Request graceful shutdown so the container orchestrator restarts us
                app.requestShutdown();
            }
            res.set_content(result.dump(), "application/json");
        } catch (const std::exception& e) {
            spdlog::error("[API] Config import failed: {}", e.what());
            res.status = 400;
            res.set_content(nlohmann::json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // ══════════════════════════════════════════════════════════════════════
    //  Twitch OAuth (Implicit Grant Flow)
    // ══════════════════════════════════════════════════════════════════════

    // Returns the Twitch authorize URL for the Implicit Grant Flow.
    // Frontend opens this in a popup; Twitch redirects back with the token.
    server.Get("/api/auth/twitch/url", [&app](const httplib::Request& req, httplib::Response& res) {
        std::string channelId   = req.has_param("channel_id")
                                    ? req.get_param_value("channel_id") : "";

        // Read client_id and redirect_uri from the channel's own settings
        std::string clientId;
        std::string redirectUri = "http://localhost:8080/auth/twitch/callback/";
        if (!channelId.empty()) {
            const auto* cfg = app.channelManager().getChannelConfig(channelId);
            if (cfg && cfg->settings.is_object()) {
                clientId    = cfg->settings.value("client_id", "");
                redirectUri = cfg->settings.value("redirect_uri", redirectUri);
            }
        }
        // Fallback to global config for backwards compatibility
        if (clientId.empty()) {
            clientId = app.config().get<std::string>("twitch.client_id", "");
        }

        if (clientId.empty()) {
            res.status = 400;
            res.set_content(R"({"error":"client_id not configured — set it in the Twitch channel settings"})", "application/json");
            return;
        }

        // Scopes needed for reading and writing chat
        std::string scopes = "chat:read+chat:edit+channel:manage:broadcast";
        // Pass channel_id in the state so the callback knows where to store the token
        std::string state = channelId;

        std::string url = "https://id.twitch.tv/oauth2/authorize"
            "?response_type=token"
            "&client_id=" + clientId +
            "&redirect_uri=" + redirectUri +
            "&scope=" + scopes +
            "&state=" + state +
            "&force_verify=true";

        res.set_content(nlohmann::json({{"url", url}, {"channelId", channelId}}).dump(), "application/json");
    });

    // Receives the OAuth token from the frontend callback and stores it
    // in the specified channel's settings.
    server.Post("/api/auth/twitch/token", [&app](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = nlohmann::json::parse(req.body);
            std::string channelId  = body.value("channelId", "");
            std::string token      = body.value("accessToken", "");

            if (channelId.empty() || token.empty()) {
                res.status = 400;
                res.set_content(R"({"error":"channelId and accessToken required"})", "application/json");
                return;
            }

            const auto* cfg = app.channelManager().getChannelConfig(channelId);
            if (!cfg) {
                res.status = 404;
                res.set_content(R"({"error":"Channel not found"})", "application/json");
                return;
            }

            // Update the channel's settings with the new token
            core::ChannelConfig updated = *cfg;
            if (!updated.settings.is_object()) updated.settings = nlohmann::json::object();
            updated.settings["oauth_token"] = token;
            app.channelManager().updateChannel(channelId, updated);
            app.persistChannels();

            // Auto-connect the channel so the user doesn't have to click Connect
            app.channelManager().connectChannel(channelId);

            // Refresh Twitch stream info on any stream subscribed to this channel
            for (auto* si : app.streamManager().allStreams()) {
                const auto& ids = si->config().channelIds;
                if (std::find(ids.begin(), ids.end(), channelId) != ids.end()) {
                    si->triggerPlatformInfoUpdate();
                }
            }

            spdlog::info("[API] Twitch OAuth token stored and connect triggered for channel '{}'", channelId);
            res.set_content(R"({"success":true})", "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // Diagnostic: test Twitch Helix API connectivity for a specific channel
    server.Get(R"(/api/twitch/test/([^/]+))", [&app](const httplib::Request& req, httplib::Response& res) {
        std::string chId = pathParam(req);
        nlohmann::json result;

        const auto* cfg = app.channelManager().getChannelConfig(chId);
        if (!cfg || cfg->platform != "twitch") {
            result["error"] = "Channel not found or not a Twitch channel.";
            res.set_content(result.dump(), "application/json");
            return;
        }

        // Read client_id from channel settings, fallback to global config
        std::string clientId = cfg->settings.value("client_id", "");
        if (clientId.empty()) {
            clientId = app.config().get<std::string>("twitch.client_id", "");
        }
        result["client_id_set"]  = !clientId.empty();
        result["curl_available"] = platform::TwitchApi::isCurlAvailable();

        std::string token = cfg->settings.value("oauth_token", "");
        result["oauth_token_set"] = !token.empty();

        if (token.empty() || clientId.empty()) {
            result["error"] = "Missing oauth_token or client_id. Set them in the channel settings.";
            res.set_content(result.dump(), "application/json");
            return;
        }

        // Test: resolve broadcaster ID
        std::string broadcasterId = platform::TwitchApi::getBroadcasterId(token, clientId);
        result["broadcaster_id"] = broadcasterId;
        result["broadcaster_ok"] = !broadcasterId.empty();

        if (broadcasterId.empty()) {
            result["error"] = "Failed to resolve broadcaster ID – check OAuth token and client_id.";
        }

        res.set_content(result.dump(), "application/json");
    });

    // ══════════════════════════════════════════════════════════════════════
    //  YouTube OAuth (Authorization Code Flow)
    // ══════════════════════════════════════════════════════════════════════

    // Returns the Google OAuth 2.0 authorize URL.
    // The frontend opens this in a popup; Google redirects back with a code.
    server.Get("/api/auth/youtube/url", [&app](const httplib::Request& req, httplib::Response& res) {
        std::string channelId = req.has_param("channel_id")
                                    ? req.get_param_value("channel_id") : "";

        std::string clientId;
        std::string redirectUri;
        if (!channelId.empty()) {
            const auto* cfg = app.channelManager().getChannelConfig(channelId);
            if (cfg && cfg->settings.is_object()) {
                clientId    = cfg->settings.value("oauth_client_id", "");
                redirectUri = cfg->settings.value("oauth_redirect_uri", "");
            }
        }

        if (clientId.empty()) {
            res.status = 400;
            res.set_content(R"({"error":"oauth_client_id not configured – set it in the YouTube channel settings"})",
                            "application/json");
            return;
        }

        // Build redirect URI.  Default: relative path on the same host.
        if (redirectUri.empty()) {
            // The frontend must supply an origin or we use a sensible default.
            redirectUri = "http://localhost:8080/auth/youtube/callback/";
        }

        auto enc = [](const std::string& s) { return platform::YouTubeApi::urlEncode(s); };

        // Scopes: read live chat + manage broadcasts (title/description updates)
        std::string scopes =
            "https://www.googleapis.com/auth/youtube"
            " https://www.googleapis.com/auth/youtube.force-ssl"
            " https://www.googleapis.com/auth/youtube.readonly";

        std::string url = "https://accounts.google.com/o/oauth2/v2/auth"
            "?response_type=code"
            "&client_id=" + enc(clientId) +
            "&redirect_uri=" + enc(redirectUri) +
            "&scope=" + enc(scopes) +
            "&access_type=offline"
            "&prompt=consent"
            "&state=" + enc(channelId);

        res.set_content(nlohmann::json({{"url", url}, {"channelId", channelId}}).dump(),
                        "application/json");
    });

    // Exchange the authorization code for tokens (called from the callback page).
    server.Post("/api/auth/youtube/token", [&app](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = nlohmann::json::parse(req.body);
            std::string channelId = body.value("channelId", "");
            std::string code      = body.value("code", "");

            if (channelId.empty() || code.empty()) {
                res.status = 400;
                res.set_content(R"({"error":"channelId and code are required"})", "application/json");
                return;
            }

            const auto* cfg = app.channelManager().getChannelConfig(channelId);
            if (!cfg) {
                res.status = 404;
                res.set_content(R"({"error":"Channel not found"})", "application/json");
                return;
            }

            std::string clientId     = cfg->settings.value("oauth_client_id", "");
            std::string clientSecret = cfg->settings.value("oauth_client_secret", "");
            std::string redirectUri  = cfg->settings.value("oauth_redirect_uri",
                                                            "http://localhost:8080/auth/youtube/callback/");

            if (clientId.empty() || clientSecret.empty()) {
                res.status = 400;
                res.set_content(R"({"error":"oauth_client_id and oauth_client_secret must be set in channel settings"})",
                                "application/json");
                return;
            }

            // Exchange the authorization code for tokens
            auto tr = platform::YouTubeApi::exchangeAuthCode(code, clientId, clientSecret, redirectUri);

            if (!tr.success) {
                res.status = 400;
                res.set_content(nlohmann::json({{"error", tr.error}}).dump(), "application/json");
                return;
            }

            // Store tokens in channel settings
            core::ChannelConfig updated = *cfg;
            if (!updated.settings.is_object()) updated.settings = nlohmann::json::object();
            updated.settings["oauth_token"]         = tr.accessToken;
            updated.settings["oauth_refresh_token"]  = tr.refreshToken;
            updated.settings["oauth_token_expiry"]   = static_cast<int64_t>(std::time(nullptr)) + tr.expiresIn;
            app.channelManager().updateChannel(channelId, updated);
            app.persistChannels();

            // Auto-connect the channel
            app.channelManager().connectChannel(channelId);

            // Trigger platform info update on related streams
            for (auto* si : app.streamManager().allStreams()) {
                const auto& ids = si->config().channelIds;
                if (std::find(ids.begin(), ids.end(), channelId) != ids.end()) {
                    si->triggerPlatformInfoUpdate();
                }
            }

            spdlog::info("[API] YouTube OAuth token stored for channel '{}'", channelId);
            res.set_content(nlohmann::json({
                {"success", true},
                {"has_refresh_token", !tr.refreshToken.empty()},
                {"expires_in", tr.expiresIn}
            }).dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // Refresh the YouTube access token using the stored refresh_token.
    server.Post(R"(/api/auth/youtube/refresh/([^/]+))", [&app](const httplib::Request& req, httplib::Response& res) {
        std::string channelId = pathParam(req);
        const auto* cfg = app.channelManager().getChannelConfig(channelId);
        if (!cfg || cfg->platform != "youtube") {
            res.status = 404;
            res.set_content(R"({"error":"YouTube channel not found"})", "application/json");
            return;
        }

        std::string refreshToken = cfg->settings.value("oauth_refresh_token", "");
        std::string clientId     = cfg->settings.value("oauth_client_id", "");
        std::string clientSecret = cfg->settings.value("oauth_client_secret", "");

        if (refreshToken.empty() || clientId.empty() || clientSecret.empty()) {
            res.status = 400;
            res.set_content(R"({"error":"Missing refresh_token, client_id, or client_secret"})",
                            "application/json");
            return;
        }

        auto tr = platform::YouTubeApi::refreshAccessToken(refreshToken, clientId, clientSecret);
        if (!tr.success) {
            res.status = 400;
            res.set_content(nlohmann::json({{"error", tr.error}}).dump(), "application/json");
            return;
        }

        // Update the stored access token
        core::ChannelConfig updated = *cfg;
        updated.settings["oauth_token"]       = tr.accessToken;
        updated.settings["oauth_token_expiry"] = static_cast<int64_t>(std::time(nullptr)) + tr.expiresIn;
        app.channelManager().updateChannel(channelId, updated);
        app.persistChannels();

        spdlog::info("[API] YouTube OAuth token refreshed for channel '{}'", channelId);
        res.set_content(nlohmann::json({
            {"success", true},
            {"expires_in", tr.expiresIn}
        }).dump(), "application/json");
    });

    // ══════════════════════════════════════════════════════════════════════
    //  YouTube Broadcast Detection & Quota
    // ══════════════════════════════════════════════════════════════════════

    // Trigger manual broadcast detection for a YouTube channel
    server.Post(R"(/api/youtube/detect/([^/]+))", [&app](const httplib::Request& req, httplib::Response& res) {
        std::string channelId = pathParam(req);
        auto* plat = app.channelManager().getPlatform(channelId);
        if (!plat) {
            res.status = 404;
            res.set_content(R"({"error":"Channel not found"})", "application/json");
            return;
        }
        auto* yt = dynamic_cast<platform::YoutubePlatform*>(plat);
        if (!yt) {
            res.status = 400;
            res.set_content(R"({"error":"Channel is not a YouTube channel"})", "application/json");
            return;
        }
        yt->requestDetection();
        res.set_content(R"({"success":true,"message":"Detection requested"})", "application/json");
    });

    // Get YouTube API quota status
    server.Get("/api/youtube/quota", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(platform::YouTubeQuota::instance().toJson().dump(), "application/json");
    });

    // Update YouTube API quota budget or reset
    server.Put("/api/youtube/quota", [](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = nlohmann::json::parse(req.body);
            if (body.contains("budget")) {
                int budget = body["budget"].get<int>();
                if (budget < 0 || budget > platform::YouTubeQuota::COST_MAX_DAILY) {
                    res.status = 400;
                    res.set_content(R"({"error":"Budget must be 0-10000"})", "application/json");
                    return;
                }
                platform::YouTubeQuota::instance().setBudget(budget);
            }
            if (body.value("reset", false)) {
                platform::YouTubeQuota::instance().reset();
            }
            res.set_content(platform::YouTubeQuota::instance().toJson().dump(), "application/json");
        } catch (...) {
            res.status = 400;
            res.set_content(R"({"error":"Invalid JSON"})", "application/json");
        }
    });

    // ══════════════════════════════════════════════════════════════════════
    //  Local Chat (Test)
    // ══════════════════════════════════════════════════════════════════════

    server.Post("/api/chat", [&app](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = nlohmann::json::parse(req.body);
            std::string username = body.value("username", "dashboard");
            std::string avatarUrl = body.value("avatar_url", "");
            std::string text     = body.value("text", "");
            if (text.empty()) {
                res.status = 400;
                res.set_content(R"({"error":"Empty message"})", "application/json");
                return;
            }
            app.channelManager().injectLocalMessage(username, text, avatarUrl);
            res.set_content(R"({"success":true})", "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json({{"error",e.what()}}).dump(), "application/json");
        }
    });

    server.Get("/api/chat/log", [&app](const httplib::Request&, httplib::Response& res) {
        auto log = app.channelManager().getLocalMessageLog();
        nlohmann::json j = log;
        res.set_content(j.dump(), "application/json");
    });

    // ══════════════════════════════════════════════════════════════════════
    //  Platform Chat (send to specific channel or all)
    // ══════════════════════════════════════════════════════════════════════

    // Send a message to a specific channel
    server.Post(R"(/api/channels/([^/]+)/send)", [&app](const httplib::Request& req, httplib::Response& res) {
        try {
            std::string channelId = pathParam(req);
            auto body = nlohmann::json::parse(req.body);
            std::string text = body.value("text", "");
            if (text.empty()) {
                res.status = 400;
                res.set_content(R"({"error":"Empty message"})", "application/json");
                return;
            }
            bool ok = app.channelManager().sendMessageToChannel(channelId, text);
            if (!ok) {
                res.status = 400;
                res.set_content(R"({"error":"Channel not found or not connected"})", "application/json");
                return;
            }
            res.set_content(R"({"success":true})", "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json({{"error",e.what()}}).dump(), "application/json");
        }
    });

    // Send a message to all connected channels
    server.Post("/api/chat/broadcast", [&app](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = nlohmann::json::parse(req.body);
            std::string text = body.value("text", "");
            if (text.empty()) {
                res.status = 400;
                res.set_content(R"({"error":"Empty message"})", "application/json");
                return;
            }
            int sent = app.channelManager().sendMessageToAll(text);
            res.set_content(nlohmann::json({{"success",true},{"sent_to",sent}}).dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json({{"error",e.what()}}).dump(), "application/json");
        }
    });

    // ══════════════════════════════════════════════════════════════════════
    //  Scoreboard (Player Database)
    // ══════════════════════════════════════════════════════════════════════

    // Top players in last N hours (default 24h, limit default 10)
    server.Get("/api/scoreboard/recent", [&app](const httplib::Request& req, httplib::Response& res) {
        int limit = 10;
        int hours = 24;
        if (req.has_param("limit"))
            limit = std::min(50, std::max(1, std::stoi(req.get_param_value("limit"))));
        if (req.has_param("hours"))
            hours = std::min(720, std::max(1, std::stoi(req.get_param_value("hours"))));
        res.set_content(app.playerDatabase().recentToJson(limit, hours).dump(2), "application/json");
    });

    // All-time top players (default limit 5)
    server.Get("/api/scoreboard/alltime", [&app](const httplib::Request& req, httplib::Response& res) {
        int limit = 5;
        if (req.has_param("limit"))
            limit = std::min(50, std::max(1, std::stoi(req.get_param_value("limit"))));
        res.set_content(app.playerDatabase().allTimeToJson(limit).dump(2), "application/json");
    });

    // Player stats by userId
    server.Get(R"(/api/scoreboard/player/([^/]+))", [&app](const httplib::Request& req, httplib::Response& res) {
        auto stats = app.playerDatabase().getPlayerStats(pathParam(req));
        if (stats.empty()) {
            res.status = 404;
            res.set_content(R"({"error":"Player not found"})", "application/json");
            return;
        }
        res.set_content(stats.dump(2), "application/json");
    });

    // ── Scoreboard overlay config (global) ───────────────────────────────

    server.Get("/api/scoreboard/config", [&app](const httplib::Request&, httplib::Response& res) {
        res.set_content(app.scoreboardConfig().toJson().dump(2), "application/json");
    });

    server.Put("/api/scoreboard/config", [&app](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = nlohmann::json::parse(req.body);
            auto cfg = is::core::GlobalScoreboardConfig::fromJson(j);
            app.setScoreboardConfig(cfg);
            app.persistScoreboardConfig();
            res.set_content(R"({"success":true})", "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // ── Player management ────────────────────────────────────────────────

    server.Get("/api/scoreboard/players", [&app](const httplib::Request&, httplib::Response& res) {
        auto players = app.playerDatabase().getAllPlayers();
        // Include hidden status from scoreboard config
        auto hidden = app.scoreboardConfig().hiddenPlayers;
        std::unordered_set<std::string> hiddenSet(hidden.begin(), hidden.end());
        for (auto& p : players) {
            p["hidden"] = hiddenSet.count(p["userId"].get<std::string>()) > 0;
        }
        res.set_content(nlohmann::json({{"players", players}}).dump(2), "application/json");
    });

    server.Put(R"(/api/scoreboard/players/([^/]+))", [&app](const httplib::Request& req, httplib::Response& res) {
        try {
            auto userId = pathParam(req);
            auto j = nlohmann::json::parse(req.body);
            if (j.contains("points") && j["points"].is_number_integer()) {
                app.playerDatabase().updatePlayerPoints(userId, j["points"].get<int>());
            }
            res.set_content(R"({"success":true})", "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // Reset all players
    server.Delete("/api/scoreboard/players", [&app](const httplib::Request&, httplib::Response& res) {
        app.playerDatabase().resetAllPlayers();
        res.set_content(R"({"success":true})", "application/json");
    });

    server.Delete(R"(/api/scoreboard/players/([^/]+))", [&app](const httplib::Request& req, httplib::Response& res) {
        auto userId = pathParam(req);
        if (app.playerDatabase().deletePlayer(userId)) {
            res.set_content(R"({"success":true})", "application/json");
        } else {
            res.status = 404;
            res.set_content(R"({"error":"Player not found"})", "application/json");
        }
    });

    // ── Country management ────────────────────────────────────────────────

    // Get all countries with win counts
    server.Get("/api/scoreboard/countries", [&app](const httplib::Request&, httplib::Response& res) {
        auto countries = app.playerDatabase().getAllCountries();
        res.set_content(nlohmann::json({{"countries", countries}}).dump(2), "application/json");
    });

    // Reset all country wins
    server.Delete("/api/scoreboard/countries", [&app](const httplib::Request&, httplib::Response& res) {
        app.playerDatabase().resetAllCountryWins();
        res.set_content(R"({"success":true})", "application/json");
    });

    // Delete wins for a specific country
    server.Delete(R"(/api/scoreboard/countries/([^/]+))", [&app](const httplib::Request& req, httplib::Response& res) {
        auto code = pathParam(req);
        if (app.playerDatabase().deleteCountryWins(code)) {
            res.set_content(R"({"success":true})", "application/json");
        } else {
            res.status = 404;
            res.set_content(R"({"error":"Country not found"})", "application/json");
        }
    });

    // ══════════════════════════════════════════════════════════════════════
    //  Performance Monitoring
    // ══════════════════════════════════════════════════════════════════════

    // Current averages (last 60s by default)
    server.Get("/api/perf", [&app](const httplib::Request& req, httplib::Response& res) {
        int seconds = 60;
        if (req.has_param("seconds"))
            seconds = std::min(600, std::max(5, std::stoi(req.get_param_value("seconds"))));
        res.set_content(app.perfMonitor().getAverages(seconds).dump(2), "application/json");
    });

    // Time-series data for charts (last 300s by default)
    server.Get("/api/perf/history", [&app](const httplib::Request& req, httplib::Response& res) {
        int seconds = 300;
        if (req.has_param("seconds"))
            seconds = std::min(3600, std::max(10, std::stoi(req.get_param_value("seconds"))));
        res.set_content(app.perfMonitor().toJson(seconds).dump(), "application/json");
    });

    // ══════════════════════════════════════════════════════════════════════
    //  Statistics (per-stream & per-channel interaction stats)
    // ══════════════════════════════════════════════════════════════════════

    // All channel stats
    server.Get("/api/stats/channels", [&app](const httplib::Request&, httplib::Response& res) {
        res.set_content(app.channelManager().getStatsJson().dump(2), "application/json");
    });

    // Single channel stats
    server.Get(R"(/api/stats/channels/([^/]+))", [&app](const httplib::Request& req, httplib::Response& res) {
        std::string id = pathParam(req);
        const auto* stats = app.channelManager().getChannelStats(id);
        if (!stats) {
            res.status = 404;
            res.set_content(R"({"error":"Channel not found"})", "application/json");
            return;
        }
        nlohmann::json j;
        j["channelId"] = id;
        j["stats"] = stats->toJson();
        res.set_content(j.dump(2), "application/json");
    });

    // All stream stats
    server.Get("/api/stats/streams", [&app](const httplib::Request&, httplib::Response& res) {
        nlohmann::json arr = nlohmann::json::array();
        for (auto* stream : app.streamManager().allStreams()) {
            nlohmann::json s;
            s["streamId"]   = stream->config().id;
            s["streamName"] = stream->config().name;
            s["stats"]      = stream->stats().toJson();
            arr.push_back(s);
        }
        res.set_content(arr.dump(2), "application/json");
    });

    // Single stream stats
    server.Get(R"(/api/stats/streams/([^/]+))", [&app](const httplib::Request& req, httplib::Response& res) {
        std::string id = pathParam(req);
        auto* stream = app.streamManager().getStream(id);
        if (!stream) {
            res.status = 404;
            res.set_content(R"({"error":"Stream not found"})", "application/json");
            return;
        }
        nlohmann::json j;
        j["streamId"]   = id;
        j["streamName"] = stream->config().name;
        j["stats"]      = stream->stats().toJson();
        res.set_content(j.dump(2), "application/json");
    });

    // Reset stats for a specific channel
    server.Post(R"(/api/stats/channels/([^/]+)/reset)", [&app](const httplib::Request& req, httplib::Response& res) {
        app.channelManager().resetChannelStats(pathParam(req));
        res.set_content(R"({"success":true})", "application/json");
    });

    // Reset stats for a specific stream
    server.Post(R"(/api/stats/streams/([^/]+)/reset)", [&app](const httplib::Request& req, httplib::Response& res) {
        auto* stream = app.streamManager().getStream(pathParam(req));
        if (!stream) {
            res.status = 404;
            res.set_content(R"({"error":"Stream not found"})", "application/json");
            return;
        }
        stream->resetStats();
        res.set_content(R"({"success":true})", "application/json");
    });

    // Reset ALL stats (channels + streams)
    server.Post("/api/stats/reset", [&app](const httplib::Request&, httplib::Response& res) {
        app.channelManager().resetAllStats();
        for (auto* stream : app.streamManager().allStreams())
            stream->resetStats();
        res.set_content(R"({"success":true})", "application/json");
    });

    // ══════════════════════════════════════════════════════════════════════
    //  Audio
    // ══════════════════════════════════════════════════════════════════════

    server.Get("/api/audio", [&app](const httplib::Request&, httplib::Response& res) {
        auto& audio = app.audioManager();
        nlohmann::json j;
        j["playing"]            = audio.isMusicPlaying();
        j["muted"]              = audio.isMuted();
        j["musicVolume"]        = audio.musicVolume();
        j["sfxVolume"]          = audio.sfxVolume();
        j["currentTrack"]       = audio.currentTrackName();
        j["trackCount"]         = audio.trackCount();
        j["fadeInSeconds"]      = audio.fadeInDuration();
        j["fadeOutSeconds"]     = audio.fadeOutDuration();
        j["crossfadeOverlap"]   = audio.crossfadeOverlap();
        j["musicDirectory"]     = app.config().get<std::string>("audio.music_directory", "assets/audio");
        j["sfxDirectory"]       = app.config().get<std::string>("audio.sfx_directory", "assets/audio/sfx");
        res.set_content(j.dump(), "application/json");
    });

    server.Put("/api/audio", [&app](const httplib::Request& req, httplib::Response& res) {
        auto body = nlohmann::json::parse(req.body, nullptr, false);
        if (body.is_discarded()) {
            res.status = 400;
            res.set_content(R"({"error":"invalid json"})", "application/json");
            return;
        }
        auto& audio = app.audioManager();
        if (body.contains("musicVolume")) {
            audio.setMusicVolume(body["musicVolume"].get<float>());
        }
        if (body.contains("sfxVolume")) {
            audio.setSfxVolume(body["sfxVolume"].get<float>());
        }
        if (body.contains("muted")) {
            audio.setMuted(body["muted"].get<bool>());
        }
        if (body.contains("fadeInSeconds"))
            audio.setFadeInDuration(body["fadeInSeconds"].get<float>());
        if (body.contains("fadeOutSeconds"))
            audio.setFadeOutDuration(body["fadeOutSeconds"].get<float>());
        if (body.contains("crossfadeOverlap"))
            audio.setCrossfadeOverlap(body["crossfadeOverlap"].get<float>());

        // Audio directory changes – apply immediately
        auto& cfg = app.config();
        if (body.contains("musicDirectory")) {
            std::string dir = body["musicDirectory"].get<std::string>();
            cfg.set("audio.music_directory", dir);
            audio.scanMusicDirectory(dir);
            if (audio.isMusicPlaying()) audio.playMusic();
        }
        if (body.contains("sfxDirectory")) {
            cfg.set("audio.sfx_directory", body["sfxDirectory"].get<std::string>());
            // SFX are reloaded when games reinitialize; no immediate action needed.
        }

        // Persist to config
        cfg.set("audio.music_volume",      audio.musicVolume());
        cfg.set("audio.sfx_volume",        audio.sfxVolume());
        cfg.set("audio.muted",             audio.isMuted());
        cfg.set("audio.fade_in_seconds",   audio.fadeInDuration());
        cfg.set("audio.fade_out_seconds",  audio.fadeOutDuration());
        cfg.set("audio.crossfade_overlap", audio.crossfadeOverlap());
        app.persistGlobalConfig();

        res.set_content(R"({"success":true})", "application/json");
    });

    server.Post("/api/audio/next", [&app](const httplib::Request&, httplib::Response& res) {
        app.audioManager().nextTrack();
        res.set_content(R"({"success":true})", "application/json");
    });

    server.Post("/api/audio/pause", [&app](const httplib::Request&, httplib::Response& res) {
        app.audioManager().pauseMusic();
        res.set_content(R"({"success":true})", "application/json");
    });

    server.Post("/api/audio/resume", [&app](const httplib::Request&, httplib::Response& res) {
        app.audioManager().resumeMusic();
        res.set_content(R"({"success":true})", "application/json");
    });

    server.Post("/api/audio/rescan", [&app](const httplib::Request&, httplib::Response& res) {
        app.audioManager().rescan();
        nlohmann::json j;
        j["success"]    = true;
        j["trackCount"] = app.audioManager().trackCount();
        res.set_content(j.dump(), "application/json");
    });

    // ══════════════════════════════════════════════════════════════════════
    //  CORS
    // ══════════════════════════════════════════════════════════════════════

    server.set_default_headers({
        {"Access-Control-Allow-Origin", "*"},
        {"Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS"},
        {"Access-Control-Allow-Headers", "Content-Type"}
    });

    // Handle OPTIONS pre-flight
    server.Options(R"(.*)", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("", "text/plain");
    });

    spdlog::info("[API] Routes registered.");
}

} // namespace is::web
