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
    std::string alignX       = "left";  ///< "left", "center", "right"
    std::string alignY       = "top";   ///< "top", "center", "bottom"
    float       opacity      = 0.7f;   ///< Overall panel opacity (0-1)
    std::string bgColor      = "#0A0A14";
    std::string borderColor  = "#5082C8";
    std::string titleColor   = "#FFD700";
    std::string nameColor    = "#AAAABC";
    std::string pointsColor  = "#58A6FF";
    std::string goldColor    = "#FFD700";
    std::string silverColor  = "#C8C8C8";
    std::string bronzeColor  = "#CD7F32";

    // ── Content type / data source ──────────────────────────────────────
    std::string contentType  = "players";   ///< "players" or "countries"
    std::string timeRange    = "alltime";   ///< "round", "recent", "alltime"
    std::string gameFilter;                 ///< empty = all games, else specific game_id
    int         group        = 0;           ///< Panels in same group cycle; different groups render simultaneously
    bool        includeBots  = false;       ///< Include bots in country panels
    bool        showFlags    = true;        ///< Show flag icons (country panels)
    std::string flagShape    = "circle";    ///< "circle" or "rect"
    float       flagSize     = 1.0f;        ///< Flag size multiplier
    bool        showNames    = true;        ///< Show display names
    bool        showCodes    = false;       ///< Show country ISO codes (country panels)
    std::string valueLabel   = "pts";       ///< Label suffix for values (e.g. "pts", "wins")
    bool        showAvatars  = false;       ///< Show player avatars (player panels)
    std::string avatarShape  = "circle";    ///< "circle" or "rect"
    float       avatarSize   = 1.0f;        ///< Avatar size multiplier

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
            {"align_x",       alignX},
            {"align_y",       alignY},
            {"opacity",       opacity},
            {"bg_color",      bgColor},
            {"border_color",  borderColor},
            {"title_color",   titleColor},
            {"name_color",    nameColor},
            {"points_color",  pointsColor},
            {"gold_color",    goldColor},
            {"silver_color",  silverColor},
            {"bronze_color",  bronzeColor},
            {"content_type",  contentType},
            {"time_range",    timeRange},
            {"game_filter",   gameFilter},
            {"group",         group},
            {"include_bots",  includeBots},
            {"show_flags",    showFlags},
            {"flag_shape",    flagShape},
            {"flag_size",     flagSize},
            {"show_names",    showNames},
            {"show_codes",    showCodes},
            {"value_label",   valueLabel},
            {"show_avatars",  showAvatars},
            {"avatar_shape",  avatarShape},
            {"avatar_size",   avatarSize}
        };
    }

    static ScoreboardPanelConfig fromJson(const nlohmann::json& j,
                                           const ScoreboardPanelConfig& def);
    static ScoreboardPanelConfig fromJson(const nlohmann::json& j) {
        return fromJson(j, ScoreboardPanelConfig{});
    }
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
    c.alignX       = j.value("align_x",       def.alignX);
    c.alignY       = j.value("align_y",       def.alignY);
    c.opacity      = j.value("opacity",        def.opacity);
    c.bgColor      = j.value("bg_color",      def.bgColor);
    c.borderColor  = j.value("border_color",  def.borderColor);
    c.titleColor   = j.value("title_color",   def.titleColor);
    c.nameColor    = j.value("name_color",    def.nameColor);
    c.pointsColor  = j.value("points_color",  def.pointsColor);
    c.goldColor    = j.value("gold_color",    def.goldColor);
    c.silverColor  = j.value("silver_color",  def.silverColor);
    c.bronzeColor  = j.value("bronze_color",  def.bronzeColor);
    c.contentType  = j.value("content_type",  def.contentType);
    c.timeRange    = j.value("time_range",    def.timeRange);
    c.gameFilter   = j.value("game_filter",   def.gameFilter);
    c.group        = j.value("group",         def.group);
    c.includeBots  = j.value("include_bots",  def.includeBots);
    c.showFlags    = j.value("show_flags",    def.showFlags);
    c.flagShape    = j.value("flag_shape",    def.flagShape);
    c.flagSize     = j.value("flag_size",     def.flagSize);
    c.showNames    = j.value("show_names",    def.showNames);
    c.showCodes    = j.value("show_codes",    def.showCodes);
    c.valueLabel   = j.value("value_label",   def.valueLabel);
    c.showAvatars  = j.value("show_avatars",  def.showAvatars);
    c.avatarShape  = j.value("avatar_shape",  def.avatarShape);
    c.avatarSize   = j.value("avatar_size",   def.avatarSize);
    return c;
}

