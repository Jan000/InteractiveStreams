#include "platform/PlatformManager.h"
#include "platform/local/LocalPlatform.h"
#include "platform/twitch/TwitchPlatform.h"
#include "platform/youtube/YoutubePlatform.h"

#include <spdlog/spdlog.h>

namespace is::platform {

PlatformManager::PlatformManager(core::Config& config)
    : m_config(config)
{
    // Auto-register built-in platforms

    // Local test platform (always registered, auto-enabled)
    auto local = std::make_unique<LocalPlatform>();
    if (config.raw().contains("platforms") && config.raw()["platforms"].contains("local")) {
        local->configure(config.raw()["platforms"]["local"]);
    }
    registerPlatform(std::move(local));

    auto twitch = std::make_unique<TwitchPlatform>();
    if (config.raw().contains("platforms") && config.raw()["platforms"].contains("twitch")) {
        twitch->configure(config.raw()["platforms"]["twitch"]);
    }
    registerPlatform(std::move(twitch));

    auto youtube = std::make_unique<YoutubePlatform>();
    if (config.raw().contains("platforms") && config.raw()["platforms"].contains("youtube")) {
        youtube->configure(config.raw()["platforms"]["youtube"]);
    }
    registerPlatform(std::move(youtube));

    spdlog::info("PlatformManager initialized with {} platforms.", m_platforms.size());
}

PlatformManager::~PlatformManager() {
    disconnectAll();
}

void PlatformManager::registerPlatform(std::unique_ptr<IPlatform> platform) {
    spdlog::info("Platform registered: '{}'", platform->displayName());
    m_platforms.push_back(std::move(platform));
}

void PlatformManager::connectAll() {
    for (auto& platform : m_platforms) {
        // Local platform defaults to enabled (for testing)
        bool defaultEnabled = (platform->id() == "local");
        bool enabled = m_config.get<bool>("platforms." + platform->id() + ".enabled", defaultEnabled);
        if (enabled) {
            spdlog::info("Connecting to {}...", platform->displayName());
            if (platform->connect()) {
                spdlog::info("{} connected successfully.", platform->displayName());
            } else {
                spdlog::warn("Failed to connect to {}.", platform->displayName());
            }
        } else {
            spdlog::info("{} is disabled, skipping.", platform->displayName());
        }
    }
}

void PlatformManager::disconnectAll() {
    for (auto& platform : m_platforms) {
        if (platform->isConnected()) {
            platform->disconnect();
            spdlog::info("{} disconnected.", platform->displayName());
        }
    }
}

std::vector<ChatMessage> PlatformManager::pollMessages() {
    std::vector<ChatMessage> allMessages;
    for (auto& platform : m_platforms) {
        if (platform->isConnected()) {
            auto messages = platform->pollMessages();
            allMessages.insert(allMessages.end(),
                std::make_move_iterator(messages.begin()),
                std::make_move_iterator(messages.end()));
        }
    }
    return allMessages;
}

void PlatformManager::broadcastMessage(const std::string& text) {
    for (auto& platform : m_platforms) {
        if (platform->isConnected()) {
            platform->sendMessage(text);
        }
    }
}

IPlatform* PlatformManager::getPlatform(const std::string& id) {
    for (auto& platform : m_platforms) {
        if (platform->id() == id) return platform.get();
    }
    return nullptr;
}

nlohmann::json PlatformManager::getStatus() const {
    nlohmann::json status = nlohmann::json::array();
    for (const auto& platform : m_platforms) {
        status.push_back(platform->getStatus());
    }
    return status;
}

} // namespace is::platform
