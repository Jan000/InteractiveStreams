#pragma once

#include "platform/ChatMessage.h"
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace is::platform {

/// Abstract interface for streaming platform integrations.
/// Implement this for each platform (Twitch, YouTube, etc.).
class IPlatform {
public:
    virtual ~IPlatform() = default;

    /// Unique platform identifier.
    virtual std::string id() const = 0;

    /// Human-readable platform name.
    virtual std::string displayName() const = 0;

    /// Connect to the platform's chat.
    virtual bool connect() = 0;

    /// Disconnect from the platform.
    virtual void disconnect() = 0;

    /// Check if currently connected.
    virtual bool isConnected() const = 0;

    /// Poll for new chat messages (non-blocking).
    virtual std::vector<ChatMessage> pollMessages() = 0;

    /// Send a message to the chat.
    virtual bool sendMessage(const std::string& text) = 0;

    /// Get platform status as JSON.
    virtual nlohmann::json getStatus() const = 0;

    /// Configure the platform from JSON settings.
    virtual void configure(const nlohmann::json& settings) = 0;

    /// Return the platform's current live settings (e.g. refreshed tokens).
    /// Default returns an empty object; override if settings can change at runtime.
    virtual nlohmann::json getCurrentSettings() const { return nlohmann::json::object(); }
};

} // namespace is::platform
