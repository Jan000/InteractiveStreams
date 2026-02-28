#pragma once

#include "platform/IPlatform.h"
#include "platform/ChatMessage.h"
#include "core/Config.h"

#include <memory>
#include <vector>
#include <unordered_map>

namespace is::platform {

/// Manages all platform connections and aggregates chat messages.
class PlatformManager {
public:
    explicit PlatformManager(core::Config& config);
    ~PlatformManager();

    /// Register a platform implementation.
    void registerPlatform(std::unique_ptr<IPlatform> platform);

    /// Connect all enabled platforms.
    void connectAll();

    /// Disconnect all platforms.
    void disconnectAll();

    /// Poll messages from all connected platforms.
    std::vector<ChatMessage> pollMessages();

    /// Send a message to all connected platforms.
    void broadcastMessage(const std::string& text);

    /// Get platform by ID.
    IPlatform* getPlatform(const std::string& id);

    /// Get all platforms.
    const std::vector<std::unique_ptr<IPlatform>>& platforms() const { return m_platforms; }

    /// Get status of all platforms.
    nlohmann::json getStatus() const;

private:
    std::vector<std::unique_ptr<IPlatform>> m_platforms;
    core::Config& m_config;
};

} // namespace is::platform
