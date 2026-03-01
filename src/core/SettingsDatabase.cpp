#include "core/SettingsDatabase.h"

#include <sqlite3.h>
#include <spdlog/spdlog.h>
#include <filesystem>

namespace is::core {

SettingsDatabase::SettingsDatabase() = default;

SettingsDatabase::~SettingsDatabase() {
    close();
}

bool SettingsDatabase::open(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Ensure directory exists
    auto dir = std::filesystem::path(path).parent_path();
    if (!dir.empty()) {
        std::filesystem::create_directories(dir);
    }

    int rc = sqlite3_open(path.c_str(), &m_db);
    if (rc != SQLITE_OK) {
        spdlog::error("[SettingsDB] Failed to open '{}': {}",
                      path, sqlite3_errmsg(m_db));
        m_db = nullptr;
        return false;
    }

    // WAL mode for better concurrency
    sqlite3_exec(m_db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);

    createTables();
    spdlog::info("[SettingsDB] Opened: {}", path);
    return true;
}

void SettingsDatabase::close() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_db) {
        sqlite3_close(m_db);
        m_db = nullptr;
        spdlog::info("[SettingsDB] Database closed.");
    }
}

void SettingsDatabase::createTables() {
    const char* sql = R"(
        CREATE TABLE IF NOT EXISTS settings (
            key  TEXT PRIMARY KEY,
            data TEXT NOT NULL
        );
    )";

    char* errmsg = nullptr;
    int rc = sqlite3_exec(m_db, sql, nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        spdlog::error("[SettingsDB] Failed to create tables: {}", errmsg ? errmsg : "unknown");
        if (errmsg) sqlite3_free(errmsg);
    }
}

// ── Generic key-value operations ─────────────────────────────────────────────

void SettingsDatabase::save(const std::string& key, const nlohmann::json& data) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_db) return;

    const char* sql = "INSERT OR REPLACE INTO settings (key, data) VALUES (?, ?);";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        spdlog::error("[SettingsDB] save prepare error: {}", sqlite3_errmsg(m_db));
        return;
    }

    std::string jsonStr = data.dump();
    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, jsonStr.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        spdlog::error("[SettingsDB] save step error for key '{}': {}", key, sqlite3_errmsg(m_db));
    }
    sqlite3_finalize(stmt);
}

nlohmann::json SettingsDatabase::load(const std::string& key) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_db) return nlohmann::json();

    const char* sql = "SELECT data FROM settings WHERE key = ?;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        spdlog::error("[SettingsDB] load prepare error: {}", sqlite3_errmsg(m_db));
        return nlohmann::json();
    }

    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);

    nlohmann::json result;
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        const char* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (text) {
            try {
                result = nlohmann::json::parse(text);
            } catch (const std::exception& e) {
                spdlog::error("[SettingsDB] Failed to parse JSON for key '{}': {}", key, e.what());
            }
        }
    }

    sqlite3_finalize(stmt);
    return result;
}

bool SettingsDatabase::has(const std::string& key) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_db) return false;

    const char* sql = "SELECT COUNT(*) FROM settings WHERE key = ?;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        found = sqlite3_column_int(stmt, 0) > 0;
    }
    sqlite3_finalize(stmt);
    return found;
}

void SettingsDatabase::remove(const std::string& key) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_db) return;

    const char* sql = "DELETE FROM settings WHERE key = ?;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return;

    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

} // namespace is::core
