#include "core/Logger.h"
#include <filesystem>

namespace is::core {

void Logger::init(const std::string& logFile, size_t maxSize, size_t maxFiles) {
    // Ensure log directory exists
    auto logDir = std::filesystem::path(logFile).parent_path();
    if (!logDir.empty()) {
        std::filesystem::create_directories(logDir);
    }

    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    consoleSink->set_level(spdlog::level::info);
    consoleSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");

    auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        logFile, maxSize, maxFiles);
    fileSink->set_level(spdlog::level::debug);
    fileSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] [%s:%#] %v");

    auto logger = std::make_shared<spdlog::logger>("main",
        spdlog::sinks_init_list{consoleSink, fileSink});
    logger->set_level(spdlog::level::debug);
    logger->flush_on(spdlog::level::warn);

    spdlog::set_default_logger(logger);
    spdlog::info("Logger initialized.");
}

} // namespace is::core
