#include "core/ChannelManager.h"
#include "platform/local/LocalPlatform.h"
#include "platform/twitch/TwitchPlatform.h"
#include "platform/youtube/YoutubePlatform.h"

#include <spdlog/spdlog.h>
#include <chrono>
#include <algorithm>

namespace is::core {

// ─────────────────────────────────────────────────────────────────────────────
ChannelManager::ChannelManager() {
    ensureLocalChannel();
}

ChannelManager::~ChannelManager() {
    disconnectAll();
}

// ── helpers ──────────────────────────────────────────────────────────────────

void ChannelManager::ensureLocalChannel() {
    for (const auto& ch : m_channels) {
        if (ch->config.id == "local") return;
    }
    ChannelConfig localCfg;
    localCfg.id       = "local";
    localCfg.platform = "local";
    localCfg.name     = "Local (Test)";
    localCfg.enabled  = true;
    localCfg.settings = {{"console_input", true}};

    // Call addChannel WITHOUT lock – mutex is not recursive.
    auto entry       = std::make_unique<ChannelEntry>();
    entry->config    = localCfg;
    entry->platform  = createPlatform("local");
    if (entry->platform) entry->platform->configure(localCfg.settings);
    m_channels.push_back(std::move(entry));
    spdlog::info("[ChannelManager] Local channel ensured.");
}

std::unique_ptr<platform::IPlatform>
ChannelManager::createPlatform(const std::string& platformType) {
    if (platformType == "local")   return std::make_unique<platform::LocalPlatform>();
    if (platformType == "twitch")  return std::make_unique<platform::TwitchPlatform>();
    if (platformType == "youtube") return std::make_unique<platform::YoutubePlatform>();
    spdlog::warn("[ChannelManager] Unknown platform type: '{}'", platformType);
    return nullptr;
}

// ── CRUD ─────────────────────────────────────────────────────────────────────

std::string ChannelManager::addChannel(const ChannelConfig& cfg) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto entry    = std::make_unique<ChannelEntry>();
    entry->config = cfg;

    // Generate ID if empty
    if (entry->config.id.empty()) {
        auto now = std::chrono::system_clock::now().time_since_epoch().count();
        entry->config.id = cfg.platform + "_" + std::to_string(now % 1000000);
    }

    entry->platform = createPlatform(cfg.platform);
    if (entry->platform && !cfg.settings.is_null()) {
        entry->platform->configure(cfg.settings);
    }

    std::string id = entry->config.id;
    spdlog::info("[ChannelManager] Channel added: '{}' ({})", id, cfg.platform);
    m_channels.push_back(std::move(entry));
    return id;
}

void ChannelManager::updateChannel(const std::string& id, const ChannelConfig& cfg) {
    std::unique_ptr<platform::IPlatform> oldPlatform;
    bool needReconnect = false;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& entry : m_channels) {
            if (entry->config.id == id) {
                bool wasConnected = entry->platform && entry->platform->isConnected();

                // Move old platform out – will be disconnected outside the lock
                oldPlatform = std::move(entry->platform);

                entry->config = cfg;
                entry->config.id = id; // preserve ID

                entry->platform = createPlatform(cfg.platform);
                if (entry->platform && !cfg.settings.is_null()) {
                    entry->platform->configure(cfg.settings);
                }
                needReconnect = (wasConnected && cfg.enabled && entry->platform != nullptr);
                spdlog::info("[ChannelManager] Channel updated: '{}'", id);
                break;
            }
        }
    }

    // Disconnect old platform OUTSIDE the lock (may block on thread join)
    if (oldPlatform) {
        oldPlatform->disconnect();
        oldPlatform.reset();
    }

    // Reconnect new platform outside the lock
    if (needReconnect) {
        connectChannel(id);
    }
}

void ChannelManager::removeChannel(const std::string& id) {
    if (id == "local") {
        spdlog::warn("[ChannelManager] Cannot remove the local channel.");
        return;
    }

    // Move channels to remove out of the list, then disconnect outside the lock
    std::vector<std::unique_ptr<ChannelEntry>> removed;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = std::remove_if(m_channels.begin(), m_channels.end(),
            [&id](const auto& e) { return e->config.id == id; });
        for (auto jt = it; jt != m_channels.end(); ++jt)
            removed.push_back(std::move(*jt));
        m_channels.erase(it, m_channels.end());
    }

    for (auto& entry : removed) {
        if (entry->platform && entry->platform->isConnected())
            entry->platform->disconnect();
        spdlog::info("[ChannelManager] Channel removed: '{}'", id);
    }
}

