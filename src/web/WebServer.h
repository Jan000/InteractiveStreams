#pragma once

#include <httplib.h>
#include <thread>
#include <atomic>
#include <memory>
#include <string>
#include <unordered_set>
#include <mutex>

namespace is::core { class Application; }

namespace is::web {

/// Embedded HTTP server serving the admin dashboard and REST API.
class WebServer {
public:
    WebServer(int port, core::Application& app);
    ~WebServer();

    /// Start the server in a background thread.
    void start();

    /// Stop the server.
    void stop();

    /// Check if running.
    bool isRunning() const { return m_running; }

    /// Get the port.
    int port() const { return m_port; }

    /// Reload the API key from config (call after settings change).
    void reloadApiKey();

    // ── Session management ──────────────────────────────────────────────

    /// Add a session token (called by auth login route).
    void addSession(const std::string& token);

    /// Remove a session token (called by auth logout route).
    void removeSession(const std::string& token);

    /// Check if a session token is valid.
    bool hasSession(const std::string& token) const;

    /// Check if password-based auth is enabled (password hash exists).
    bool isPasswordAuthEnabled() const;

private:
    void setupRoutes();
    void setupAuth();

    int                          m_port;
    core::Application&           m_app;
    std::unique_ptr<httplib::Server> m_server;
    std::thread                  m_thread;
    std::atomic<bool>            m_running{false};
    std::string                  m_apiKey;   ///< empty = no auth

    // Session tokens (in-memory, cleared on restart)
    mutable std::mutex           m_sessionMutex;
    std::unordered_set<std::string> m_sessions;
};

} // namespace is::web
