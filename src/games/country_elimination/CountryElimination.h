#pragma once

#include "games/IGame.h"
#include "rendering/PostProcessing.h"
#include "rendering/Background.h"

#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/VertexArray.hpp>
#include <box2d/box2d.h>
#include <vector>
#include <unordered_map>
#include <deque>
#include <random>

namespace is::games::country_elimination {

// ── Physics Constants (world-space meters) ───────────────────────────────────

static constexpr float PIXELS_PER_METER = 40.0f;

// World dimensions (based on 1080x1920 at PPM=40)
static constexpr float WORLD_W  = 27.0f;
static constexpr float WORLD_H  = 48.0f;
static constexpr float WORLD_CX = WORLD_W / 2.0f;         // 13.5m
static constexpr float WORLD_CY = WORLD_H * 0.36f;        // ~17.3m

static constexpr float ARENA_RADIUS   = 7.5f;
static constexpr float WALL_THICKNESS = 0.35f;
static constexpr float BALL_RADIUS    = 0.45f;

// Gap (~30° opening)
static constexpr float GAP_HALF_ANGLE = 0.26f;

// Floor for eliminated balls
static constexpr float FLOOR_Y = WORLD_H - 1.5f;

// Physics segments (higher = smoother collision hull)
static constexpr int WALL_SEGMENTS = 64;

// Visual ring smoothness
static constexpr int RING_RESOLUTION = 180;

// ── Enums ────────────────────────────────────────────────────────────────────

enum class GamePhase { Lobby, Countdown, Battle, RoundEnd };

// ── Data Structures ──────────────────────────────────────────────────────────

struct EliminationEntry {
    std::string displayName;
    std::string label;
    sf::Color   color;
    double      timeRemaining;
};

struct RoundWinEntry {
    std::string userId;
    std::string displayName;
    std::string label;
    sf::Color   color;
    int         wins = 0;
};

struct Particle {
    sf::Vector2f pos, vel;
    sf::Color    color;
    float        life, maxLife, size;
};

struct Player {
    std::string userId;
    std::string displayName;
    std::string label;
    sf::Color   color;

    b2Body*     body       = nullptr;
    float       radiusM    = BALL_RADIUS;
    bool        alive      = true;
    bool        eliminated = false;
    bool        hasShield  = false;
    float       shieldTimer = 0.0f;
    int         score      = 0;

    b2Vec2      prevPos{0.0f, 0.0f};
    b2Vec2      currPos{0.0f, 0.0f};
};

// ── Screen Layout (computed per-frame from render target) ────────────────────

struct ScreenLayout {
    float W, H;
    float arenaCX, arenaCY;
    float arenaRadiusPx;
    float ppm;
    // 9:16 safe zone
    float safeLeft, safeRight;
    float safeW;
};

// ── Main Game Class ──────────────────────────────────────────────────────────

class CountryElimination : public IGame {
public:
    CountryElimination();
    ~CountryElimination() override;

    std::string id() const override { return "country_elimination"; }
    std::string displayName() const override { return "Country Elimination"; }
    std::string description() const override {
        return "Marble race elimination! Type join <country> — survive the rotating arena. Last ball standing wins!";
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
    unsigned int fs(int base) const {
        return static_cast<unsigned int>(std::max(1.0f, base * m_fontScale));
    }

    ScreenLayout computeLayout(const sf::RenderTarget& target) const;

    sf::Vector2f worldToScreen(const ScreenLayout& L, float wx, float wy) const {
        return { L.arenaCX + (wx - WORLD_CX) * L.ppm,
                 L.arenaCY + (wy - WORLD_CY) * L.ppm };
    }
    sf::Vector2f worldToScreen(const ScreenLayout& L, b2Vec2 v) const {
        return worldToScreen(L, v.x, v.y);
    }

    // Commands
    void cmdJoin(const std::string& userId, const std::string& displayName,
                 const std::string& label);
    void handleStreamEvent(const platform::ChatMessage& msg);

    // Game logic
    void startCountdown();
    void startBattle();
    void checkEliminations();
    void checkRoundEnd();
    void endRound();
    void resetForNextRound();
    void createArenaBody();
    void destroyArenaBody();
    b2Body* createPlayerBody(float x, float y, float radius);
    void recordRoundWin(const Player& winner);

    // Particles
    void emitParticles(sf::Vector2f pos, sf::Color color, int count,
                       float speed, float life, float size = 3.0f);
    void emitCelebration(sf::Vector2f pos);
    void updateParticles(float dt);

    // Rendering
    void renderBackground(sf::RenderTarget& target, const ScreenLayout& L);
    void renderArena(sf::RenderTarget& target, const ScreenLayout& L);
    void renderPlayers(sf::RenderTarget& target, const ScreenLayout& L, double alpha);
    void renderParticles(sf::RenderTarget& target);
    void renderTimer(sf::RenderTarget& target, const ScreenLayout& L);
    void renderUI(sf::RenderTarget& target, const ScreenLayout& L);
    void renderCountdown(sf::RenderTarget& target, const ScreenLayout& L);
    void renderRoundWinners(sf::RenderTarget& target, const ScreenLayout& L);
    void renderWinnerOverlay(sf::RenderTarget& target, const ScreenLayout& L);
    void renderEliminationFeed(sf::RenderTarget& target, const ScreenLayout& L);

    sf::Color generateColor();

    // ── State ────────────────────────────────────────────────────────────────

    GamePhase m_phase = GamePhase::Lobby;
    std::unordered_map<std::string, Player> m_players;
    std::deque<EliminationEntry>            m_eliminationFeed;
    std::string                             m_winnerId;
    std::vector<RoundWinEntry>              m_roundWinners;

    b2World*  m_world      = nullptr;
    b2Body*   m_arenaBody  = nullptr;
    b2Body*   m_floorBody  = nullptr;

    float m_arenaAngle       = 0.0f;
    float m_arenaAngularVel  = 0.3f;
    float m_arenaGlowPhase   = 0.0f;
    float m_globalTime       = 0.0f;

    double m_countdownTimer  = 0.0;
    double m_lobbyTimer      = 0.0;
    double m_roundTimer      = 0.0;
    double m_roundEndTimer   = 0.0;

    int    m_minPlayers        = 2;
    float  m_initialSpeed      = 5.0f;
    float  m_restitution       = 0.95f;
    double m_lobbyDuration     = 30.0;
    double m_roundEndDuration  = 5.0;
    float  m_arenaSpeedIncrease = 0.02f;
    int    m_championThreshold  = 4;

    int  m_roundNumber = 0;
    bool m_gameWon     = false;
    std::string m_championId;

    rendering::PostProcessing m_postProcessing;
    rendering::Background     m_background;
    sf::Font  m_font;
    bool      m_fontLoaded = false;
    std::vector<Particle> m_particles;

    std::mt19937 m_rng;
};

} // namespace is::games::country_elimination