const ChannelConfig* ChannelManager::getChannelConfig(const std::string& id) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& e : m_channels)
        if (e->config.id == id) return &e->config;
    return nullptr;
}

std::vector<ChannelConfig> ChannelManager::getAllChannels() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<ChannelConfig> result;
    for (const auto& e : m_channels) result.push_back(e->config);
    return result;
}

// ── Connection management ────────────────────────────────────────────────────

void ChannelManager::connectChannel(const std::string& id) {
    platform::IPlatform* plat = nullptr;
    std::string name;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& e : m_channels) {
            if (e->config.id == id && e->platform) {
                plat = e->platform.get();
                name = e->config.name;
                break;
            }
        }
    }
    if (plat) {
        spdlog::info("[ChannelManager] Connecting '{}'...", name);
        plat->connect();
    }
}

void ChannelManager::disconnectChannel(const std::string& id) {
    platform::IPlatform* plat = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& e : m_channels) {
            if (e->config.id == id && e->platform) {
                plat = e->platform.get();
                break;
            }
        }
    }
    if (plat) {
        plat->disconnect();
    }
}

void ChannelManager::connectAllEnabled() {
    // Collect platforms to connect outside the lock
    struct ConnectInfo { platform::IPlatform* plat; std::string name; };
    std::vector<ConnectInfo> toConnect;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& e : m_channels) {
            if (e->config.enabled && e->platform && !e->platform->isConnected()) {
                toConnect.push_back({e->platform.get(), e->config.name});
            }
        }
    }
    for (auto& ci : toConnect) {
        spdlog::info("[ChannelManager] Connecting '{}'...", ci.name);
        if (ci.plat->connect())
            spdlog::info("[ChannelManager] '{}' connected.", ci.name);
        else
            spdlog::warn("[ChannelManager] Failed to connect '{}'.", ci.name);
    }
}

void ChannelManager::disconnectAll() {
    std::vector<platform::IPlatform*> toDisconnect;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& e : m_channels) {
            if (e->platform && e->platform->isConnected())
                toDisconnect.push_back(e->platform.get());
        }
    }
    for (auto* plat : toDisconnect) {
        plat->disconnect();
    }
}

// ── Message polling ──────────────────────────────────────────────────────────

std::vector<platform::ChatMessage> ChannelManager::pollAllMessages() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<platform::ChatMessage> all;
    for (auto& e : m_channels) {
        if (e->platform && e->platform->isConnected()) {
            auto msgs = e->platform->pollMessages();
            for (auto& m : msgs) {
                m.channelId = e->config.id;   // tag with our channel ID
                // Record per-channel statistics
                e->stats.recordMessage(m.userId, m.displayName);
            }
            all.insert(all.end(),
                std::make_move_iterator(msgs.begin()),
                std::make_move_iterator(msgs.end()));
        }
    }
    return all;
}

std::vector<platform::ChatMessage> ChannelManager::filterByChannels(
    const std::vector<platform::ChatMessage>& messages,
    const std::vector<std::string>& channelIds)
{
    if (channelIds.empty()) return messages; // no filter → all
    std::vector<platform::ChatMessage> out;
    for (const auto& msg : messages) {
        for (const auto& ch : channelIds) {
            if (msg.channelId == ch) { out.push_back(msg); break; }
        }
    }
    return out;
}

// ── Local test helpers ───────────────────────────────────────────────────────

void ChannelManager::injectLocalMessage(const std::string& username,
                                         const std::string& text) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& e : m_channels) {
        if (e->config.id == "local") {
            auto* lp = dynamic_cast<platform::LocalPlatform*>(e->platform.get());
            if (lp) lp->injectMessage(username, text);
            return;
        }
    }
}

