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
    // Next.js is built with trailingSlash: true, so routes generate
    // <route>/index.html which httplib's mount point serves natively.
    m_server->set_mount_point("/", "dashboard");

    // Setup API routes
    ApiRoutes::setup(*m_server, m_app);

    // SPA fallback: for paths that the mount point can't serve directly,
    // try serving the .html file or the route/index.html file.
    m_server->set_error_handler([](const httplib::Request& req, httplib::Response& res) {
        if (res.status == 404 && req.method == "GET" &&
            req.path.find("/api/") == std::string::npos) {
            std::string path = req.path;
            if (!path.empty() && path[0] == '/') path = path.substr(1);
            if (!path.empty() && path.back() == '/') path.pop_back();
            if (path.empty()) return;

            // Handle Next.js RSC flight data files.
            // Browser requests e.g. performance/__next.performance.__PAGE__.txt
            // but the file lives at  performance/__next.performance/__PAGE__.txt
            // (the dot after the route segment is a directory separator).
            auto nextPos = path.find("__next.");
            if (nextPos != std::string::npos) {
                size_t afterPrefix = nextPos + 7; // skip "__next."
                std::string afterStr = path.substr(afterPrefix);
                auto dotPos = afterStr.find('.');
                if (dotPos != std::string::npos) {
                    std::string rewritten = path.substr(0, afterPrefix)
                        + afterStr.substr(0, dotPos) + "/"
                        + afterStr.substr(dotPos + 1);
                    std::string filePath = "dashboard/" + rewritten;
                    std::ifstream f(filePath, std::ios::binary);
                    if (f.good()) {
                        std::string content((std::istreambuf_iterator<char>(f)),
                                             std::istreambuf_iterator<char>());
                        res.status = 200;
                        res.set_content(content, "text/plain");
                        return;
                    }
                }
            }

            // Try route/index.html (trailingSlash: true output)
            std::string indexFile = "dashboard/" + path + "/index.html";
            std::ifstream f1(indexFile, std::ios::binary);
            if (f1.good()) {
                std::string content((std::istreambuf_iterator<char>(f1)),
                                     std::istreambuf_iterator<char>());
                res.status = 200;
                res.set_content(content, "text/html");
                return;
            }

            // Try route.html (fallback for non-trailingSlash output)
            std::string htmlFile = "dashboard/" + path + ".html";
            std::ifstream f2(htmlFile, std::ios::binary);
            if (f2.good()) {
                std::string content((std::istreambuf_iterator<char>(f2)),
                                     std::istreambuf_iterator<char>());
                res.status = 200;
                res.set_content(content, "text/html");
                return;
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
