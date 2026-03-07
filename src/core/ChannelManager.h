#pragma once

#include "platform/IPlatform.h"
#include "platform/ChatMessage.h"
#include "core/ChannelStats.h"

#include <memory>
#include <vector>
#include <string>
#include <mutex>
#include <functional>
#include <nlohmann/json.hpp>

namespace is::core {

/// Configuration for a single chat channel (platform connection).
struct ChannelConfig {
    std::string id;
    std::string platform;   ///< "local", "twitch", "youtube"
    std::string name;       ///< Display name
    bool enabled = false;
    nlohmann::json settings; ///< Platform-specific settings
};

/// Manages multiple platform channel connections.
/// Replaces the old PlatformManager by supporting multiple instances of
/// the same platform type (e.g. two Twitch channels simultaneously).
class ChannelManager {
public:
    ChannelManager();
    ~ChannelManager();

    // ── Channel CRUD ─────────────────────────────────────────────────────

    /// Add a channel. Returns the (possibly generated) channel ID.
    std::string addChannel(const ChannelConfig& cfg);

    /// Update an existing channel by ID.
    void updateChannel(const std::string& id, const ChannelConfig& cfg);

    /// Remove a channel by ID (cannot remove "local").
    void removeChannel(const std::string& id);

    /// Get a channel config by ID (nullptr if not found).
    const ChannelConfig* getChannelConfig(const std::string& id) const;

    /// Get all channel configs.
    std::vector<ChannelConfig> getAllChannels() const;

    // ── Connection management ────────────────────────────────────────────

    void connectChannel(const std::string& id);
    void disconnectChannel(const std::string& id);
    void connectAllEnabled();
    void disconnectAll();

    // ── Message polling ──────────────────────────────────────────────────

    /// Poll messages from all connected channels. Each message's channelId
    /// is overwritten with the channel manager entry ID.
    std::vector<platform::ChatMessage> pollAllMessages();

    /// Filter a message list to only those from the given channel IDs.
    static std::vector<platform::ChatMessage> filterByChannels(
        const std::vector<platform::ChatMessage>& messages,
        const std::vector<std::string>& channelIds);

    // ── Local test helper ────────────────────────────────────────────────

    void injectLocalMessage(const std::string& username, const std::string& text);
    std::vector<std::string> getLocalMessageLog() const;

    // ── Send messages to channels ────────────────────────────────────────

    /// Send a message to a specific channel.
    bool sendMessageToChannel(const std::string& channelId, const std::string& text);

    /// Send a message to all connected channels.
    int sendMessageToAll(const std::string& text);

    // ── Platform access ──────────────────────────────────────────────────

    platform::IPlatform* getPlatform(const std::string& channelId);

    /// Set a callback used by YouTube platforms to determine whether at least
    /// one stream is actively encoding.  Auto-detection of the liveChatId is
    /// deferred until the checker returns true.
    void setStreamingChecker(std::function<bool()> checker);

    // ── Status / serialisation ───────────────────────────────────────────

    nlohmann::json getStatus() const;

    /// Lightweight: number of currently connected channels (no JSON alloc).
    int connectedChannelCount() const;
    void loadFromJson(const nlohmann::json& arr);
    nlohmann::json toJson() const;

    // ── Statistics ───────────────────────────────────────────────────────

    /// Get stats for a specific channel (nullptr if not found).
    const ChannelStats* getChannelStats(const std::string& channelId) const;

    /// Get stats JSON for all channels.
    nlohmann::json getStatsJson() const;

    /// Reset stats for a specific channel.
    void resetChannelStats(const std::string& channelId);

    /// Reset stats for all channels.
    void resetAllStats();

private:
    std::unique_ptr<platform::IPlatform> createPlatform(const std::string& platformType);
    void ensureLocalChannel();

    struct ChannelEntry {
        ChannelConfig config;
        std::unique_ptr<platform::IPlatform> platform;
        ChannelStats stats;
    };

    mutable std::mutex m_mutex;
    std::vector<std::unique_ptr<ChannelEntry>> m_channels;
    std::function<bool()> m_streamingChecker;  ///< Passed to YouTube platforms
};

} // namespace is::core
