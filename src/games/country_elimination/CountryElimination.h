#pragma once

#include "games/IGame.h"
#include "core/AvatarCache.h"
#include "rendering/PostProcessing.h"
#include "rendering/Background.h"

#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/Image.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/Graphics/VertexArray.hpp>
#include <box2d/box2d.h>
#include <vector>
#include <unordered_map>
#include <deque>
#include <random>

namespace is::games::country_elimination {

// ── Physics Constants (world-space meters) ───────────────────────────────────

static constexpr float PIXELS_PER_METER = 40.0f;

static constexpr float WORLD_W  = 27.0f;
static constexpr float WORLD_H  = 48.0f;
static constexpr float WORLD_CX = WORLD_W / 2.0f;
static constexpr float WORLD_CY = WORLD_H * 0.36f;

static constexpr float ARENA_RADIUS   = 7.5f;
static constexpr float WALL_THICKNESS = 0.35f;
static constexpr float BALL_RADIUS    = 0.45f;

// Default gap half-angle (~30° opening)
static constexpr float GAP_INITIAL = 0.26f;

// Reference mobile resolution for boundary placement (9:16 portrait)
static constexpr float REF_W = 1080.0f;
static constexpr float REF_H = 1920.0f;
static constexpr float REF_DIAM_W = REF_W * 0.88f;
static constexpr float REF_DIAM_H = REF_H * 0.44f;
static constexpr float REF_DIAM   = (REF_DIAM_W < REF_DIAM_H) ? REF_DIAM_W : REF_DIAM_H;
static constexpr float REF_PPM    = (REF_DIAM * 0.5f) / ARENA_RADIUS;

// Flag aspect ratio (60px wide × 41px tall)
static constexpr float FLAG_ASPECT = 60.0f / 41.0f;

// Visible-area boundaries — walls at exact screen edges
static constexpr float FLOOR_Y      = WORLD_CY + 17.0f;
static constexpr float CEILING_Y    = WORLD_CY - (REF_H * 0.36f) / REF_PPM - 0.5f;
static constexpr float WALL_LEFT_X  = WORLD_CX - (REF_W * 0.5f) / REF_PPM - 0.5f;
static constexpr float WALL_RIGHT_X = WORLD_CX + (REF_W * 0.5f) / REF_PPM + 0.5f;

// Physics segments
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
    std::string avatarUrl;
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

    bool isBot() const { return userId.rfind("__bot_", 0) == 0; }
};

// ── Screen Layout ────────────────────────────────────────────────────────────

struct ScreenLayout {
    float W, H;
    float arenaCX, arenaCY;
    float arenaRadiusPx;
    float ppm;
    float safeLeft, safeRight;
    float safeW;
    bool  isDesktop = false;  // true when aspect > ~9:16 (side panels visible)
    float leftPanelX, leftPanelW;   // left side panel bounds
    float rightPanelX, rightPanelW; // right side panel bounds
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
                 const std::string& label, const std::string& avatarUrl = "");
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
    void recreateArena();
    void createBoundaryWalls();
    b2Body* createPlayerBody(float x, float y, float radius);
    void recordRoundWin(const Player& winner);
    void enforceConstantVelocity();

    // Flags
    void generateFlagTextures();

    // Bots
    void spawnBots();
    void respawnDeadBots(float dt);

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
    void renderSidePanels(sf::RenderTarget& target, const ScreenLayout& L);

    sf::Color generateColor();

    // ── State ────────────────────────────────────────────────────────────

    GamePhase m_phase = GamePhase::Lobby;
    std::unordered_map<std::string, Player> m_players;
    std::deque<EliminationEntry>            m_eliminationFeed;
    std::string                             m_winnerId;
    std::vector<RoundWinEntry>              m_roundWinners;

    b2World*  m_world      = nullptr;
    b2Body*   m_arenaBody  = nullptr;
    b2Body*   m_floorBody  = nullptr;
    b2Body*   m_leftWall    = nullptr;
    b2Body*   m_rightWall   = nullptr;
    b2Body*   m_ceilingBody = nullptr;

    float m_arenaAngle       = 0.0f;
    float m_arenaAngularVel  = 0.3f;
    float m_arenaGlowPhase   = 0.0f;
    float m_globalTime       = 0.0f;

    // Dynamic gap (0 during lobby = closed ring, expands during battle)
    float m_currentGapAngle    = 0.0f;
    float m_arenaRebuildTimer  = 0.0f;

    double m_countdownTimer  = 0.0;
    double m_lobbyTimer      = 0.0;
    double m_roundTimer      = 0.0;
    double m_roundEndTimer   = 0.0;

    // ── Settings ─────────────────────────────────────────────────────────

    int    m_minPlayers         = 2;
    float  m_initialSpeed       = 5.0f;
    float  m_ballSpeedIncrease  = 0.5f;
    float  m_maxBallSpeed       = 15.0f;
    float  m_currentBallSpeed   = 5.0f;
    float  m_restitution        = 0.95f;
    double m_lobbyDuration      = 5.0;
    double m_roundEndDuration   = 4.0;
    float  m_arenaSpeedIncrease = 0.03f;
    float  m_gapExpansionRate   = 0.02f;
    float  m_gapMax             = 1.2f;
    int    m_championThreshold  = 4;
    double m_roundDuration      = 120.0;

    // Bot settings
    int   m_botFillTarget   = 8;
    int   m_botCounter      = 0;
    bool  m_botRespawn      = true;
    float m_botRespawnDelay = 3.0f;
    std::unordered_map<std::string, float> m_botRespawnTimers;

    // Eliminated player display settings
    int    m_maxEliminatedVisible = 20;
    float  m_elimFadeDuration     = 2.0f;
    float  m_elimLingerDuration   = 8.0f;
    bool   m_elimInfiniteLinger   = false;

    // Flag display
    bool   m_flagShapeRect        = false;

    // Eliminated player fade tracking (FIFO)
    struct EliminatedBall {
        std::string playerId;
        float       age = 0.0f;      // how long it's been eliminated
        bool        fading = false;   // in fade-out phase
        float       fadeProgress = 0; // 0..1
    };
    std::deque<EliminatedBall> m_eliminatedQueue;

    int  m_roundNumber = 0;
    bool m_gameWon     = false;
    std::string m_championId;

    // Visual size settings
    float  m_nameTextScale       = 1.0f;
    float  m_avatarScale         = 1.0f;

    rendering::PostProcessing m_postProcessing;
    rendering::Background     m_background;
    is::core::AvatarCache     m_avatarCache;
    sf::Font  m_font;
    bool      m_fontLoaded = false;
    std::vector<Particle> m_particles;
    std::unordered_map<std::string, sf::Texture> m_flagTextures;

    std::mt19937 m_rng;
};

} // namespace is::games::country_elimination
