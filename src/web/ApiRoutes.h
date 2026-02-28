#pragma once

#include <httplib.h>

namespace is::core { class Application; }

namespace is::web {

/// Configures all REST API routes for the admin dashboard.
class ApiRoutes {
public:
    static void setup(httplib::Server& server, core::Application& app);
};

} // namespace is::web
