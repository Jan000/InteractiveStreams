#include "web/WebServer.h"
#include "web/ApiRoutes.h"
#include "core/Application.h"
#include <spdlog/spdlog.h>

namespace is::web {

WebServer::WebServer(int port, core::Application& app)
    : m_port(port)
    , m_app(app)
    , m_server(std::make_unique<httplib::Server>())
{
    setupRoutes();
}

WebServer::~WebServer() {
    stop();
}

void WebServer::setupRoutes() {
    // Serve static dashboard files
    m_server->set_mount_point("/", "dashboard");

    // Setup API routes
    ApiRoutes::setup(*m_server, m_app);

    spdlog::info("[WebServer] Routes configured.");
}

void WebServer::start() {
    m_running = true;
    m_thread = std::thread([this]() {
        spdlog::info("[WebServer] Starting on port {}...", m_port);
        m_server->listen("0.0.0.0", m_port);
    });
    spdlog::info("[WebServer] Admin dashboard available at http://localhost:{}", m_port);
}

void WebServer::stop() {
    if (m_running) {
        m_server->stop();
        m_running = false;
        if (m_thread.joinable()) {
            m_thread.join();
        }
        spdlog::info("[WebServer] Stopped.");
    }
}

} // namespace is::web
