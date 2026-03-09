#include "core/PlayerDatabase.h"

#include <sqlite3.h>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <chrono>

namespace is::core {

PlayerDatabase::PlayerDatabase() = default;

PlayerDatabase::~PlayerDatabase() {
    close();
}

bool PlayerDatabase::open(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Ensure directory exists
    auto dir = std::filesystem::path(path).parent_path();
    if (!dir.empty()) {
        std::filesystem::create_directories(dir);
    }

    int rc = sqlite3_open(path.c_str(), &m_db);
    if (rc != SQLITE_OK) {
        spdlog::error("[PlayerDB] Failed to open database '{}': {}",
                      path, sqlite3_errmsg(m_db));
        m_db = nullptr;
        return false;
    }

    // Enable WAL mode for better concurrency
    sqlite3_exec(m_db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);

    createTables();
    spdlog::info("[PlayerDB] Database opened: {}", path);
    return true;
}

void PlayerDatabase::close() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_db) {
        sqlite3_close(m_db);
        m_db = nullptr;
        spdlog::info("[PlayerDB] Database closed.");
    }
}

void PlayerDatabase::createTables() {
    const char* sql = R"(
        CREATE TABLE IF NOT EXISTS players (
            user_id      TEXT PRIMARY KEY,
            display_name TEXT NOT NULL,
            total_points INTEGER DEFAULT 0,
            total_wins   INTEGER DEFAULT 0,
            games_played INTEGER DEFAULT 0,
            created_at   REAL DEFAULT (strftime('%s','now')),
            updated_at   REAL DEFAULT (strftime('%s','now'))
        );

        CREATE TABLE IF NOT EXISTS game_results (
            id           INTEGER PRIMARY KEY AUTOINCREMENT,
            user_id      TEXT NOT NULL,
            display_name TEXT NOT NULL,
            game_name    TEXT NOT NULL,
            points       INTEGER NOT NULL,
            is_win       INTEGER NOT NULL DEFAULT 0,
            timestamp    REAL DEFAULT (strftime('%s','now')),
            FOREIGN KEY (user_id) REFERENCES players(user_id)
        );

        CREATE INDEX IF NOT EXISTS idx_results_timestamp ON game_results(timestamp);
        CREATE INDEX IF NOT EXISTS idx_results_user ON game_results(user_id);
        CREATE INDEX IF NOT EXISTS idx_players_points ON players(total_points DESC);
    )";

    char* errmsg = nullptr;
    int rc = sqlite3_exec(m_db, sql, nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        spdlog::error("[PlayerDB] Failed to create tables: {}", errmsg ? errmsg : "unknown");
        sqlite3_free(errmsg);
    }
}

void PlayerDatabase::recordResult(const std::string& userId,
                                   const std::string& displayName,
                                   const std::string& gameName,
                                   int points,
                                   bool isWin) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_db) return;

    double now = std::chrono::duration<double>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    // Upsert player
    const char* upsertPlayer = R"(
        INSERT INTO players (user_id, display_name, total_points, total_wins, games_played, updated_at)
        VALUES (?, ?, ?, ?, 1, ?)
        ON CONFLICT(user_id) DO UPDATE SET
            display_name = excluded.display_name,
            total_points = total_points + excluded.total_points,
            total_wins   = total_wins + excluded.total_wins,
            games_played = games_played + 1,
            updated_at   = excluded.updated_at;
    )";

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(m_db, upsertPlayer, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, userId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, displayName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, points);
    sqlite3_bind_int(stmt, 4, isWin ? 1 : 0);
    sqlite3_bind_double(stmt, 5, now);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    // Insert game result
    const char* insertResult = R"(
        INSERT INTO game_results (user_id, display_name, game_name, points, is_win, timestamp)
        VALUES (?, ?, ?, ?, ?, ?);
    )";

    sqlite3_prepare_v2(m_db, insertResult, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, userId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, displayName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, gameName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, points);
    sqlite3_bind_int(stmt, 5, isWin ? 1 : 0);
    sqlite3_bind_double(stmt, 6, now);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    spdlog::debug("[PlayerDB] Recorded: {} ({}) — {} pts, win={} in {}",
                  displayName, userId, points, isWin, gameName);
}

