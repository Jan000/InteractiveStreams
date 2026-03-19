#pragma once

#include "games/IGame.h"
#include "games/country_elimination/QuizQuestions.h"
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
static constexpr float WALL_LEFT_X  = WORLD_CX - (REF_W * 0.5f) / REF_PPM - 0.25f;
static constexpr float WALL_RIGHT_X = WORLD_CX + (REF_W * 0.5f) / REF_PPM + 0.25f;

// Physics segments
static constexpr int WALL_SEGMENTS = 64;

// Visual ring smoothness
static constexpr int RING_RESOLUTION = 180;

// ── Box2D Collision Categories ────────────────────────────────────────────────

static constexpr uint16_t CAT_ALIVE      = 0x0001; // Alive players
static constexpr uint16_t CAT_ARENA      = 0x0002; // Arena ring
static constexpr uint16_t CAT_ELIMINATED = 0x0004; // Eliminated players
static constexpr uint16_t CAT_BOUNDARY   = 0x0008; // Floor, walls

// Alive players collide with: alive + arena + boundaries
static constexpr uint16_t MASK_ALIVE     = CAT_ALIVE | CAT_ARENA | CAT_BOUNDARY;
// Eliminated (re-entry OFF): only other eliminated + boundaries
static constexpr uint16_t MASK_ELIM_NOREENTRY = CAT_ELIMINATED | CAT_BOUNDARY;
// Eliminated (re-entry ON):  everything (to be able to bounce back in)
static constexpr uint16_t MASK_ELIM_REENTRY   = 0xFFFF;
// Arena ring collides with: alive players only
static constexpr uint16_t MASK_ARENA     = CAT_ALIVE;
// Boundaries collide with: everything
static constexpr uint16_t MASK_BOUNDARY  = 0xFFFF;

// ── Enums ────────────────────────────────────────────────────────────────────

enum class GamePhase { Lobby, Countdown, Battle, RoundEnd };

// ── Data Structures ──────────────────────────────────────────────────────────

struct EliminationEntry {
    std::string displayName;
    std::string label;
    std::string avatarUrl;
    sf::Color   color;
    double      timeRemaining;
};

struct RoundWinEntry {
    std::string userId;
    std::string displayName;
    std::string label;
    std::string avatarUrl;
    sf::Color   color;
    int         wins = 0;
};

