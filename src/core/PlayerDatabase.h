#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <nlohmann/json.hpp>

struct sqlite3;

namespace is::core {

/// A single scoreboard entry (used for API responses).
struct ScoreEntry {
    std::string userId;
    std::string displayName;
    std::string gameName;
    int points = 0;
    int wins = 0;
    int gamesPlayed = 0;
    double timestamp = 0.0;  // epoch seconds
};

/// Persistent player database backed by SQLite.
/// Stores player scores, wins, and game history.
/// Thread-safe: all public methods lock an internal mutex.
class PlayerDatabase {
public:
    PlayerDatabase();
    ~PlayerDatabase();

    /// Open (or create) the database file.
    bool open(const std::string& path = "data/players.db");

    /// Close the database.
    void close();

    // ── Score recording ──────────────────────────────────────────────────

    /// Record a game result for a player.
    /// @param userId    Unique user ID (platform:name format).
    /// @param displayName  Human-readable display name.
    /// @param gameName  Game ID (e.g. "chaos_arena").
    /// @param points    Points awarded (e.g. 1 for win, 0 for participation).
    /// @param isWin     Whether this counts as a win.
    void recordResult(const std::string& userId,
                      const std::string& displayName,
                      const std::string& gameName,
                      int points,
                      bool isWin);

    // ── Leaderboard queries ──────────────────────────────────────────────

    /// Get top N players by total points in the last `hours` hours.
    /// @param limit  Max entries (default 10).
    /// @param hours  Time window in hours (default 24).
    std::vector<ScoreEntry> getTopRecent(int limit = 10, int hours = 24) const;

    /// Get top N players by all-time total points.
    /// @param limit  Max entries (default 5).
    std::vector<ScoreEntry> getTopAllTime(int limit = 5) const;

    /// Get full stats for a single player.
    nlohmann::json getPlayerStats(const std::string& userId) const;

    // ── Leaderboard queries (with exclusion) ────────────────────────────

    /// Get top N recent excluding certain user IDs.
    std::vector<ScoreEntry> getTopRecentFiltered(int limit, int hours,
        const std::vector<std::string>& excludeIds) const;

    /// Get top N all-time excluding certain user IDs.
    std::vector<ScoreEntry> getTopAllTimeFiltered(int limit,
        const std::vector<std::string>& excludeIds) const;

    // ── Player management ────────────────────────────────────────────────

    /// Get all players (for admin management page).
    nlohmann::json getAllPlayers() const;

    /// Update a player's total points.
    bool updatePlayerPoints(const std::string& userId, int newPoints);

    /// Delete a player record entirely.
    bool deletePlayer(const std::string& userId);

    // ── JSON serialisation ───────────────────────────────────────────────

    nlohmann::json recentToJson(int limit = 10, int hours = 24) const;
    nlohmann::json allTimeToJson(int limit = 5) const;

private:
    void createTables();

    sqlite3* m_db = nullptr;
    mutable std::mutex m_mutex;
};

} // namespace is::core