/// Global scoreboard configuration shared by all streams.
struct GlobalScoreboardConfig {
    std::vector<ScoreboardPanelConfig> panels;
    int    recentHours  = 24;
    double fadeSecs     = 1.0;
    int    chatInterval = 120;   ///< Seconds between chat posts (0 = disabled)
    std::vector<std::string> hiddenPlayers;  ///< user_ids to exclude

    GlobalScoreboardConfig() {
        // Default: 3 player panels in group 0, matching old behavior
        ScoreboardPanelConfig p1;
        p1.title = "ALL TIME"; p1.timeRange = "alltime"; p1.group = 0;
        p1.valueLabel = "pts";
        ScoreboardPanelConfig p2;
        p2.title = "LAST 24H"; p2.timeRange = "recent"; p2.group = 0;
        p2.valueLabel = "pts";
        ScoreboardPanelConfig p3;
        p3.title = "CURRENT ROUND"; p3.timeRange = "round"; p3.group = 0;
        p3.durationSecs = 8.0; p3.valueLabel = "pts";
        panels = {p1, p2, p3};
    }

    nlohmann::json toJson() const {
        nlohmann::json j;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& p : panels)
            arr.push_back(p.toJson());
        j["panels"]         = arr;
        j["recent_hours"]   = recentHours;
        j["fade_secs"]      = fadeSecs;
        j["chat_interval"]  = chatInterval;
        j["hidden_players"] = hiddenPlayers;
        return j;
    }

    static GlobalScoreboardConfig fromJson(const nlohmann::json& j) {
        GlobalScoreboardConfig c;

        // ── New format: panels array ────────────────────────────────────
        if (j.contains("panels") && j["panels"].is_array()) {
            c.panels.clear();
            for (const auto& pj : j["panels"])
                c.panels.push_back(ScoreboardPanelConfig::fromJson(pj));
        }
        // ── Legacy format: alltime/recent/round objects ─────────────────
        else if (j.contains("alltime") || j.contains("recent") || j.contains("round")) {
            c.panels.clear();
            ScoreboardPanelConfig defAlltime;
            defAlltime.title = "ALL TIME"; defAlltime.timeRange = "alltime";
            ScoreboardPanelConfig defRecent;
            defRecent.title = "LAST 24H"; defRecent.timeRange = "recent";
            ScoreboardPanelConfig defRound;
            defRound.title = "CURRENT ROUND"; defRound.timeRange = "round";
            defRound.durationSecs = 8.0;

            if (j.contains("alltime") && j["alltime"].is_object()) {
                auto p = ScoreboardPanelConfig::fromJson(j["alltime"], defAlltime);
                p.timeRange = "alltime"; p.contentType = "players"; p.group = 0;
                c.panels.push_back(p);
            }
            if (j.contains("recent") && j["recent"].is_object()) {
                auto p = ScoreboardPanelConfig::fromJson(j["recent"], defRecent);
                p.timeRange = "recent"; p.contentType = "players"; p.group = 0;
                c.panels.push_back(p);
            }
            if (j.contains("round") && j["round"].is_object()) {
                auto p = ScoreboardPanelConfig::fromJson(j["round"], defRound);
                p.timeRange = "round"; p.contentType = "players"; p.group = 0;
                c.panels.push_back(p);
            }
        }

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