struct CountryWinEntry {
    std::string label;      // country code (uppercased)
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
    bool        flagless   = false;  // joined without a flag — avatar shown on ball

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
        return static_cast<unsigned int>(std::max(20.0f, base * m_fontScale));
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
                 const std::string& label, const std::string& avatarUrl = "",
                 bool flagless = false);
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
    void recordCountryWin(const std::string& label);
    void refreshPlayerLeaderboardCache();
    void enforceConstantVelocity();

    // Flags
    void generateFlagTextures();

    // Bots
    void spawnBots();          // immediate fill (used at initialize)
    void scheduleBotSpawns();  // staggered fill via timers
    void tickBotSpawnTimers(float dt);
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
    void renderQuizOverlay(sf::RenderTarget& target, const ScreenLayout& L);
    void renderVisualizer(sf::RenderTarget& target, const ScreenLayout& L);

    // Quiz
    void startQuiz();
    void endQuiz();
    void updateQuiz(float dt);
    void handleQuizAnswer(const std::string& userId, const std::string& displayName,
                          int answerIndex);

    sf::Color generateColor();

    // ── State ────────────────────────────────────────────────────────────

    GamePhase m_phase = GamePhase::Lobby;
    std::unordered_map<std::string, Player> m_players;
    std::deque<EliminationEntry>            m_eliminationFeed;
    std::string                             m_winnerId;
    std::vector<RoundWinEntry>              m_roundWinners;
    std::vector<CountryWinEntry>            m_countryWins;

    // Player leaderboard cache (from PlayerDatabase)
    struct CachedPlayerEntry {
        std::string displayName;
        std::string avatarUrl;
        int points = 0;
        int wins   = 0;
    };
    std::vector<CachedPlayerEntry> m_cachedPlayerLeaderboard;
    double m_leaderboardCacheTimer = 0.0;

    b2World*  m_world      = nullptr;
    b2Body*   m_arenaBody  = nullptr;
    b2Body*   m_floorBody  = nullptr;
    b2Body*   m_leftWall    = nullptr;
    b2Body*   m_rightWall   = nullptr;

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
    float  m_arenaSpeedDefault  = 0.3f;   // configurable default arena rotation
    float  m_gapExpansionRate   = 0.02f;
    float  m_gapInitial         = GAP_INITIAL;
    float  m_gapMax             = 1.2f;
    int    m_championThreshold  = 4;
    double m_roundDuration      = 120.0;

    // Configurable sizes
    float  m_ballRadius         = BALL_RADIUS;
    float  m_wallThickness      = WALL_THICKNESS;
    float  m_arenaRadius        = ARENA_RADIUS;
    float  m_countdownDuration  = 3.0f;
    float  m_gravity            = 15.0f;
    int    m_elimFeedMax        = 8;

    // Stream-event reward settings
    float  m_shieldDurationSub       = 15.0f;
    float  m_shieldDurationSuperchat = 20.0f;
    float  m_shieldDurationPoints    = 10.0f;
    int    m_scoreWin           = 100;
    int    m_scoreSub           = 300;
    int    m_scoreSuperchat     = 500;
    int    m_scorePoints        = 100;
    int    m_scoreParticipation = 1;

    // Bot settings
    int   m_botFillTarget   = 8;
    int   m_botCounter      = 0;
    bool  m_botRespawn      = true;
    float m_botRespawnDelay = 3.0f;
    std::unordered_map<std::string, float> m_botRespawnTimers;

    // Staggered bot spawn queue (used at round start / init)
    struct PendingBotSpawn {
        float timer;
        std::string name;
        std::string label;
    };
    std::vector<PendingBotSpawn> m_pendingBotSpawns;

    // Eliminated player display settings
    int    m_maxEliminatedVisible = 20;
    float  m_elimFadeDuration     = 2.0f;
    float  m_elimLingerDuration   = 8.0f;
    bool   m_elimInfiniteLinger   = false;
    bool   m_elimPersistRounds    = false;

    // Flag display
    bool   m_flagShapeRect        = false;
    bool   m_flagOutline          = true;
    float  m_flagOutlineThickness = 1.5f;

    // Multi-join
    int    m_maxEntriesPerPlayer = 1;

    // Visual / gameplay toggles
    bool   m_rainbowRing    = true;
    bool   m_allowReentry   = true;
    bool   m_showBotNames   = true;
    bool   m_allowFlaglessJoin  = false;  // allow join/play without a country
    bool   m_autoDetectCountry  = true;   // detect country from any message

    // Audio visualizer
    bool   m_visualizerEnabled = false;
    int    m_visualizerStyle   = 0;       // 0=Bars, 1=Dots, 2=Pulse, 3=Wave
    float  m_visualizerHeight  = 40.0f;   // max bar height in pixels
    float  m_visualizerOpacity = 0.7f;    // 0..1
    int    m_visualizerBands   = 64;      // number of bands to display
    float  m_visualizerSmoothing = 0.3f;  // smoothing factor (0=no smoothing, 1=max)
    float  m_visualizerGain     = 2.0f;   // amplitude gain multiplier
    std::vector<float> m_vizSmoothed;     // smoothed band values

    // Country leaderboard panel settings
    int    m_leaderboardMaxEntries  = 10;  // max entries shown in panel
    int    m_leaderboardFontSize    = 24;  // base font size for entries
    float  m_leaderboardFlagSize    = 1.0f; // flag radius multiplier
    bool   m_leaderboardShowCodes   = true; // show country code labels
    float  m_leaderboardTextScale   = 1.0f; // text scale multiplier

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
    float  m_nameTextScale          = 1.0f;
    float  m_labelTextScale         = 1.0f;
    float  m_avatarScale            = 1.0f;
    float  m_avatarOutlineThickness = 1.0f;

    // ── Quiz State ───────────────────────────────────────────────────────

    bool   m_quizEnabled       = true;
    float  m_quizInterval      = 30.0f;  // seconds between quizzes
    float  m_quizDuration      = 15.0f;  // seconds to answer
    int    m_quizPoints        = 50;     // points for correct answer
    float  m_quizShieldSecs    = 10.0f;  // shield duration for correct answer

    bool   m_quizActive        = false;
    float  m_quizTimer         = 0.0f;   // countdown while quiz is active
    float  m_quizCooldown      = 0.0f;   // countdown until next quiz
    int    m_quizCurrentIdx    = -1;     // index into catalog
    float  m_quizRevealTimer   = 0.0f;   // time showing correct answer after quiz ends
    int    m_quizCorrectCount  = 0;      // how many got it right this round

    std::vector<int> m_quizOrder;        // shuffled question indices
    int    m_quizOrderPos      = 0;      // position in shuffled order

    // userId → answer index (0–3), -1 = not yet answered
    std::unordered_map<std::string, int> m_quizAnswers;

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
