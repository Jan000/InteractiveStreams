#pragma once

#include "core/ScoreboardConfig.h"
#include <string>
#include <memory>
#include <mutex>

namespace is::core {

class Config;
class ChannelManager;
class StreamManager;
class ProfileManager;
class Logger;
class PlayerDatabase;
class PerfMonitor;
class SettingsDatabase;
class AudioManager;
class AudioMixer;

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
    ProfileManager&       profileManager();
    PlayerDatabase&       playerDatabase();
    SettingsDatabase&     settingsDb();
    PerfMonitor&          perfMonitor();
    AudioManager&         audioManager();
    AudioMixer&           audioMixer();
    rendering::Renderer&  renderer();
    web::WebServer&       webServer();

    /// Persist channels to SQLite (call after any channel CRUD).
    void persistChannels();
    /// Persist streams to SQLite (call after any stream CRUD).
    void persistStreams();
    /// Persist profiles to SQLite (call after any profile CRUD).
    void persistProfiles();
    /// Persist global config to SQLite (call after settings change).
    void persistGlobalConfig();

    /// Global scoreboard configuration (shared by all streams).
    const GlobalScoreboardConfig& scoreboardConfig() const;
    void setScoreboardConfig(const GlobalScoreboardConfig& cfg);
    void persistScoreboardConfig();

    /// Tell shutdown() to skip persistStreams() because an import has already
    /// written new stream data to SQLite that is not yet in memory.
    void setSkipStreamPersistOnShutdown(bool skip);

    static Application& instance();

private:
    void initialize();
    void mainLoop();
    void shutdown();

    struct Impl;
    std::unique_ptr<Impl> m_impl;

    GlobalScoreboardConfig m_scoreboardConfig;
    mutable std::mutex     m_scoreboardMutex;

    static Application* s_instance;
};

} // namespace is::core
