#include "platform/youtube/YouTubeQuota.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace is::platform {

YouTubeQuota::YouTubeQuota()
    : m_dayNumber(pacificDayNumber())
{
}

YouTubeQuota& YouTubeQuota::instance() {
    static YouTubeQuota s_instance;
    return s_instance;
}

int YouTubeQuota::pacificDayNumber() const {
    // YouTube quota resets at midnight Pacific Time (UTC-8, ignoring DST for simplicity).
    auto now = std::chrono::system_clock::now();
    auto epoch = now.time_since_epoch();
    auto secs  = std::chrono::duration_cast<std::chrono::seconds>(epoch).count();
    // Shift to Pacific: subtract 8 hours
    constexpr int64_t PACIFIC_OFFSET = 8 * 3600;
    return static_cast<int>((secs - PACIFIC_OFFSET) / 86400);
}

std::string YouTubeQuota::nowIso8601() {
    auto now = std::chrono::system_clock::now();
    auto tt  = std::chrono::system_clock::to_time_t(now);
    std::tm gmt{};
#ifdef _WIN32
    gmtime_s(&gmt, &tt);
#else
    gmtime_r(&tt, &gmt);
#endif
    std::ostringstream oss;
    oss << std::put_time(&gmt, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

void YouTubeQuota::checkDayRollover() {
    // caller must hold m_mutex
    int today = pacificDayNumber();
    if (today != m_dayNumber) {
        spdlog::info("[YouTube Quota] New day (Pacific) — resetting usage from {} to 0.", m_used);
        m_used = 0;
        m_dayNumber = today;
        m_log.clear();
    }
}

bool YouTubeQuota::consume(int cost, const std::string& method) {
    std::lock_guard<std::mutex> lock(m_mutex);
    checkDayRollover();

    bool blocked = (m_used + cost > m_budget);
    if (!blocked) {
        m_used += cost;
    }

    // Log every call
    LogEntry entry;
    entry.timestamp  = nowIso8601();
    entry.method     = method;
    entry.cost       = cost;
    entry.totalAfter = m_used;
    entry.blocked    = blocked;
    m_log.push_back(std::move(entry));
    if (m_log.size() > MAX_LOG_ENTRIES) {
        m_log.pop_front();
    }

    if (blocked) {
        spdlog::warn("[YouTube Quota] Blocked {}: {} units requested, {}/{} used.",
                     method, cost, m_used, m_budget);
    } else {
        spdlog::debug("[YouTube Quota] {} — {} units ({}/{} used).", method, cost, m_used, m_budget);
    }
    return !blocked;
}

int YouTubeQuota::used() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_used;
}

int YouTubeQuota::budget() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_budget;
}

int YouTubeQuota::remaining() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return std::max(0, m_budget - m_used);
}

void YouTubeQuota::setBudget(int budget) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_budget = std::clamp(budget, 1, COST_MAX_DAILY);
    spdlog::info("[YouTube Quota] Budget set to {} units.", m_budget);
}

void YouTubeQuota::reset() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_used = 0;
    m_dayNumber = pacificDayNumber();
    m_log.clear();
    spdlog::info("[YouTube Quota] Usage manually reset.");
}

nlohmann::json YouTubeQuota::toJson() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return {
        {"used", m_used},
        {"budget", m_budget},
        {"remaining", std::max(0, m_budget - m_used)},
        {"max_daily", COST_MAX_DAILY},
        {"log", logToJson()}
    };
}

nlohmann::json YouTubeQuota::logToJson() const {
    // caller may or may not hold m_mutex — safe because toJson() already holds it
    // and standalone callers should lock themselves.
    nlohmann::json arr = nlohmann::json::array();
    // Return newest first
    for (auto it = m_log.rbegin(); it != m_log.rend(); ++it) {
        arr.push_back({
            {"timestamp", it->timestamp},
            {"method", it->method},
            {"cost", it->cost},
            {"total_after", it->totalAfter},
            {"blocked", it->blocked}
        });
    }
    return arr;
}

} // namespace is::platform
