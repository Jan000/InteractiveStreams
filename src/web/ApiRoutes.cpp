#include "web/ApiRoutes.h"
#include "core/Application.h"
#include "core/GameManager.h"
#include "core/Config.h"
#include "platform/PlatformManager.h"
#include "platform/local/LocalPlatform.h"
#include "streaming/StreamEncoder.h"
#include "streaming/StreamOutput.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace is::web {

void ApiRoutes::setup(httplib::Server& server, core::Application& app) {
    // ──────── System Status ──────────────────────────────────────────────────
    server.Get("/api/status", [&app](const httplib::Request&, httplib::Response& res) {
        nlohmann::json status;
        status["version"] = "0.1.0";
        status["uptime"] = 0; // TODO: Track uptime

        // Game info
        auto* game = app.gameManager().activeGame();
        if (game) {
            status["game"]["id"] = game->id();
            status["game"]["name"] = game->displayName();
            status["game"]["state"] = game->getState();
            status["game"]["commands"] = game->getCommands();
        }

        // Platforms
        status["platforms"] = app.platformManager().getStatus();

        // Streaming
        status["streaming"]["encoding"] = app.streamEncoder().isRunning();
        status["streaming"]["fps"] = app.streamEncoder().getFps();
        status["streaming"]["frames"] = app.streamEncoder().getFrameCount();
        status["streaming"]["targets"] = app.streamOutput().getStatus();

        res.set_content(status.dump(2), "application/json");
    });

    // ──────── Game Management ────────────────────────────────────────────────
    server.Get("/api/games", [&app](const httplib::Request&, httplib::Response& res) {
        auto games = app.gameManager().availableGames();
        nlohmann::json j = nlohmann::json::array();
        for (const auto& g : games) {
            j.push_back(g);
        }
        res.set_content(j.dump(), "application/json");
    });

    server.Post("/api/games/load", [&app](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = nlohmann::json::parse(req.body);
            std::string gameName = body["game"];
            app.gameManager().loadGame(gameName);
            res.set_content(R"({"success":true})", "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    server.Get("/api/games/state", [&app](const httplib::Request&, httplib::Response& res) {
        auto* game = app.gameManager().activeGame();
        if (game) {
            res.set_content(game->getState().dump(2), "application/json");
        } else {
            res.status = 404;
            res.set_content(R"({"error":"No active game"})", "application/json");
        }
    });

    // ──────── Platform Management ────────────────────────────────────────────
    server.Get("/api/platforms", [&app](const httplib::Request&, httplib::Response& res) {
        res.set_content(app.platformManager().getStatus().dump(2), "application/json");
    });

    server.Post("/api/platforms/connect", [&app](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = nlohmann::json::parse(req.body);
            std::string platformId = body["platform"];
            auto* platform = app.platformManager().getPlatform(platformId);
            if (platform) {
                platform->connect();
                res.set_content(R"({"success":true})", "application/json");
            } else {
                res.status = 404;
                res.set_content(R"({"error":"Platform not found"})", "application/json");
            }
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    server.Post("/api/platforms/disconnect", [&app](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = nlohmann::json::parse(req.body);
            std::string platformId = body["platform"];
            auto* platform = app.platformManager().getPlatform(platformId);
            if (platform) {
                platform->disconnect();
                res.set_content(R"({"success":true})", "application/json");
            } else {
                res.status = 404;
                res.set_content(R"({"error":"Platform not found"})", "application/json");
            }
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    // ──────── Configuration ──────────────────────────────────────────────────
    server.Get("/api/config", [&app](const httplib::Request&, httplib::Response& res) {
        // Return config but redact sensitive fields
        auto config = app.config().raw();
        // Redact tokens/keys
        if (config.contains("platforms")) {
            for (auto& [key, val] : config["platforms"].items()) {
                if (val.contains("oauth_token")) val["oauth_token"] = "***";
                if (val.contains("api_key")) val["api_key"] = "***";
                if (val.contains("stream_key")) val["stream_key"] = "***";
            }
        }
        if (config.contains("streaming") && config["streaming"].contains("targets")) {
            for (auto& t : config["streaming"]["targets"]) {
                if (t.contains("stream_key")) t["stream_key"] = "***";
            }
        }
        res.set_content(config.dump(2), "application/json");
    });

    // ──────── Streaming ──────────────────────────────────────────────────────
    server.Get("/api/streaming", [&app](const httplib::Request&, httplib::Response& res) {
        nlohmann::json j;
        j["encoding"] = app.streamEncoder().isRunning();
        j["fps"] = app.streamEncoder().getFps();
        j["frames"] = app.streamEncoder().getFrameCount();
        j["targets"] = app.streamOutput().getStatus();
        res.set_content(j.dump(2), "application/json");
    });
    // ──────── Local Chat (Test) ───────────────────────────────────────────
    server.Post("/api/chat", [&app](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = nlohmann::json::parse(req.body);
            std::string username = body.value("username", "dashboard");
            std::string text = body.value("text", "");
            if (text.empty()) {
                res.status = 400;
                res.set_content(R"({"error":"Empty message"})", "application/json");
                return;
            }
            auto* local = dynamic_cast<platform::LocalPlatform*>(
                app.platformManager().getPlatform("local"));
            if (local) {
                local->injectMessage(username, text);
                res.set_content(R"({"success":true})", "application/json");
            } else {
                res.status = 404;
                res.set_content(R"({"error":"Local platform not found"})", "application/json");
            }
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(nlohmann::json({{"error", e.what()}}).dump(), "application/json");
        }
    });

    server.Get("/api/chat/log", [&app](const httplib::Request&, httplib::Response& res) {
        auto* local = dynamic_cast<platform::LocalPlatform*>(
            app.platformManager().getPlatform("local"));
        if (local) {
            nlohmann::json j = local->getMessageLog();
            res.set_content(j.dump(), "application/json");
        } else {
            res.set_content("[]", "application/json");
        }
    });
    // ──────── Shutdown ───────────────────────────────────────────────────────
    server.Post("/api/shutdown", [&app](const httplib::Request&, httplib::Response& res) {
        spdlog::warn("[API] Shutdown requested via web dashboard.");
        app.requestShutdown();
        res.set_content(R"({"success":true,"message":"Shutting down..."})", "application/json");
    });

    // ──────── CORS headers ───────────────────────────────────────────────────
    server.set_default_headers({
        {"Access-Control-Allow-Origin", "*"},
        {"Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS"},
        {"Access-Control-Allow-Headers", "Content-Type"}
    });

    spdlog::info("[API] Routes registered.");
}

} // namespace is::web
