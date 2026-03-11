#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <string>
#include <nlohmann/json.hpp>

namespace is::platform {

/// Tracks YouTube Data API v3 quota usage.
///
/// YouTube enforces a daily quota of 10,000 units (resets at midnight Pacific Time).
/// Different API calls cost different amounts:
///   - list (read) operations:       1 unit
///   - search.list:                100 units
///   - insert/update/delete:        50 units
///
/// This tracker allows setting a configurable budget <= 10,000 and blocks
/// further API calls once the budget is exhausted.
class YouTubeQuota {
public:
    /// Well-known quota costs per API method.
    static constexpr int COST_LIST           = 1;    // liveBroadcasts.list, liveChatMessages.list, videos.list
    static constexpr int COST_SEARCH         = 100;  // search.list
    static constexpr int COST_INSERT         = 50;   // liveChatMessages.insert
    static constexpr int COST_UPDATE         = 50;   // liveBroadcasts.update
    static constexpr int COST_MAX_DAILY      = 10000;

    /// Maximum number of log entries kept in memory.
    static constexpr size_t MAX_LOG_ENTRIES   = 200;

    /// A single logged API call.
    struct LogEntry {
        std::string timestamp;  // ISO-8601 UTC
        std::string method;     // e.g. "liveBroadcasts.list"
        int cost;
        int totalAfter;         // cumulative usage after this call
        bool blocked;           // true if the call was rejected
    };

    static YouTubeQuota& instance();

    /// Try to consume `cost` units.  Returns true if the budget allows it,
    /// false if the request would exceed the remaining budget.
    /// `method` is a human-readable label for the API call (logged).
    bool consume(int cost, const std::string& method = "unknown");

    /// Current usage today.
    int used() const;

    /// Configured daily budget.
    int budget() const;

    /// Remaining units today.
    int remaining() const;

    /// Set the daily budget (clamped to 1 .. 10 000).
    void setBudget(int budget);

    /// Manually reset usage to 0 (e.g. after midnight or a manual reset).
    void reset();

    /// JSON representation for the API / dashboard.
    nlohmann::json toJson() const;

    /// Recent API call log as JSON array (newest first).
    nlohmann::json logToJson() const;

private:
    YouTubeQuota();

    /// Check if the day has rolled over (Pacific Time) and auto-reset.
    void checkDayRollover();

    /// Get the current day number in Pacific Time.
    int pacificDayNumber() const;

    /// Format current time as ISO-8601 UTC string.
    static std::string nowIso8601();

    mutable std::mutex m_mutex;
    int m_budget = COST_MAX_DAILY;
    int m_used   = 0;
    int m_dayNumber = 0; ///< Day number when usage was last reset

    std::deque<LogEntry> m_log;
};

} // namespace is::platform