std::vector<std::string> ChannelManager::getLocalMessageLog() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& e : m_channels) {
        if (e->config.id == "local") {
            auto* lp = dynamic_cast<platform::LocalPlatform*>(e->platform.get());
            if (lp) return lp->getMessageLog();
        }
    }
    return {};
}

platform::IPlatform* ChannelManager::getPlatform(const std::string& channelId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& e : m_channels)
        if (e->config.id == channelId) return e->platform.get();
    return nullptr;
}

bool ChannelManager::sendMessageToChannel(const std::string& channelId,
                                           const std::string& text) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& e : m_channels) {
        if (e->config.id == channelId && e->platform && e->platform->isConnected()) {
            return e->platform->sendMessage(text);
        }
    }
    spdlog::warn("[ChannelManager] Cannot send to '{}': not found or not connected.", channelId);
    return false;
}

int ChannelManager::sendMessageToAll(const std::string& text) {
    std::lock_guard<std::mutex> lock(m_mutex);
    int sent = 0;
    for (auto& e : m_channels) {
        if (e->platform && e->platform->isConnected()) {
            if (e->platform->sendMessage(text)) sent++;
        }
    }
    return sent;
}

// ── Status / serialisation ───────────────────────────────────────────────────

int ChannelManager::connectedChannelCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    int count = 0;
    for (const auto& e : m_channels) {
        if (e->platform && e->platform->isConnected()) ++count;
    }
    return count;
}

nlohmann::json ChannelManager::getStatus() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : m_channels) {
        nlohmann::json ch;
        ch["id"]       = e->config.id;
        ch["platform"] = e->config.platform;
        ch["name"]     = e->config.name;
        ch["enabled"]  = e->config.enabled;
        ch["connected"] = e->platform ? e->platform->isConnected() : false;
        // Include settings but redact secrets
        auto settings = e->config.settings;
        if (!settings.is_null()) {
            for (const char* key : {"oauth_token", "api_key", "stream_key",
                                    "oauth_client_secret", "oauth_refresh_token"}) {
                if (settings.contains(key) && settings[key].is_string()
                    && !settings[key].get<std::string>().empty())
                    settings[key] = "***";
            }
        }
        ch["settings"] = settings;
        if (e->platform) ch["details"] = e->platform->getStatus();
        ch["stats"] = e->stats.toJson();
        arr.push_back(ch);
    }
    return arr;
}

void ChannelManager::loadFromJson(const nlohmann::json& arr) {
    if (!arr.is_array()) return;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_channels.clear();
    }
    for (const auto& item : arr) {
        ChannelConfig cfg;
        cfg.id       = item.value("id", "");
        cfg.platform = item.value("platform", "");
        cfg.name     = item.value("name", "");
        cfg.enabled  = item.value("enabled", false);
        if (item.contains("settings")) cfg.settings = item["settings"];
        addChannel(cfg);
    }
    ensureLocalChannel();
}

nlohmann::json ChannelManager::toJson() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : m_channels) {
        nlohmann::json ch;
        ch["id"]       = e->config.id;
        ch["platform"] = e->config.platform;
        ch["name"]     = e->config.name;
        ch["enabled"]  = e->config.enabled;
        ch["settings"] = e->config.settings;
        arr.push_back(ch);
    }
    return arr;
}

// ── Statistics ───────────────────────────────────────────────────────────────

const ChannelStats* ChannelManager::getChannelStats(const std::string& channelId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& e : m_channels)
        if (e->config.id == channelId) return &e->stats;
    return nullptr;
}

nlohmann::json ChannelManager::getStatsJson() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : m_channels) {
        nlohmann::json ch;
        ch["channelId"] = e->config.id;
        ch["channelName"] = e->config.name;
        ch["platform"]  = e->config.platform;
        ch["connected"] = e->platform ? e->platform->isConnected() : false;
        ch["stats"]     = e->stats.toJson();
        arr.push_back(ch);
    }
    return arr;
}

void ChannelManager::resetChannelStats(const std::string& channelId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& e : m_channels) {
        if (e->config.id == channelId) {
            e->stats.reset();
            return;
        }
    }
}

void ChannelManager::resetAllStats() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& e : m_channels)
        e->stats.reset();
}

} // namespace is::core
