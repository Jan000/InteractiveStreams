#pragma once

#include "core/StreamInstance.h"

#include <vector>
#include <memory>
#include <mutex>
#include <string>
#include <nlohmann/json.hpp>

namespace is::core {

/// Manages multiple StreamInstance objects.
class StreamManager {
public:
    StreamManager();
    ~StreamManager();

    // ── CRUD ─────────────────────────────────────────────────────────────

    /// Add a stream. Returns the (possibly generated) ID.
    std::string addStream(const StreamConfig& config);

    /// Update an existing stream.
    void updateStream(const std::string& id, const StreamConfig& config);

    /// Remove a stream.
    void removeStream(const std::string& id);

    /// Get stream by ID (nullptr if not found).
    StreamInstance*       getStream(const std::string& id);
    const StreamInstance* getStream(const std::string& id) const;

    // ── Iteration ────────────────────────────────────────────────────────

    std::vector<StreamInstance*>       allStreams();
    std::vector<const StreamInstance*> allStreams() const;
    size_t count() const;

    // ── Streaming control ────────────────────────────────────────────────

    void startStreaming(const std::string& id);
    void stopStreaming(const std::string& id);

    // ── Serialisation ────────────────────────────────────────────────────

    void loadFromJson(const nlohmann::json& arr);
    nlohmann::json toJson()    const;
    nlohmann::json getStatus() const;

private:
    mutable std::mutex m_mutex;
    std::vector<std::unique_ptr<StreamInstance>> m_streams;
};

} // namespace is::core
