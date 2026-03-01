#include "web/WebServer.h"
#include "web/ApiRoutes.h"
#include "core/Application.h"
#include <spdlog/spdlog.h>
#include <fstream>

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

    // SPA fallback: for Next.js static-export routes that don't match a physical
    // file, serve the corresponding .html file (e.g. /channels → channels.html).
    // This must come after API routes so /api/* is handled first.
    m_server->set_error_handler([](const httplib::Request& req, httplib::Response& res) {
        if (res.status == 404 && req.method == "GET" &&
            req.path.find("/api/") == std::string::npos) {
            // Try serving <path>.html from the dashboard directory
            std::string path = req.path;
            // Strip leading slash
            if (!path.empty() && path[0] == '/') path = path.substr(1);
            // Strip trailing slash
            if (!path.empty() && path.back() == '/') path.pop_back();

            if (path.empty()) return;  // root is handled by index.html

            std::string htmlFile = "dashboard/" + path + ".html";
            std::ifstream file(htmlFile, std::ios::binary);
            if (file.good()) {
                std::string content((std::istreambuf_iterator<char>(file)),
                                     std::istreambuf_iterator<char>());
                res.status = 200;
                res.set_content(content, "text/html");
            }
        }
    });

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
