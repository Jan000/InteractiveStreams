#include "web/ApiRoutes.h"
#include "core/Application.h"
#include "core/Config.h"
#include "core/ChannelManager.h"
#include "core/StreamManager.h"
#include "core/StreamInstance.h"
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
            res.set_content(R"({"success":true})", "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json({{"error",e.what()}}).dump(), "application/json");
        }
    });

    server.Delete(R"(/api/channels/([^/]+))", [&app](const httplib::Request& req, httplib::Response& res) {
        app.channelManager().removeChannel(pathParam(req));
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
            res.set_content(R"({"success":true})", "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json({{"error",e.what()}}).dump(), "application/json");
        }
    });

    server.Delete(R"(/api/streams/([^/]+))", [&app](const httplib::Request& req, httplib::Response& res) {
        app.streamManager().removeStream(pathParam(req));
        res.set_content(R"({"success":true})", "application/json");
    });

    // Start / stop streaming (FFmpeg encoding)
    server.Post(R"(/api/streams/([^/]+)/start)", [&app](const httplib::Request& req, httplib::Response& res) {
        app.streamManager().startStreaming(pathParam(req));
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
            res.set_content(R"({"success":true})", "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json({{"error",e.what()}}).dump(), "application/json");
        }
    });

    server.Post("/api/config/save", [&app](const httplib::Request&, httplib::Response& res) {
        // Synchronise runtime state back to config JSON, then save to disk
        app.config().rawMut()["channels"] = app.channelManager().toJson();
        app.config().rawMut()["streams"]  = app.streamManager().toJson();
        app.config().save();
        res.set_content(R"({"success":true})", "application/json");
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
