#include "platform/youtube/YouTubeQuota.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace is::platform {

namespace {

// Civil date to days since Unix epoch (1970-01-01), based on
// Howard Hinnant's public-domain algorithms.
int64_t daysFromCivil(int y, unsigned m, unsigned d) {
    y -= (m <= 2);
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);                    // [0, 399]
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;          // [0, 365]
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;                    // [0, 146096]
    return era * 146097 + static_cast<int>(doe) - 719468;
}

int64_t epochSecondsUtc(int y, unsigned m, unsigned d, int hh, int mm, int ss) {
    return daysFromCivil(y, m, d) * 86400LL + hh * 3600LL + mm * 60LL + ss;
}

// 0=Sunday..6=Saturday
int dayOfWeek(int y, unsigned m, unsigned d) {
    int64_t days = daysFromCivil(y, m, d);
    int w = static_cast<int>((days + 4) % 7); // 1970-01-01 was Thursday (4)
    return (w < 0) ? w + 7 : w;
}

unsigned nthWeekdayOfMonth(int year, unsigned month, int weekday, unsigned nth) {
    int firstDow = dayOfWeek(year, month, 1);
    unsigned firstHit = 1u + static_cast<unsigned>((weekday - firstDow + 7) % 7);
    return firstHit + (nth - 1u) * 7u;
}

bool isPacificDstForUtc(int64_t utcSecs) {
    std::time_t tt = static_cast<std::time_t>(utcSecs);
    std::tm gmt{};
#ifdef _WIN32
    gmtime_s(&gmt, &tt);
#else
    gmtime_r(&tt, &gmt);
#endif
    int year = gmt.tm_year + 1900;

    // US Pacific DST:
    // - Starts second Sunday in March at 02:00 local standard time (UTC-8 => 10:00 UTC)
    // - Ends first Sunday in November at 02:00 local daylight time (UTC-7 => 09:00 UTC)
    unsigned marchSecondSunday = nthWeekdayOfMonth(year, 3u, 0, 2u);
    unsigned novFirstSunday    = nthWeekdayOfMonth(year, 11u, 0, 1u);

    int64_t dstStartUtc = epochSecondsUtc(year, 3u, marchSecondSunday, 10, 0, 0);
    int64_t dstEndUtc   = epochSecondsUtc(year, 11u, novFirstSunday, 9, 0, 0);

    return utcSecs >= dstStartUtc && utcSecs < dstEndUtc;
}

int pacificUtcOffsetHours(int64_t utcSecs) {
    return isPacificDstForUtc(utcSecs) ? 7 : 8;
}

} // namespace

YouTubeQuota::YouTubeQuota()
    : m_dayNumber(pacificDayNumber())
{
}

YouTubeQuota& YouTubeQuota::instance() {
    static YouTubeQuota s_instance;
    return s_instance;
}

int YouTubeQuota::pacificDayNumber() const {
    // YouTube quota resets at midnight Pacific Time. Respect DST (PST/PDT)
    // so rollover matches YouTube Console.
    auto now = std::chrono::system_clock::now();
    auto epoch = now.time_since_epoch();
    auto secs  = std::chrono::duration_cast<std::chrono::seconds>(epoch).count();
    int offsetHours = pacificUtcOffsetHours(secs);
    return static_cast<int>((secs - static_cast<int64_t>(offsetHours) * 3600LL) / 86400LL);
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
