#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>

namespace is::core {

/// Initializes the global spdlog logger with console + file output.
class Logger {
public:
    static void init(const std::string& logFile = "logs/interactive_streams.log",
                     size_t maxSize = 5 * 1024 * 1024,
                     size_t maxFiles = 3);
};

} // namespace is::core
