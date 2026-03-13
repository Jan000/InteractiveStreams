#include "core/Application.h"
#include "core/Config.h"
#include "core/ChannelManager.h"
#include "core/StreamManager.h"
#include "core/StreamProfile.h"
#include "core/Logger.h"
#include "core/PlayerDatabase.h"
#include "core/PerfMonitor.h"
#include "core/SettingsDatabase.h"
#include "core/AudioManager.h"
#include "core/AudioMixer.h"
#include "rendering/Renderer.h"
#include "web/WebServer.h"
#include "games/GameRegistry.h"

#include <SFML/Graphics.hpp>
#include <spdlog/spdlog.h>
#include <chrono>
#include <thread>
#include <atomic>

namespace is::core {

Application* Application::s_instance = nullptr;

struct Application::Impl {
    bool                                       running = false;
    std::unique_ptr<Config>                    config;
    std::unique_ptr<SettingsDatabase>          settingsDb;
    std::unique_ptr<ChannelManager>            channelManager;
    std::unique_ptr<StreamManager>             streamManager;
    std::unique_ptr<ProfileManager>            profileManager;
    std::unique_ptr<PlayerDatabase>            playerDatabase;
    std::unique_ptr<PerfMonitor>               perfMonitor;
    std::unique_ptr<AudioManager>              audioManager;
    std::unique_ptr<AudioMixer>                audioMixer;
    std::unique_ptr<rendering::Renderer>       renderer;
    std::unique_ptr<web::WebServer>            webServer;

    int    targetFps      = 60;
    double fixedDeltaTime = 1.0 / 60.0;
    bool   resetPassword  = false;
    std::string previewStreamId;
    std::atomic<bool> skipStreamPersistOnShutdown{false};
};

Application::Application(int argc, char* argv[])
    : m_impl(std::make_unique<Impl>())
{
    s_instance = this;

    Logger::init();
    spdlog::info("InteractiveStreams v{}.{}.{} starting...", 0, 2, 0);
    // Log games that were auto-registered during static init (REGISTER_GAME macros)
    for (const auto& name : is::games::GameRegistry::instance().list()) {
        spdlog::info("  Game registered: '{}'", name);
    }

    std::string configPath = "config/default.json";
    bool resetPassword = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--reset-password") {
            resetPassword = true;
        } else {
            configPath = arg;
        }
    }

    m_impl->config = std::make_unique<Config>(configPath);
    m_impl->targetFps = m_impl->config->get<int>("application.target_fps", 60);
    m_impl->fixedDeltaTime = 1.0 / static_cast<double>(m_impl->targetFps);
    m_impl->resetPassword = resetPassword;
}

Application::~Application() {
    s_instance = nullptr;
}

