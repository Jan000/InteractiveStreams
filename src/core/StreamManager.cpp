#include "core/StreamManager.h"
#include <spdlog/spdlog.h>
#include <chrono>
#include <algorithm>

namespace is::core {

StreamManager::StreamManager()  = default;
StreamManager::~StreamManager() = default;

// ── CRUD ─────────────────────────────────────────────────────────────────────

std::string StreamManager::addStream(const StreamConfig& config) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto cfg = config;
    if (cfg.id.empty()) {
        auto now = std::chrono::system_clock::now().time_since_epoch().count();
        cfg.id = "stream_" + std::to_string(now % 1000000);
    }

    std::string id = cfg.id;
    m_streams.push_back(std::make_unique<StreamInstance>(cfg));
    spdlog::info("[StreamManager] Stream added: '{}' ('{}')", id, cfg.name);
    return id;
}

void StreamManager::updateStream(const std::string& id,
                                  const StreamConfig& config) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& s : m_streams) {
        if (s->config().id == id) {
            s->updateConfig(config);
            spdlog::info("[StreamManager] Stream updated: '{}'", id);
            return;
        }
    }
    spdlog::warn("[StreamManager] Stream not found: '{}'", id);
}

void StreamManager::removeStream(const std::string& id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = std::remove_if(m_streams.begin(), m_streams.end(),
        [&id](const auto& s) { return s->config().id == id; });
    if (it != m_streams.end()) {
        m_streams.erase(it, m_streams.end());
        spdlog::info("[StreamManager] Stream removed: '{}'", id);
    }
}

StreamInstance* StreamManager::getStream(const std::string& id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& s : m_streams)
        if (s->config().id == id) return s.get();
    return nullptr;
}

const StreamInstance* StreamManager::getStream(const std::string& id) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& s : m_streams)
        if (s->config().id == id) return s.get();
    return nullptr;
}

// ── Iteration ────────────────────────────────────────────────────────────────

std::vector<StreamInstance*> StreamManager::allStreams() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<StreamInstance*> out;
    out.reserve(m_streams.size());
    for (auto& s : m_streams) out.push_back(s.get());
    return out;
}

std::vector<const StreamInstance*> StreamManager::allStreams() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<const StreamInstance*> out;
    out.reserve(m_streams.size());
    for (const auto& s : m_streams) out.push_back(s.get());
    return out;
}

size_t StreamManager::count() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_streams.size();
}

// ── Streaming control ────────────────────────────────────────────────────────

std::string StreamManager::startStreaming(const std::string& id) {
    auto* s = getStream(id);
    if (!s) return "Stream not found";
    return s->startStreaming();
}

void StreamManager::stopStreaming(const std::string& id) {
    auto* s = getStream(id);
    if (s) s->stopStreaming();
}

// ── Serialisation ────────────────────────────────────────────────────────────

void StreamManager::loadFromJson(const nlohmann::json& arr) {
    if (!arr.is_array()) return;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_streams.clear();
    }
    for (const auto& item : arr) {
        addStream(StreamInstance::configFromJson(item));
    }
    // Ensure at least one default stream
    if (count() == 0) {
        StreamConfig def;
        def.id         = "default";
        def.name       = "Main Stream";
        def.channelIds = {"local"};
        addStream(def);
    }
}

nlohmann::json StreamManager::toJson() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& s : m_streams) arr.push_back(s->toJson());
    return arr;
}

nlohmann::json StreamManager::getStatus() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& s : m_streams) arr.push_back(s->getState());
    return arr;
}

} // namespace is::core
