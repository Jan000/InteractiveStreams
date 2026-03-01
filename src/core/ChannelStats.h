#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <chrono>
#include <nlohmann/json.hpp>

namespace is::core {

/// Tracks interaction statistics for a channel or stream.
/// Records unique viewers, total interactions, and per-user engagement sessions
/// with a configurable session timeout (for 24/7 streams where users return).
class ChannelStats {
public:
    /// Session timeout in seconds (default 30 min).
    /// If a user is silent for longer than this, a new session starts.
    static constexpr double DEFAULT_SESSION_TIMEOUT = 1800.0;

    explicit ChannelStats(double sessionTimeout = DEFAULT_SESSION_TIMEOUT)
        : m_sessionTimeout(sessionTimeout) {}

    /// Record an incoming message from a user.
    void recordMessage(const std::string& userId, const std::string& displayName) {
        auto now = std::chrono::steady_clock::now();
        double t = std::chrono::duration<double>(now - m_startTime).count();

        m_totalMessages++;
        m_uniqueUsers.insert(userId);
        m_userMessageCounts[userId]++;

        // Update display name cache
        m_displayNames[userId] = displayName;

        auto& ua = m_userActivity[userId];
        if (ua.sessions.empty()) {
            // First message ever from this user
            ua.sessions.push_back({t, t});
        } else {
            auto& lastSession = ua.sessions.back();
            double gap = t - lastSession.second;
            if (gap > m_sessionTimeout) {
                // Start a new session
                ua.sessions.push_back({t, t});
            } else {
                // Extend the current session
                lastSession.second = t;
            }
        }
    }

    /// Number of unique users (viewers) who sent at least one message.
    int uniqueViewerCount() const {
        return static_cast<int>(m_uniqueUsers.size());
    }

    /// Total number of messages recorded.
    int totalInteractions() const {
        return m_totalMessages;
    }

    /// Ratio of interactions to unique viewers (0 if no viewers).
    double interactionRatio() const {
        if (m_uniqueUsers.empty()) return 0.0;
        return static_cast<double>(m_totalMessages) / static_cast<double>(m_uniqueUsers.size());
    }

    /// Average engagement time in seconds across all completed sessions.
    /// A session with only one message counts as 0 duration.
    /// Only sessions that have ended (gap > timeout or not the current session) are "completed".
    /// We include all sessions for a more useful metric.
    double avgEngagementSeconds() const {
        double totalDuration = 0.0;
        int sessionCount = 0;
        for (const auto& [userId, ua] : m_userActivity) {
            for (const auto& session : ua.sessions) {
                totalDuration += (session.second - session.first);
                sessionCount++;
            }
        }
        if (sessionCount == 0) return 0.0;
        return totalDuration / static_cast<double>(sessionCount);
    }

    /// Median engagement time in seconds across all sessions.
    double medianEngagementSeconds() const {
        std::vector<double> durations;
        for (const auto& [userId, ua] : m_userActivity) {
            for (const auto& session : ua.sessions) {
                durations.push_back(session.second - session.first);
            }
        }
        if (durations.empty()) return 0.0;
        std::sort(durations.begin(), durations.end());
        size_t n = durations.size();
        if (n % 2 == 0) return (durations[n / 2 - 1] + durations[n / 2]) / 2.0;
        return durations[n / 2];
    }

    /// Total number of sessions across all users.
    int totalSessions() const {
        int count = 0;
        for (const auto& [userId, ua] : m_userActivity)
            count += static_cast<int>(ua.sessions.size());
        return count;
    }

    /// Top N most active users by message count.
    std::vector<std::pair<std::string, int>> topUsers(int n = 5) const {
        std::vector<std::pair<std::string, int>> sorted(
            m_userMessageCounts.begin(), m_userMessageCounts.end());

        std::sort(sorted.begin(), sorted.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });
        if (static_cast<int>(sorted.size()) > n) sorted.resize(n);

        // Replace userId with displayName if available
        for (auto& [id, count] : sorted) {
            auto it = m_displayNames.find(id);
            if (it != m_displayNames.end()) id = it->second;
        }
        return sorted;
    }

    /// Uptime in seconds since this stats object was created.
    double uptimeSeconds() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration<double>(now - m_startTime).count();
    }

    /// Serialise stats to JSON.
    nlohmann::json toJson() const {
        nlohmann::json j;
        j["uniqueViewers"]        = uniqueViewerCount();
        j["totalInteractions"]    = totalInteractions();
        j["interactionRatio"]     = std::round(interactionRatio() * 100.0) / 100.0;
        j["avgEngagementSeconds"] = std::round(avgEngagementSeconds() * 10.0) / 10.0;
        j["medianEngagementSeconds"] = std::round(medianEngagementSeconds() * 10.0) / 10.0;
        j["totalSessions"]        = totalSessions();
        j["uptimeSeconds"]        = std::round(uptimeSeconds());
        j["sessionTimeoutSeconds"]= m_sessionTimeout;

        // Per-user message counts
        nlohmann::json usersArr = nlohmann::json::array();
        for (const auto& [userId, ua] : m_userActivity) {
            nlohmann::json u;
            auto nameIt = m_displayNames.find(userId);
            u["userId"]      = userId;
            u["displayName"] = (nameIt != m_displayNames.end()) ? nameIt->second : userId;
            u["sessions"]    = static_cast<int>(ua.sessions.size());
            u["messages"]    = m_userMessageCounts.count(userId) ?
                               m_userMessageCounts.at(userId) : 0;

            // Total engagement for this user
            double totalEng = 0.0;
            for (const auto& s : ua.sessions)
                totalEng += (s.second - s.first);
            u["engagementSeconds"] = std::round(totalEng * 10.0) / 10.0;
            usersArr.push_back(u);
        }

        // Sort by message count descending
        std::sort(usersArr.begin(), usersArr.end(),
            [](const nlohmann::json& a, const nlohmann::json& b) {
                return a["messages"].get<int>() > b["messages"].get<int>();
            });

        j["users"] = usersArr;
        return j;
    }

    /// Reset all stats.
    void reset() {
        m_totalMessages = 0;
        m_uniqueUsers.clear();
        m_userActivity.clear();
        m_displayNames.clear();
        m_userMessageCounts.clear();
        m_startTime = std::chrono::steady_clock::now();
    }

private:
    struct UserActivity {
        /// List of sessions: {startTime, lastMessageTime} in seconds since m_startTime.
        std::vector<std::pair<double, double>> sessions;
    };

    double m_sessionTimeout;
    int    m_totalMessages = 0;

    std::unordered_set<std::string>         m_uniqueUsers;
    std::unordered_map<std::string, UserActivity> m_userActivity;
    std::unordered_map<std::string, std::string>  m_displayNames;
    std::unordered_map<std::string, int>          m_userMessageCounts;

    std::chrono::steady_clock::time_point m_startTime = std::chrono::steady_clock::now();
};

} // namespace is::core