void Application::initialize() {
    spdlog::info("Initializing subsystems...");
    auto& cfg = *m_impl->config;

    // ── Settings database (SQLite – persistent storage for ALL settings) ─
    m_impl->settingsDb = std::make_unique<SettingsDatabase>();
    m_impl->settingsDb->open("data/settings.db");

    // Handle --reset-password flag
    if (m_impl->resetPassword) {
        m_impl->settingsDb->remove("auth_password");
        spdlog::warn("[Auth] Password has been reset via --reset-password flag.");
    }

    // Determine data source: SQLite (if populated) or JSON config (first run)
    bool hasSavedChannels = m_impl->settingsDb->has("channels");
    bool hasSavedStreams  = m_impl->settingsDb->has("streams");
    bool hasSavedConfig   = m_impl->settingsDb->has("config");

    // Merge saved global config into the in-memory Config (override defaults)
    if (hasSavedConfig) {
        auto savedCfg = m_impl->settingsDb->load("config");
        if (savedCfg.is_object()) {
            for (auto it = savedCfg.begin(); it != savedCfg.end(); ++it) {
                cfg.rawMut()[it.key()] = it.value();
            }
            spdlog::info("[SettingsDB] Global config restored from SQLite.");
        }
    }

    // ── Channel manager ──────────────────────────────────────────────────
    auto channelsJson = hasSavedChannels
        ? m_impl->settingsDb->load("channels")
        : (cfg.raw().contains("channels") ? cfg.raw()["channels"] : nlohmann::json::array());

    m_impl->channelManager = std::make_unique<ChannelManager>();
    if (channelsJson.is_array()) {
        m_impl->channelManager->loadFromJson(channelsJson);
    }
    // NOTE: connectAllEnabled() is called AFTER setStreamingChecker() below
    // to ensure YouTube platforms have the checker before starting their poll loop.

    // ── Player database (SQLite) ─────────────────────────────────────────
    m_impl->playerDatabase = std::make_unique<PlayerDatabase>();
    m_impl->playerDatabase->open("data/players.db");

    // ── Global scoreboard config ─────────────────────────────────────────
    {
        auto sbJson = m_impl->settingsDb->load("scoreboard");
        if (sbJson.is_object()) {
            m_scoreboardConfig = GlobalScoreboardConfig::fromJson(sbJson);
        }
        // else: uses defaults set by constructor
    }

    // ── Performance monitor ──────────────────────────────────────────────
    m_impl->perfMonitor = std::make_unique<PerfMonitor>();

    // ── Audio manager ────────────────────────────────────────────────────
    m_impl->audioManager = std::make_unique<AudioManager>();
    m_impl->audioManager->scanMusicDirectory("assets/audio");
    m_impl->audioManager->setMusicVolume(
        cfg.get<float>("audio.music_volume", 50.0f));
    m_impl->audioManager->setSfxVolume(
        cfg.get<float>("audio.sfx_volume", 70.0f));
    m_impl->audioManager->setMuted(
        cfg.get<bool>("audio.muted", false));
    m_impl->audioManager->setFadeInDuration(
        cfg.get<float>("audio.fade_in_seconds", 2.0f));
    m_impl->audioManager->setFadeOutDuration(
        cfg.get<float>("audio.fade_out_seconds", 2.0f));
    m_impl->audioManager->setCrossfadeOverlap(
        cfg.get<float>("audio.crossfade_overlap", 1.5f));
    m_impl->audioManager->playMusic();
    spdlog::info("AudioManager initialised ({} tracks found).",
        m_impl->audioManager->trackCount());

    // ── Audio mixer (for stream encoding) ─────────────────────────────────
    m_impl->audioMixer = std::make_unique<AudioMixer>();

    // Connect AudioManager -> AudioMixer; manager pushes full state and track.
    m_impl->audioManager->setAudioMixer(m_impl->audioMixer.get());
    spdlog::info("AudioMixer initialised for stream encoding.");

    // ── Preview renderer (must be created BEFORE streams, because
    //    GameManager::loadGame() accesses the renderer for setWindowTitle) ─
    // IS_HEADLESS env var (set in Docker) always wins over config/SQLite,
    // so that a config import with "headless": false cannot break headless mode.
    bool headless = cfg.get<bool>("application.headless", false);
    const char* envHeadless = std::getenv("IS_HEADLESS");
    if (envHeadless && std::string(envHeadless) == "1") {
        headless = true;
        spdlog::info("Headless mode forced by IS_HEADLESS=1 environment variable.");
    }
    m_impl->renderer = std::make_unique<rendering::Renderer>(1080, 1920,
        cfg.get<std::string>("rendering.title", "InteractiveStreams"),
        headless);

    // ── Profile manager ─────────────────────────────────────────────────
    bool hasSavedProfiles = m_impl->settingsDb->has("profiles");
    auto profilesJson = hasSavedProfiles
        ? m_impl->settingsDb->load("profiles")
        : nlohmann::json::array();

    m_impl->profileManager = std::make_unique<ProfileManager>();
    if (profilesJson.is_array()) {
        m_impl->profileManager->loadFromJson(profilesJson);
    }
    spdlog::info("[ProfileManager] Loaded {} profile(s).", m_impl->profileManager->count());

    // ── Stream manager ───────────────────────────────────────────────────
    auto streamsJson = hasSavedStreams
        ? m_impl->settingsDb->load("streams")
        : (cfg.raw().contains("streams") ? cfg.raw()["streams"] : nlohmann::json::array());

    m_impl->streamManager = std::make_unique<StreamManager>();
    if (streamsJson.is_array()) {
        // Resolve profile inheritance before loading streams
        nlohmann::json resolvedStreams = nlohmann::json::array();
        for (const auto& s : streamsJson) {
            resolvedStreams.push_back(m_impl->profileManager->resolveStreamConfig(s));
        }
        m_impl->streamManager->loadFromJson(resolvedStreams);
    }

    // Ensure at least one stream exists
    if (m_impl->streamManager->count() == 0) {
        StreamConfig def;
        def.id         = "default";
        def.name       = "Main Stream";
        def.channelIds = {"local"};
        def.fixedGame  = cfg.get<std::string>("application.default_game", "chaos_arena");
        m_impl->streamManager->addStream(def);
    }

    // Set preview to the first stream
    auto streams = m_impl->streamManager->allStreams();
    if (!streams.empty()) m_impl->previewStreamId = streams[0]->config().id;

    // Load and apply per-game settings to all streams
    {
        nlohmann::json gameSettings;
        if (cfg.raw().contains("game_settings") && cfg.raw()["game_settings"].is_object()) {
            gameSettings = cfg.raw()["game_settings"];
        }
        if (!gameSettings.empty()) {
            for (auto* s : m_impl->streamManager->allStreams()) {
                for (auto& [gameId, settings] : gameSettings.items()) {
                    s->gameManager().setGameSettings(gameId, settings);
                }
            }
            spdlog::info("Applied game settings for {} games.", gameSettings.size());
        }
    }

    // Tell ChannelManager how to check whether any stream is live.
    // YouTube platforms use this to defer liveChatId auto-detection until
    // a stream is actually encoding.
    m_impl->channelManager->setStreamingChecker([this]() -> bool {
        for (auto* s : m_impl->streamManager->allStreams()) {
            if (s->isStreaming()) return true;
        }
        return false;
    });

    // Connect channels AFTER the streaming checker is set, so that YouTube
    // platforms can correctly gate on encoder activity in their poll loop.
    m_impl->channelManager->connectAllEnabled();

    // ── Web dashboard ────────────────────────────────────────────────────
    int webPort = cfg.get<int>("web.port", 8080);
    m_impl->webServer = std::make_unique<web::WebServer>(webPort, *this);
    m_impl->webServer->start();

    // ── Persist initial state to SQLite (first run migration) ────────────
    if (!hasSavedChannels || !hasSavedStreams || !hasSavedConfig || !hasSavedProfiles) {
        persistChannels();
        persistStreams();
        persistProfiles();
        persistGlobalConfig();
        spdlog::info("[SettingsDB] Initial state persisted to SQLite.");
    }

    spdlog::info("All subsystems initialised ({} stream(s), {} channel(s)).",
        m_impl->streamManager->count(),
        m_impl->channelManager->getAllChannels().size());
}

