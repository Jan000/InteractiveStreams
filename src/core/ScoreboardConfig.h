#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace is::core {

/// Visual / behavioural configuration for a single scoreboard panel.
struct ScoreboardPanelConfig {
    bool        enabled      = true;
    std::string title;
    double      durationSecs = 10.0;   ///< Display duration (0 = skip this panel)
    int         topN         = 5;      ///< Number of entries to show
    int         fontSize     = 20;     ///< Base font size
    float       boxWidthPct  = 30.0f;  ///< % of screen width
    float       posXPct      = 70.0f;  ///< X position, % from left (left edge of box)
    float       posYPct      = 1.0f;   ///< Y position, % from top
    float       opacity      = 0.7f;   ///< Overall panel opacity (0-1)
    std::string bgColor      = "#0A0A14";
    std::string borderColor  = "#5082C8";
    std::string titleColor   = "#FFD700";
    std::string nameColor    = "#AAAABC";
    std::string pointsColor  = "#58A6FF";

    nlohmann::json toJson() const {
        return {
            {"enabled",       enabled},
            {"title",         title},
            {"duration_secs", durationSecs},
            {"top_n",         topN},
            {"font_size",     fontSize},
            {"box_width_pct", boxWidthPct},
            {"pos_x_pct",     posXPct},
            {"pos_y_pct",     posYPct},
            {"opacity",       opacity},
            {"bg_color",      bgColor},
            {"border_color",  borderColor},
            {"title_color",   titleColor},
            {"name_color",    nameColor},
            {"points_color",  pointsColor}
        };
    }

    static ScoreboardPanelConfig fromJson(const nlohmann::json& j,
                                           const ScoreboardPanelConfig& def);
};

inline ScoreboardPanelConfig ScoreboardPanelConfig::fromJson(
        const nlohmann::json& j, const ScoreboardPanelConfig& def) {
    ScoreboardPanelConfig c;
    c.enabled      = j.value("enabled",       def.enabled);
    c.title        = j.value("title",         def.title);
    c.durationSecs = j.value("duration_secs", def.durationSecs);
    c.topN         = j.value("top_n",         def.topN);
    c.fontSize     = j.value("font_size",     def.fontSize);
    c.boxWidthPct  = j.value("box_width_pct", def.boxWidthPct);
    c.posXPct      = j.value("pos_x_pct",     def.posXPct);
    c.posYPct      = j.value("pos_y_pct",     def.posYPct);
    c.opacity       = j.value("opacity",       def.opacity);
    c.bgColor      = j.value("bg_color",      def.bgColor);
    c.borderColor  = j.value("border_color",  def.borderColor);
    c.titleColor   = j.value("title_color",   def.titleColor);
    c.nameColor    = j.value("name_color",     def.nameColor);
    c.pointsColor  = j.value("points_color",  def.pointsColor);
    return c;
}

/// Global scoreboard configuration shared by all streams.
struct GlobalScoreboardConfig {
    ScoreboardPanelConfig alltime;
    ScoreboardPanelConfig recent;
    ScoreboardPanelConfig round;
    int    recentHours  = 24;
    double fadeSecs     = 1.0;
    int    chatInterval = 120;   ///< Seconds between chat posts (0 = disabled)
    std::vector<std::string> hiddenPlayers;  ///< user_ids to exclude

    GlobalScoreboardConfig() {
        alltime.title        = "ALL TIME";
        alltime.durationSecs = 10.0;
        recent.title         = "LAST 24H";
        recent.durationSecs  = 10.0;
        round.title          = "CURRENT ROUND";
        round.durationSecs   = 8.0;
    }

    nlohmann::json toJson() const {
        nlohmann::json j;
        j["alltime"]        = alltime.toJson();
        j["recent"]         = recent.toJson();
        j["round"]          = round.toJson();
        j["recent_hours"]   = recentHours;
        j["fade_secs"]      = fadeSecs;
        j["chat_interval"]  = chatInterval;
        j["hidden_players"] = hiddenPlayers;
        return j;
    }

    static GlobalScoreboardConfig fromJson(const nlohmann::json& j) {
        GlobalScoreboardConfig c;
        ScoreboardPanelConfig defAlltime;
        defAlltime.title = "ALL TIME";
        ScoreboardPanelConfig defRecent;
        defRecent.title = "LAST 24H";
        ScoreboardPanelConfig defRound;
        defRound.title = "CURRENT ROUND";
        defRound.durationSecs = 8.0;

        if (j.contains("alltime") && j["alltime"].is_object())
            c.alltime = ScoreboardPanelConfig::fromJson(j["alltime"], defAlltime);
        if (j.contains("recent") && j["recent"].is_object())
            c.recent = ScoreboardPanelConfig::fromJson(j["recent"], defRecent);
        if (j.contains("round") && j["round"].is_object())
            c.round = ScoreboardPanelConfig::fromJson(j["round"], defRound);

        c.recentHours  = j.value("recent_hours",  24);
        c.fadeSecs     = j.value("fade_secs",     1.0);
        c.chatInterval = j.value("chat_interval",  120);

        if (j.contains("hidden_players") && j["hidden_players"].is_array()) {
            for (const auto& id : j["hidden_players"])
                c.hiddenPlayers.push_back(id.get<std::string>());
        }
        return c;
    }
};

} // namespace is::core
