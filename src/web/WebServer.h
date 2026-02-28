#pragma once

#include <httplib.h>
#include <thread>
#include <atomic>
#include <memory>

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

private:
    void setupRoutes();

    int                          m_port;
    core::Application&           m_app;
    std::unique_ptr<httplib::Server> m_server;
    std::thread                  m_thread;
    std::atomic<bool>            m_running{false};
};

} // namespace is::web
