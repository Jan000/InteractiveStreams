#pragma once

#include <string>
#include <memory>

namespace is::core {

class Config;
class GameManager;
class Logger;

}

namespace is::platform {
class PlatformManager;
}

namespace is::rendering {
class Renderer;
}

namespace is::streaming {
class StreamEncoder;
class StreamOutput;
}

namespace is::web {
class WebServer;
}

namespace is::core {

/// Main application class – bootstraps and owns all subsystems.
class Application {
public:
    Application(int argc, char* argv[]);
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    /// Run the main loop. Returns exit code.
    int run();

    /// Request a graceful shutdown.
    void requestShutdown();

    // Subsystem accessors
    Config&                       config();
    GameManager&                  gameManager();
    platform::PlatformManager&    platformManager();
    rendering::Renderer&          renderer();
    streaming::StreamEncoder&     streamEncoder();
    streaming::StreamOutput&      streamOutput();
    web::WebServer&               webServer();

    static Application& instance();

private:
    void initialize();
    void mainLoop();
    void shutdown();

    struct Impl;
    std::unique_ptr<Impl> m_impl;

    static Application* s_instance;
};

} // namespace is::core
