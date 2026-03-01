#pragma once

#include <string>
#include <memory>

namespace is::core {

class Config;
class ChannelManager;
class StreamManager;
class Logger;
class PlayerDatabase;
class PerfMonitor;

}

namespace is::rendering {
class Renderer;
}

namespace is::web {
class WebServer;
}

namespace is::core {

/// Main application class – bootstraps and owns all subsystems.
/// In multi-stream mode it manages multiple game/render/encode pipelines
/// via StreamManager, and multiple chat platform connections via ChannelManager.
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
    Config&               config();
    ChannelManager&       channelManager();
    StreamManager&        streamManager();
    PlayerDatabase&       playerDatabase();
    PerfMonitor&          perfMonitor();
    rendering::Renderer&  renderer();
    web::WebServer&       webServer();

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
