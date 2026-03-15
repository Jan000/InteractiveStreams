#pragma once

#include "games/IGame.h"
#include "rendering/PostProcessing.h"

#include <SFML/Graphics/Font.hpp>
#include <box2d/box2d.h>
#include <vector>
#include <unordered_map>
#include <deque>
#include <random>

namespace is::games::country_elimination {

// ── Constants ────────────────────────────────────────────────────────────────

// Screen dimensions (9:16 portrait)
static constexpr float SCREEN_W = 1080.0f;
static constexpr float SCREEN_H = 1920.0f;

// Physics scale
static constexpr float PIXELS_PER_METER = 40.0f;

// Arena (world-space meters)
static constexpr float ARENA_CENTER_X = SCREEN_W / (2.0f * PIXELS_PER_METER);  // ~13.5m
static constexpr float ARENA_CENTER_Y = SCREEN_H / (3.0f * PIXELS_PER_METER);  // ~16m
static constexpr float ARENA_RADIUS   = 8.0f;   // meters
static constexpr float WALL_THICKNESS = 0.5f;    // meters
static constexpr float BALL_RADIUS    = 0.4f;    // meters (default player ball)

// Gap size in radians (~30 degrees)
static constexpr float GAP_HALF_ANGLE = 0.26f;   // ~15 degrees each side

// Floor for eliminated balls
static constexpr float FLOOR_Y = (SCREEN_H / PIXELS_PER_METER) - 1.0f;

// ── Game Phases ──────────────────────────────────────────────────────────────

enum class GamePhase {
    Lobby,
    Countdown,
    Battle,
    RoundEnd
};

// ── Kill Feed Entry ──────────────────────────────────────────────────────────

struct EliminationEntry {
    std::string displayName;
    std::string label;
    double      timeRemaining;
};

// ── Player ───────────────────────────────────────────────────────────────────

struct Player {
    std::string userId;
    std::string displayName;
    std::string label;          // country emoji/code shown on ball
    sf::Color   color;

    b2Body*     body       = nullptr;
    float       radiusM    = BALL_RADIUS;
    bool        alive      = true; // still inside the arena
    bool        eliminated = false; // fell out and hit the floor
    bool        hasShield  = false;
    float       shieldTimer = 0.0f;
    int         score      = 0;

    // Interpolation: previous + current position
    b2Vec2      prevPos{0.0f, 0.0f};
    b2Vec2      currPos{0.0f, 0.0f};
};

// ── Main Game Class ──────────────────────────────────────────────────────────

class CountryElimination : public IGame {
public:
    CountryElimination();
    ~CountryElimination() override;

    // IGame interface
    std::string id() const override { return "country_elimination"; }
    std::string displayName() const override { return "Country Elimination"; }
    std::string description() const override {
        return "Marble race elimination! Join with a country, survive the rotating arena. Last ball standing wins!";
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
    std::vector<std::pair<std::string, int>> getLeaderboard() const override;

    void configure(const nlohmann::json& settings) override;
    nlohmann::json getSettings() const override;

private:
    // ── Helpers ──────────────────────────────────────────────────────────────

    unsigned int fs(int base) const {
        return static_cast<unsigned int>(std::max(1.0f, base * m_fontScale));
    }

    sf::Vector2f worldToScreen(float wx, float wy) const {
        return {wx * PIXELS_PER_METER, wy * PIXELS_PER_METER};
    }
    sf::Vector2f worldToScreen(b2Vec2 v) const {
        return worldToScreen(v.x, v.y);
    }

    // ── Command Handlers ─────────────────────────────────────────────────────

    void cmdJoin(const std::string& userId, const std::string& displayName,
                 const std::string& label);
    void handleStreamEvent(const platform::ChatMessage& msg);

    // ── Game Logic ───────────────────────────────────────────────────────────

    void startCountdown();
    void startBattle();
    void checkEliminations();
    void checkRoundEnd();
    void endRound();
    void resetForNextRound();

    void createArenaBody();
    void destroyArenaBody();
    b2Body* createPlayerBody(float x, float y, float radius);

    // ── Rendering ────────────────────────────────────────────────────────────

    void renderArena(sf::RenderTarget& target);
    void renderPlayers(sf::RenderTarget& target, double alpha);
    void renderEliminatedPile(sf::RenderTarget& target);
    void renderUI(sf::RenderTarget& target);
    void renderCountdown(sf::RenderTarget& target);
    void renderEliminationFeed(sf::RenderTarget& target);

    sf::Color generateColor();

    // ── State ────────────────────────────────────────────────────────────────

    GamePhase m_phase = GamePhase::Lobby;
    std::unordered_map<std::string, Player> m_players;
    std::deque<EliminationEntry>            m_eliminationFeed;
    std::string                             m_winnerId;

    // Physics
    b2World*  m_world      = nullptr;
    b2Body*   m_arenaBody  = nullptr;   // kinematic rotating boundary
    b2Body*   m_floorBody  = nullptr;   // static floor to catch eliminated balls

    // Arena rotation
    float m_arenaAngle       = 0.0f;    // current angle offset (radians)
    float m_arenaAngularVel  = 0.3f;    // radians/second (configurable)

    // Timing
    double m_countdownTimer  = 0.0;
    double m_lobbyTimer      = 0.0;
    double m_roundTimer      = 0.0;
    double m_roundEndTimer   = 0.0;

    // Settings
    int    m_minPlayers      = 2;
    float  m_initialSpeed    = 5.0f;    // initial random velocity magnitude
    float  m_restitution     = 0.95f;   // ball bounciness
    double m_lobbyDuration   = 30.0;
    double m_roundEndDuration = 5.0;
    float  m_arenaSpeedIncrease = 0.02f; // angular vel increase per second

    // Round tracking
    int m_roundNumber = 0;

    // Rendering
    rendering::PostProcessing m_postProcessing;
    sf::Font  m_font;
    bool      m_fontLoaded = false;

    // Arena wall segment count (for building the circle minus gap)
    static constexpr int WALL_SEGMENTS = 48;

    // RNG
    std::mt19937 m_rng;
};

} // namespace is::games::country_elimination