std::vector<ScoreEntry> PlayerDatabase::getTopRecent(int limit, int hours) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<ScoreEntry> results;
    if (!m_db) return results;

    double cutoff = std::chrono::duration<double>(
        std::chrono::system_clock::now().time_since_epoch()).count() - (hours * 3600.0);

    const char* sql = R"(
        SELECT user_id, display_name,
               SUM(points) as total_pts,
               SUM(is_win) as wins,
               COUNT(*) as games,
               MAX(timestamp) as last_played
        FROM game_results
        WHERE timestamp >= ?
        GROUP BY user_id
        ORDER BY total_pts DESC, wins DESC
        LIMIT ?;
    )";

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
    sqlite3_bind_double(stmt, 1, cutoff);
    sqlite3_bind_int(stmt, 2, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ScoreEntry e;
        e.userId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        e.displayName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        e.points = sqlite3_column_int(stmt, 2);
        e.wins = sqlite3_column_int(stmt, 3);
        e.gamesPlayed = sqlite3_column_int(stmt, 4);
        e.timestamp = sqlite3_column_double(stmt, 5);
        results.push_back(std::move(e));
    }
    sqlite3_finalize(stmt);

    return results;
}

std::vector<ScoreEntry> PlayerDatabase::getTopAllTime(int limit) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<ScoreEntry> results;
    if (!m_db) return results;

    const char* sql = R"(
        SELECT user_id, display_name, total_points, total_wins, games_played, updated_at
        FROM players
        ORDER BY total_points DESC, total_wins DESC
        LIMIT ?;
    )";

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ScoreEntry e;
        e.userId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        e.displayName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        e.points = sqlite3_column_int(stmt, 2);
        e.wins = sqlite3_column_int(stmt, 3);
        e.gamesPlayed = sqlite3_column_int(stmt, 4);
        e.timestamp = sqlite3_column_double(stmt, 5);
        results.push_back(std::move(e));
    }
    sqlite3_finalize(stmt);

    return results;
}

nlohmann::json PlayerDatabase::getPlayerStats(const std::string& userId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_db) return {};

    const char* sql = R"(
        SELECT display_name, total_points, total_wins, games_played, updated_at
        FROM players WHERE user_id = ?;
    )";

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, userId.c_str(), -1, SQLITE_TRANSIENT);

    nlohmann::json result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result["displayName"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        result["totalPoints"] = sqlite3_column_int(stmt, 1);
        result["totalWins"] = sqlite3_column_int(stmt, 2);
        result["gamesPlayed"] = sqlite3_column_int(stmt, 3);
        result["lastPlayed"] = sqlite3_column_double(stmt, 4);
    }
    sqlite3_finalize(stmt);

    return result;
}

nlohmann::json PlayerDatabase::recentToJson(int limit, int hours) const {
    auto entries = getTopRecent(limit, hours);
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : entries) {
        arr.push_back({
            {"userId", e.userId},
            {"displayName", e.displayName},
            {"points", e.points},
            {"wins", e.wins},
            {"gamesPlayed", e.gamesPlayed},
            {"lastPlayed", e.timestamp}
        });
    }
    return arr;
}

nlohmann::json PlayerDatabase::allTimeToJson(int limit) const {
    auto entries = getTopAllTime(limit);
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : entries) {
        arr.push_back({
            {"userId", e.userId},
            {"displayName", e.displayName},
            {"points", e.points},
            {"wins", e.wins},
            {"gamesPlayed", e.gamesPlayed},
            {"lastPlayed", e.timestamp}
        });
    }
    return arr;
}

// ── Filtered queries (exclude hidden players) ────────────────────────────────