int Application::run() {
    try {
        initialize();
        mainLoop();
        shutdown();
        return 0;
    } catch (const std::exception& e) {
        spdlog::critical("Fatal error: {}", e.what());
        return 1;
    }
}

void Application::mainLoop() {
    m_impl->running = true;
    spdlog::info("Entering main loop at {} FPS target", m_impl->targetFps);

    using clock = std::chrono::high_resolution_clock;
    auto previousTime = clock::now();
    double accumulator = 0.0;
    const double dt = m_impl->fixedDeltaTime;

    while (m_impl->running) {
        auto currentTime = clock::now();
        double frameTime = std::chrono::duration<double>(currentTime - previousTime).count();
        previousTime = currentTime;
        if (frameTime > 0.25) frameTime = 0.25;
        accumulator += frameTime;

        // Poll ALL channel messages once per frame
        auto allMessages = m_impl->channelManager->pollAllMessages();

        // Fixed-timestep update
        while (accumulator >= dt) {
            for (auto* stream : m_impl->streamManager->allStreams()) {
                // Route messages to this stream's subscribed channels
                auto msgs = ChannelManager::filterByChannels(
                    allMessages, stream->config().channelIds);
                for (const auto& msg : msgs)
                    stream->handleChatMessage(msg);
                stream->update(dt);
            }
            accumulator -= dt;
            allMessages.clear(); // process only once
        }

        // Render & encode every stream
        double alpha = accumulator / dt;
        for (auto* stream : m_impl->streamManager->allStreams()) {
            stream->render(alpha);
            stream->encodeFrame();
        }

        // Preview: show selected stream in the SFML window (skip in headless)
        if (!m_impl->renderer->isHeadless()) {
            auto* previewStream = m_impl->streamManager->getStream(m_impl->previewStreamId);
            if (!previewStream) {
                auto all = m_impl->streamManager->allStreams();
                if (!all.empty()) previewStream = all[0];
            }
            if (previewStream) {
                m_impl->renderer->displayPreview(
                    previewStream->renderTexture().getTexture(),
                    previewStream->width(), previewStream->height());
            }
        }

        // Record performance sample (lightweight – no getState())
        {
            double fps = (frameTime > 0.0) ? (1.0 / frameTime) : 0.0;
            double ftMs = frameTime * 1000.0;
            int nStreams = 0;
            int nChannels = 0;
            int nPlayers = 0;
            for (auto* s : m_impl->streamManager->allStreams()) {
                if (s->isStreaming()) nStreams++;
                nPlayers += s->stats().uniqueViewerCount();
            }
            nChannels = m_impl->channelManager->connectedChannelCount();
            m_impl->perfMonitor->recordSample(fps, ftMs, nStreams, nChannels, nPlayers);
        }

        // Handle window events
        m_impl->renderer->processEvents([this](const sf::Event& event) {
            if (event.type == sf::Event::Closed) requestShutdown();
        });

        // Live headless toggle: check config flag each frame
        bool wantHeadless = m_impl->config->get<bool>("application.headless", false);
        m_impl->renderer->setHeadless(wantHeadless);

        // Update audio (auto-advance tracks, crossfade, clean finished sounds)
        if (m_impl->audioManager) m_impl->audioManager->update(frameTime);

        // Frame limiter: sleep to hit target FPS
        auto endTime = clock::now();
        double elapsed = std::chrono::duration<double>(endTime - currentTime).count();
        double targetFrameTime = 1.0 / static_cast<double>(m_impl->targetFps);
        if (elapsed < targetFrameTime) {
            auto sleepDuration = std::chrono::duration<double>(targetFrameTime - elapsed);
            std::this_thread::sleep_for(sleepDuration);
        }
    }
}

