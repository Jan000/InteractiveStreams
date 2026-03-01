#include "core/Application.h"
#include "core/Config.h"
#include "core/ChannelManager.h"
#include "core/StreamManager.h"
#include "core/Logger.h"
#include "rendering/Renderer.h"
#include "web/WebServer.h"

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
    std::unique_ptr<ChannelManager>            channelManager;
    std::unique_ptr<StreamManager>             streamManager;
    std::unique_ptr<rendering::Renderer>       renderer;
    std::unique_ptr<web::WebServer>            webServer;

    int    targetFps      = 60;
    double fixedDeltaTime = 1.0 / 60.0;
    std::string previewStreamId;
};

Application::Application(int argc, char* argv[])
    : m_impl(std::make_unique<Impl>())
{
    s_instance = this;

    Logger::init();
    spdlog::info("InteractiveStreams v{}.{}.{} starting...", 0, 2, 0);

    std::string configPath = "config/default.json";
    if (argc > 1) configPath = argv[1];

    m_impl->config = std::make_unique<Config>(configPath);
    m_impl->targetFps = m_impl->config->get<int>("application.target_fps", 60);
    m_impl->fixedDeltaTime = 1.0 / static_cast<double>(m_impl->targetFps);
}

Application::~Application() {
    s_instance = nullptr;
}

void Application::initialize() {
    spdlog::info("Initializing subsystems...");
    auto& cfg = *m_impl->config;

    // ── Channel manager (replaces PlatformManager) ───────────────────────
    m_impl->channelManager = std::make_unique<ChannelManager>();
    if (cfg.raw().contains("channels") && cfg.raw()["channels"].is_array()) {
        m_impl->channelManager->loadFromJson(cfg.raw()["channels"]);
    }
    m_impl->channelManager->connectAllEnabled();

    // ── Preview renderer (must be created BEFORE streams, because
    //    GameManager::loadGame() accesses the renderer for setWindowTitle) ─
    bool headless = cfg.get<bool>("application.headless", false);
    m_impl->renderer = std::make_unique<rendering::Renderer>(1080, 1920,
        cfg.get<std::string>("rendering.title", "InteractiveStreams"),
        headless);

    // ── Stream manager (replaces single GameManager+Renderer+Encoder) ────
    m_impl->streamManager = std::make_unique<StreamManager>();
    if (cfg.raw().contains("streams") && cfg.raw()["streams"].is_array()) {
        m_impl->streamManager->loadFromJson(cfg.raw()["streams"]);
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

    // ── Web dashboard ────────────────────────────────────────────────────
    int webPort = cfg.get<int>("web.port", 8080);
    m_impl->webServer = std::make_unique<web::WebServer>(webPort, *this);
    m_impl->webServer->start();

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

        // Preview: show selected stream in the SFML window
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

        // Handle window events
        m_impl->renderer->processEvents([this](const sf::Event& event) {
            if (event.type == sf::Event::Closed) requestShutdown();
        });
    }
}

void Application::requestShutdown() {
    spdlog::info("Shutdown requested.");
    m_impl->running = false;
}

void Application::shutdown() {
    spdlog::info("Shutting down...");
    if (m_impl->webServer)      m_impl->webServer->stop();
    if (m_impl->channelManager) m_impl->channelManager->disconnectAll();
    for (auto* s : m_impl->streamManager->allStreams()) s->stopStreaming();
    spdlog::info("Shutdown complete.");
}

Config&               Application::config()         { return *m_impl->config; }
ChannelManager&       Application::channelManager() { return *m_impl->channelManager; }
StreamManager&        Application::streamManager()  { return *m_impl->streamManager; }
rendering::Renderer&  Application::renderer()       { return *m_impl->renderer; }
web::WebServer&       Application::webServer()      { return *m_impl->webServer; }

Application& Application::instance() { return *s_instance; }

} // namespace is::core