std::vector<ScoreEntry> PlayerDatabase::getTopRecentFiltered(
        int limit, int hours,
        const std::vector<std::string>& excludeIds) const {
    if (excludeIds.empty()) return getTopRecent(limit, hours);

    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<ScoreEntry> results;
    if (!m_db) return results;

    double cutoff = std::chrono::duration<double>(
        std::chrono::system_clock::now().time_since_epoch()).count() - (hours * 3600.0);

    // Build SQL with placeholders for exclusions
    std::string sql = "SELECT user_id, display_name, SUM(points) as total_pts, "
                      "SUM(is_win) as wins, COUNT(*) as games, MAX(timestamp) as last_played "
                      "FROM game_results WHERE timestamp >= ? AND user_id NOT IN (";
    for (size_t i = 0; i < excludeIds.size(); ++i) {
        sql += (i > 0) ? ",?" : "?";
    }
    sql += ") GROUP BY user_id ORDER BY total_pts DESC, wins DESC LIMIT ?;";

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr);
    sqlite3_bind_double(stmt, 1, cutoff);
    int idx = 2;
    for (const auto& id : excludeIds)
        sqlite3_bind_text(stmt, idx++, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, idx, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ScoreEntry e;
        e.userId      = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        e.displayName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        e.points      = sqlite3_column_int(stmt, 2);
        e.wins        = sqlite3_column_int(stmt, 3);
        e.gamesPlayed = sqlite3_column_int(stmt, 4);
        e.timestamp   = sqlite3_column_double(stmt, 5);
        results.push_back(std::move(e));
    }
    sqlite3_finalize(stmt);
    return results;
}

std::vector<ScoreEntry> PlayerDatabase::getTopAllTimeFiltered(
        int limit,
        const std::vector<std::string>& excludeIds) const {
    if (excludeIds.empty()) return getTopAllTime(limit);

    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<ScoreEntry> results;
    if (!m_db) return results;

    std::string sql = "SELECT user_id, display_name, total_points, total_wins, "
                      "games_played, updated_at FROM players WHERE user_id NOT IN (";
    for (size_t i = 0; i < excludeIds.size(); ++i) {
        sql += (i > 0) ? ",?" : "?";
    }
    sql += ") ORDER BY total_points DESC, total_wins DESC LIMIT ?;";

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr);
    int idx = 1;
    for (const auto& id : excludeIds)
        sqlite3_bind_text(stmt, idx++, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, idx, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ScoreEntry e;
        e.userId      = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        e.displayName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        e.points      = sqlite3_column_int(stmt, 2);
        e.wins        = sqlite3_column_int(stmt, 3);
        e.gamesPlayed = sqlite3_column_int(stmt, 4);
        e.timestamp   = sqlite3_column_double(stmt, 5);
        results.push_back(std::move(e));
    }
    sqlite3_finalize(stmt);
    return results;
}

// ── Player management ────────────────────────────────────────────────────────

nlohmann::json PlayerDatabase::getAllPlayers() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    nlohmann::json arr = nlohmann::json::array();
    if (!m_db) return arr;

    const char* sql = R"(
        SELECT user_id, display_name, total_points, total_wins, games_played, updated_at
        FROM players ORDER BY total_points DESC;
    )";

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        arr.push_back({
            {"userId",      reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0))},
            {"displayName", reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1))},
            {"points",      sqlite3_column_int(stmt, 2)},
            {"wins",        sqlite3_column_int(stmt, 3)},
            {"gamesPlayed", sqlite3_column_int(stmt, 4)},
            {"lastPlayed",  sqlite3_column_double(stmt, 5)}
        });
    }
    sqlite3_finalize(stmt);
    return arr;
}

bool PlayerDatabase::updatePlayerPoints(const std::string& userId, int newPoints) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_db) return false;

    const char* sql = "UPDATE players SET total_points = ? WHERE user_id = ?;";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, newPoints);
    sqlite3_bind_text(stmt, 2, userId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    int changes = sqlite3_changes(m_db);
    sqlite3_finalize(stmt);
    return changes > 0;
}

bool PlayerDatabase::deletePlayer(const std::string& userId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_db) return false;

    // Delete game results first (FK constraint)
    const char* delResults = "DELETE FROM game_results WHERE user_id = ?;";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(m_db, delResults, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, userId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    const char* delPlayer = "DELETE FROM players WHERE user_id = ?;";
    sqlite3_prepare_v2(m_db, delPlayer, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, userId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    int changes = sqlite3_changes(m_db);
    sqlite3_finalize(stmt);
    return changes > 0;
}

} // namespace is::core
