#include "web/ApiRoutes.h"
#include "core/Application.h"
#include "core/Config.h"
#include "core/ChannelManager.h"
#include "core/StreamManager.h"
#include "core/StreamInstance.h"
#include "core/PlayerDatabase.h"
#include "core/PerfMonitor.h"
#include "core/SettingsDatabase.h"
#include "games/GameRegistry.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace is::web {

// Helper: extract first regex capture from path
static std::string pathParam(const httplib::Request& req, size_t idx = 1) {
    if (idx < req.matches.size()) return std::string(req.matches[idx]);
    return "";
}

void ApiRoutes::setup(httplib::Server& server, core::Application& app) {

    // ══════════════════════════════════════════════════════════════════════
    //  System
    // ══════════════════════════════════════════════════════════════════════

    server.Get("/api/status", [&app](const httplib::Request&, httplib::Response& res) {
        nlohmann::json s;
        s["version"]  = "0.2.0";
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
        app.channelManager().connectChannel(pathParam(req));
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
            auto cfg  = core::StreamInstance::configFromJson(body);
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
            auto cfg  = core::StreamInstance::configFromJson(body);
            cfg.id = id;  // preserve the path-param ID
            app.streamManager().updateStream(id, cfg);
            app.persistStreams();
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
        bool ok = app.streamManager().startStreaming(id);
        if (!ok) {
            res.status = 400;
            res.set_content(R"({"error":"Cannot start streaming: no RTMP URL configured. Set stream URL and key first."})", "application/json");
            return;
        }
        res.set_content(R"({"success":true})", "application/json");
    });

    server.Post(R"(/api/streams/([^/]+)/stop)", [&app](const httplib::Request& req, httplib::Response& res) {
        app.streamManager().stopStreaming(pathParam(req));
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
                }
            }
        }
        if (cfg.contains("streams")) {
            for (auto& st : cfg["streams"]) {
                if (st.contains("stream_key")) st["stream_key"] = "***";
            }
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
    //  Twitch OAuth (Implicit Grant Flow)
    // ══════════════════════════════════════════════════════════════════════

    // Returns the Twitch authorize URL for the Implicit Grant Flow.
    // Frontend opens this in a popup; Twitch redirects back with the token.
    server.Get("/api/auth/twitch/url", [&app](const httplib::Request& req, httplib::Response& res) {
        std::string clientId    = app.config().get<std::string>("twitch.client_id", "");
        std::string redirectUri = app.config().get<std::string>("twitch.redirect_uri",
                                     "http://localhost:8080/auth/twitch/callback/");
        std::string channelId   = req.has_param("channel_id")
                                    ? req.get_param_value("channel_id") : "";

        if (clientId.empty()) {
            res.status = 400;
            res.set_content(R"({"error":"twitch.client_id not configured"})", "application/json");
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

            spdlog::info("[API] Twitch OAuth token stored for channel '{}'", channelId);
            res.set_content(R"({"success":true})", "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // ══════════════════════════════════════════════════════════════════════
    //  Local Chat (Test)
    // ══════════════════════════════════════════════════════════════════════

    server.Post("/api/chat", [&app](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = nlohmann::json::parse(req.body);
            std::string username = body.value("username", "dashboard");
            std::string text     = body.value("text", "");
            if (text.empty()) {
                res.status = 400;
                res.set_content(R"({"error":"Empty message"})", "application/json");
                return;
            }
            app.channelManager().injectLocalMessage(username, text);
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
