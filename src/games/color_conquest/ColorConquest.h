#pragma once

#include "games/IGame.h"
#include "games/GameRegistry.h"
#include "games/color_conquest/Grid.h"
#include "games/color_conquest/Team.h"

#include <SFML/Graphics.hpp>
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <unordered_set>
#include <random>
#include <chrono>

namespace is::games::color_conquest {

/// The game phases for Color Conquest.
enum class Phase {
    Lobby,      ///< Waiting for players to join teams
    Playing,    ///< Active voting rounds
    RoundEnd,   ///< Brief pause between rounds showing results
    GameOver    ///< Final results screen
};

/// Color Conquest – a territorial control game designed for 500+ concurrent players.
///
/// Players join one of four teams via `!join red/blue/green/yellow`.
/// Each round, team members vote on an expansion direction (`!up/!down/!left/!right`).
/// The team expands into the voted direction. Teams can conquer enemy cells.
/// The team that controls the most cells after all rounds wins.
///
/// Architecture: O(1) per chat message, O(grid) per update tick, no physics engine.
class ColorConquest : public IGame {
public:
    ColorConquest();
    ~ColorConquest() override = default;

    std::string id() const override { return "color_conquest"; }
    std::string displayName() const override { return "Color Conquest"; }
    std::string description() const override {
        return "Territorial control for massive player counts. "
               "Join a team, vote on expansion direction, conquer the grid!";
    }

    void initialize() override;
    void shutdown() override;
    void onChatMessage(const platform::ChatMessage& msg) override;
    void update(double dt) override;
    void render(sf::RenderTarget& target, double alpha) override;
    bool isRoundComplete() const override;
    bool isGameOver() const override;
    nlohmann::json getState() const override;
    nlohmann::json getCommands() const override;
    int maxPlayers() const override { return 0; } // unlimited by design
    void configure(const nlohmann::json& settings) override;
    nlohmann::json getSettings() const override;

private:
    // ── Command handlers ────────────────────────────────────────
    void cmdJoin(const std::string& userId, const std::string& displayName,
                 const std::string& teamName);
    void cmdVote(const std::string& userId, Direction dir);
    void cmdEmote(const std::string& userId, const std::string& emote);
    void handleStreamEvent(const platform::ChatMessage& msg);

    // ── Game logic ──────────────────────────────────────────────
    void startGame();
    void executeRound();
    void expandTeam(TeamId team, Direction dir);
    void checkGameOver();
    void resetForNewGame();

    // ── Rendering ───────────────────────────────────────────────
    void renderGrid(sf::RenderTarget& target);
    void renderTeamStats(sf::RenderTarget& target);
    void renderVoteArrows(sf::RenderTarget& target);
    void renderRoundInfo(sf::RenderTarget& target);
    void renderLobby(sf::RenderTarget& target);
    void renderGameOver(sf::RenderTarget& target);
    void renderCellConquestAnimation(sf::RenderTarget& target);

    // ── Helpers ─────────────────────────────────────────────────
    TeamId parseTeamName(const std::string& name) const;
    Direction parseDirection(const std::string& cmd) const;
    sf::Color getTeamColor(TeamId team) const;
    sf::Color getTeamColorDark(TeamId team) const;
    std::string getTeamName(TeamId team) const;
    Direction getWinningVote(TeamId team) const;
    /// Scale a font size by the current font scale factor.
    unsigned int fs(int base) const {
        return static_cast<unsigned int>(std::max(1.0f, base * m_fontScale));
    }
    // ── State ─────────────────────────────────────────────────
    Phase           m_phase = Phase::Lobby;
    Grid            m_grid;

    // Team data
    std::array<TeamData, 4> m_teams;

    // Player → Team mapping (supports unlimited players, O(1) lookup)
    std::unordered_map<std::string, TeamId> m_playerTeam;

    // Player display names (for dashboard)
    std::unordered_map<std::string, std::string> m_playerNames;

    // Current round votes per player (overwritten each round)
    std::unordered_map<std::string, Direction> m_currentVotes;

    // Round management
    int     m_currentRound   = 0;
    int     m_maxRounds      = 30;
    float   m_roundTimer     = 0.0f;
    float   m_roundDuration  = 8.0f;   ///< Seconds per voting round
    float   m_resultDuration = 3.0f;   ///< Seconds showing round result
    float   m_lobbyTimer     = 0.0f;
    float   m_lobbyDuration  = 10.0f;  ///< Min seconds in lobby

    // Conquest animation
    struct ConquestAnim {
        int x, y;
        TeamId newTeam;
        float timer;
    };
    std::vector<ConquestAnim> m_conquestAnims;

    // Event log (for kill-feed style messages)
    struct EventEntry {
        std::string text;
        sf::Color   color;
        float       timer;
    };
    std::vector<EventEntry> m_events;

    // Rendering
    sf::Font    m_font;
    bool        m_fontLoaded = false;
    float       m_animTime   = 0.0f;

    // RNG
    std::mt19937 m_rng{std::random_device{}()};

    // ── Constants ───────────────────────────────────────────────
    static constexpr int   MIN_PLAYERS_TO_START = 2;
    static constexpr float EVENT_DISPLAY_TIME   = 5.0f;
    static constexpr float CONQUEST_ANIM_TIME   = 0.5f;
};

} // namespace is::games::color_conquest