void Application::requestShutdown() {
    spdlog::info("Shutdown requested.");
    m_impl->running = false;
}

void Application::shutdown() {
    spdlog::info("Shutting down...");

    // Persist all settings to SQLite before stopping
    if (m_impl->settingsDb) {
        persistChannels();
        // Skip stream persistence when an import saved new streams to SQLite
        // but they were NOT loaded into the in-memory StreamManager (to avoid
        // OpenGL thread issues).  Without this guard the old in-memory state
        // would overwrite the freshly imported data.
        if (!m_impl->skipStreamPersistOnShutdown.load()) {
            persistStreams();
        } else {
            spdlog::info("[SettingsDB] Skipping stream persist (import pending restart).");
        }
        persistProfiles();
        persistGlobalConfig();
        spdlog::info("[SettingsDB] Settings persisted on shutdown.");
    }

    if (m_impl->audioManager) {
        m_impl->audioManager->stopMusic();
        spdlog::info("Audio stopped.");
    }
    if (m_impl->webServer)      m_impl->webServer->stop();
    if (m_impl->channelManager) m_impl->channelManager->disconnectAll();
    for (auto* s : m_impl->streamManager->allStreams()) s->stopStreaming();
    spdlog::info("Shutdown complete.");
}

Config&               Application::config()         { return *m_impl->config; }
ChannelManager&       Application::channelManager() { return *m_impl->channelManager; }
StreamManager&        Application::streamManager()  { return *m_impl->streamManager; }
ProfileManager&       Application::profileManager() { return *m_impl->profileManager; }
PlayerDatabase&       Application::playerDatabase() { return *m_impl->playerDatabase; }
SettingsDatabase&     Application::settingsDb()     { return *m_impl->settingsDb; }
PerfMonitor&          Application::perfMonitor()    { return *m_impl->perfMonitor; }
AudioManager&         Application::audioManager()   { return *m_impl->audioManager; }
AudioMixer&           Application::audioMixer()     { return *m_impl->audioMixer; }
rendering::Renderer&  Application::renderer()       { return *m_impl->renderer; }
web::WebServer&       Application::webServer()      { return *m_impl->webServer; }

void Application::persistChannels() {
    if (m_impl->settingsDb && m_impl->channelManager)
        m_impl->settingsDb->save("channels", m_impl->channelManager->toJson());
}

void Application::persistStreams() {
    if (m_impl->settingsDb && m_impl->streamManager)
        m_impl->settingsDb->save("streams", m_impl->streamManager->toJson());
}

void Application::persistProfiles() {
    if (m_impl->settingsDb && m_impl->profileManager)
        m_impl->settingsDb->save("profiles", m_impl->profileManager->toJson());
}

const GlobalScoreboardConfig& Application::scoreboardConfig() const {
    return m_scoreboardConfig;
}

void Application::setScoreboardConfig(const GlobalScoreboardConfig& cfg) {
    std::lock_guard<std::mutex> lock(m_scoreboardMutex);
    m_scoreboardConfig = cfg;
}

void Application::persistScoreboardConfig() {
    if (m_impl->settingsDb) {
        std::lock_guard<std::mutex> lock(m_scoreboardMutex);
        m_impl->settingsDb->save("scoreboard", m_scoreboardConfig.toJson());
    }
}

void Application::persistGlobalConfig() {
    if (!m_impl->settingsDb || !m_impl->config) return;
    // Store global config without channels/streams/profiles (those are stored separately)
    auto cfg = m_impl->config->raw();
    cfg.erase("channels");
    cfg.erase("streams");
    cfg.erase("profiles");
    m_impl->settingsDb->save("config", cfg);
}

void Application::setSkipStreamPersistOnShutdown(bool skip) {
    m_impl->skipStreamPersistOnShutdown.store(skip);
}

Application& Application::instance() { return *s_instance; }

} // namespace is::core
