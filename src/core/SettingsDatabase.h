#pragma once

#include <string>
#include <mutex>
#include <nlohmann/json.hpp>

struct sqlite3;

namespace is::core {

/// Persistent settings storage backed by SQLite.
/// Stores global config, channel configs, and stream configs
/// so that ALL settings survive application restarts.
/// Thread-safe: all public methods lock an internal mutex.
class SettingsDatabase {
public:
    SettingsDatabase();
    ~SettingsDatabase();

    /// Open (or create) the database file.
    bool open(const std::string& path = "data/settings.db");

    /// Close the database.
    void close();

    // ── Generic key-value ────────────────────────────────────────────────

    /// Save a JSON value under a key (UPSERT).
    void save(const std::string& key, const nlohmann::json& data);

    /// Load a JSON value by key. Returns null JSON if not found.
    nlohmann::json load(const std::string& key) const;

    /// Check whether a key exists.
    bool has(const std::string& key) const;

    /// Remove a key.
    void remove(const std::string& key);

private:
    void createTables();

    sqlite3*       m_db = nullptr;
    mutable std::mutex m_mutex;
};

} // namespace is::core
