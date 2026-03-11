#pragma once

#include <atomic>
#include <chrono>
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

    static YouTubeQuota& instance();

    /// Try to consume `cost` units.  Returns true if the budget allows it,
    /// false if the request would exceed the remaining budget.
    bool consume(int cost);

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

private:
    YouTubeQuota();

    /// Check if the day has rolled over (Pacific Time) and auto-reset.
    void checkDayRollover();

    /// Get the current day number in Pacific Time.
    int pacificDayNumber() const;

    mutable std::mutex m_mutex;
    int m_budget = COST_MAX_DAILY;
    int m_used   = 0;
    int m_dayNumber = 0; ///< Day number when usage was last reset
};

} // namespace is::platform
