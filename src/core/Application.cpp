#include "core/Application.h"
#include "core/Config.h"
#include "core/GameManager.h"
#include "core/Logger.h"
#include "platform/PlatformManager.h"
#include "rendering/Renderer.h"
#include "streaming/StreamEncoder.h"
#include "streaming/StreamOutput.h"
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
    std::unique_ptr<GameManager>               gameManager;
    std::unique_ptr<platform::PlatformManager> platformManager;
    std::unique_ptr<rendering::Renderer>       renderer;
    std::unique_ptr<streaming::StreamEncoder>  streamEncoder;
    std::unique_ptr<streaming::StreamOutput>   streamOutput;
    std::unique_ptr<web::WebServer>            webServer;

    int    targetFps    = 60;
    double fixedDeltaTime = 1.0 / 60.0;
};

Application::Application(int argc, char* argv[])
    : m_impl(std::make_unique<Impl>())
{
    s_instance = this;

    // Initialize logger first
    Logger::init();
    spdlog::info("InteractiveStreams v{}.{}.{} starting...", 0, 1, 0);

    // Load configuration
    std::string configPath = "config/default.json";
    if (argc > 1) {
        configPath = argv[1];
    }
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

    // Rendering
    int width  = cfg.get<int>("rendering.width", 1920);
    int height = cfg.get<int>("rendering.height", 1080);
    m_impl->renderer = std::make_unique<rendering::Renderer>(width, height,
        cfg.get<std::string>("rendering.title", "InteractiveStreams"));

    // Platform manager (chat connections)
    m_impl->platformManager = std::make_unique<platform::PlatformManager>(cfg);

    // Game manager
    m_impl->gameManager = std::make_unique<GameManager>();

    // Streaming
    m_impl->streamEncoder = std::make_unique<streaming::StreamEncoder>(cfg);
    m_impl->streamOutput  = std::make_unique<streaming::StreamOutput>(cfg);

    // Web dashboard
    int webPort = cfg.get<int>("web.port", 8080);
    m_impl->webServer = std::make_unique<web::WebServer>(webPort, *this);

    // Start web server in background
    m_impl->webServer->start();

    // Connect to enabled platforms
    m_impl->platformManager->connectAll();

    // Load the default game
    std::string defaultGame = cfg.get<std::string>("application.default_game", "chaos_arena");
    m_impl->gameManager->loadGame(defaultGame);

    spdlog::info("All subsystems initialized successfully.");
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

        // Cap frame time to avoid spiral of death
        if (frameTime > 0.25) frameTime = 0.25;
        accumulator += frameTime;

        // Process platform messages (chat)
        auto messages = m_impl->platformManager->pollMessages();
        for (auto& msg : messages) {
            m_impl->gameManager->handleChatMessage(msg);
        }

        // Fixed-timestep physics/game update
        while (accumulator >= dt) {
            m_impl->gameManager->update(dt);
            accumulator -= dt;
        }

        // Check for deferred game switch (after round/game end)
        m_impl->gameManager->checkPendingSwitch();

        // Render
        double alpha = accumulator / dt;
        m_impl->renderer->beginFrame();
        m_impl->gameManager->render(m_impl->renderer->getRenderTarget(), alpha);
        m_impl->renderer->endFrame();

        // Encode frame for streaming
        m_impl->streamEncoder->encodeFrame(m_impl->renderer->getFrameBuffer());

        // Handle window events (for local preview)
        m_impl->renderer->processEvents([this](const sf::Event& event) {
            if (event.type == sf::Event::Closed) {
                requestShutdown();
            }
        });
    }
}

void Application::requestShutdown() {
    spdlog::info("Shutdown requested.");
    m_impl->running = false;
}

void Application::shutdown() {
    spdlog::info("Shutting down...");
    m_impl->webServer->stop();
    m_impl->platformManager->disconnectAll();
    m_impl->streamEncoder->stop();
    spdlog::info("Shutdown complete.");
}

Config&                       Application::config()          { return *m_impl->config; }
GameManager&                  Application::gameManager()     { return *m_impl->gameManager; }
platform::PlatformManager&    Application::platformManager() { return *m_impl->platformManager; }
rendering::Renderer&          Application::renderer()        { return *m_impl->renderer; }
streaming::StreamEncoder&     Application::streamEncoder()   { return *m_impl->streamEncoder; }
streaming::StreamOutput&      Application::streamOutput()    { return *m_impl->streamOutput; }
web::WebServer&               Application::webServer()       { return *m_impl->webServer; }

Application& Application::instance() {
    return *s_instance;
}

} // namespace is::core
