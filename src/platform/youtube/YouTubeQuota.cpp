#include "platform/youtube/YouTubeQuota.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <ctime>

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

void YouTubeQuota::checkDayRollover() {
    // caller must hold m_mutex
    int today = pacificDayNumber();
    if (today != m_dayNumber) {
        spdlog::info("[YouTube Quota] New day (Pacific) — resetting usage from {} to 0.", m_used);
        m_used = 0;
        m_dayNumber = today;
    }
}

bool YouTubeQuota::consume(int cost) {
    std::lock_guard<std::mutex> lock(m_mutex);
    checkDayRollover();
    if (m_used + cost > m_budget) {
        spdlog::warn("[YouTube Quota] Blocked: {} units requested, {}/{} used. Budget exhausted.",
                     cost, m_used, m_budget);
        return false;
    }
    m_used += cost;
    spdlog::debug("[YouTube Quota] Consumed {} units ({}/{} used).", cost, m_used, m_budget);
    return true;
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
    spdlog::info("[YouTube Quota] Usage manually reset.");
}

nlohmann::json YouTubeQuota::toJson() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return {
        {"used", m_used},
        {"budget", m_budget},
        {"remaining", std::max(0, m_budget - m_used)},
        {"max_daily", COST_MAX_DAILY}
    };
}

} // namespace is::platform
