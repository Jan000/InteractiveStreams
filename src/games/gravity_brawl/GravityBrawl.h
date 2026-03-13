#pragma once

#include "games/IGame.h"
#include "games/gravity_brawl/AvatarCache.h"
#include "rendering/Background.h"
#include "rendering/PostProcessing.h"

#include <SFML/Graphics/Font.hpp>
#include <box2d/box2d.h>
#include <vector>
#include <unordered_map>
#include <deque>
#include <random>
#include <memory>

namespace is::games::gravity_brawl {

struct GravityBrawlTestAccess;

// ── Constants ────────────────────────────────────────────────────────────────

static constexpr float ARENA_RADIUS      = 18.0f;   // meters – safe orbit radius
static constexpr float BLACK_HOLE_RADIUS = 2.5f;    // meters – kill zone
static constexpr float PIXELS_PER_METER  = 27.0f;   // world → screen
static constexpr float WORLD_CENTER_X    = 20.0f;   // world center (meters)
static constexpr float WORLD_CENTER_Y    = 35.5f;   // world center (meters)

// Screen dimensions (9:16 portrait)
static constexpr float SCREEN_W = 1080.0f;
static constexpr float SCREEN_H = 1920.0f;

// ── Visual Evolution Tiers ───────────────────────────────────────────────────

enum class PlanetTier {
    Asteroid,    // 0 kills  – small grey
    IcePlanet,   // 3 kills  – blue with trail
    GasGiant,    // 10 kills – red/orange, larger
    Star         // 25 kills – bright yellow/white, particles
};

inline PlanetTier tierFromKills(int kills) {
    if (kills >= 25) return PlanetTier::Star;
    if (kills >= 10) return PlanetTier::GasGiant;
    if (kills >= 3)  return PlanetTier::IcePlanet;
    return PlanetTier::Asteroid;
}

// ── Player ───────────────────────────────────────────────────────────────────

struct Planet {
    // Identity
    std::string userId;
    std::string displayName;
    std::string avatarUrl;

    // Physics (owned by Box2D world)
    b2Body*     body = nullptr;

    // State
    bool        alive          = true;
    int         kills          = 0;
    int         deaths         = 0;
    double      survivalTime   = 0.0;   // accumulated survival seconds
    int         score          = 0;
    int         hitCount       = 0;     // successful rams dealt

    // Visual evolution
    PlanetTier  tier           = PlanetTier::Asteroid;
    float       radiusMeters   = 0.5f;  // collision / visual radius (meters)
    float       baseRadius     = 0.5f;  // starting radius

    // Smash / spam tracking
    float       smashCooldown  = 0.0f;  // cooldown after a single dash
    double      lastSmashTime  = 0.0;   // time of last !s
    int         comboCount     = 0;     // !s presses within combo window
    double      comboWindowEnd = 0.0;   // when combo window expires

    // Last-hit tracking (for kill attribution)
    std::string lastHitBy;              // userId of last player that hit us
    double      lastHitTime    = 0.0;   // when the hit happened

    // Orbit direction: +1 = counter-clockwise, -1 = clockwise
    int         orbitDirection = 1;

    // Visual timers
    float       hitFlashTimer  = 0.0f;
    float       trailTimer     = 0.0f;
    float       animTimer      = 0.0f;
    float       glowPulse      = 0.0f;  // for star-tier pulsing
    float       supernovaTimer = 0.0f;  // visual shockwave effect
    float       smashVisTimer  = 0.0f;  // countdown for slash-arc visual
    sf::Vector2f smashDir      = {1.f, 0.f}; // dash direction for slash arc

    // Interpolation
    sf::Vector2f prevPosition;
    sf::Vector2f renderPosition;

    // Whether this player is the current king (bounty target)
    bool        isKing         = false;

    // Respawn cooldown: player cannot rejoin before this game-timer value
    double      nextJoinTime   = 0.0;

    /// Returns true if this planet is a bot (AI-controlled filler).
    bool isBot() const { return userId.rfind("__bot_", 0) == 0; }

    void updateTimers(float dt) {
        smashCooldown  = std::max(0.0f, smashCooldown - dt);
        hitFlashTimer  = std::max(0.0f, hitFlashTimer - dt);
        trailTimer     = std::max(0.0f, trailTimer    - dt);
        supernovaTimer = std::max(0.0f, supernovaTimer - dt);
        smashVisTimer  = std::max(0.0f, smashVisTimer  - dt);
        animTimer     += dt;
        glowPulse     += dt * 2.0f;

        // Update tier based on kills
        tier = tierFromKills(kills);
    }

    /// Get visual radius in meters (grows with tier)
    float getVisualRadius() const {
        return radiusMeters;
    }

    /// Get mass multiplier (grows with tier, king is heavier)
    float getMassScale() const {
        float scale = massScale;
        if (isKing) scale *= 1.5f;
        return scale;
    }

    // Per-tier values (set by game on tier change)
    float radiusByTier = 0.5f;   // radius assigned by current tier
    float massByTier   = 1.0f;   // mass multiplier assigned by current tier
    float massScale    = 1.0f;   // effective mass scale (massByTier + per-kill bonus)

    // Combo constants
    static constexpr float  SMASH_COOLDOWN    = 0.8f;  // seconds between dashes
    static constexpr double COMBO_WINDOW      = 3.0;   // seconds for combo tracking
    static constexpr int    SUPERNOVA_COMBO   = 5;     // !s count for supernova
};

// ── Kill Feed ────────────────────────────────────────────────────────────────

struct KillFeedEntry {
    std::string killer;
    std::string victim;
    bool        wasBounty;     // was victim the king?
    double      timeRemaining;
};

// ── Particle ─────────────────────────────────────────────────────────────────

struct Particle {
    sf::Vector2f position;
    sf::Vector2f velocity;
    sf::Color    color;
    float        life    = 1.0f;
    float        maxLife = 1.0f;
    float        size    = 3.0f;
    float        drag    = 0.99f;
    bool         fadeAlpha = true;
    bool         shrink   = true;
};

// ── Game Phases ──────────────────────────────────────────────────────────────

enum class GamePhase {
    Lobby,       ///< Waiting for players
    Countdown,   ///< 3-2-1 before play
    Playing,     ///< Main game loop
    GameOver     ///< Final scoreboard
};

// ── The Game ─────────────────────────────────────────────────────────────────

class GravityBrawl : public IGame {
public:
    GravityBrawl();
    ~GravityBrawl() override;

    // IGame interface
    std::string id() const override { return "gravity_brawl"; }
    std::string displayName() const override { return "Gravity Brawl"; }
    std::string description() const override {
         return "Galactic moshpit! Orbit the black hole, type 's' to smash enemies into the void. "
             "Survive epoch-ending Void Eruptions to earn bonus points!";
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

    friend struct GravityBrawlTestAccess;

private:
    // ── Commands ─────────────────────────────────────────────────────────
    void cmdJoin(const std::string& userId, const std::string& displayName,
                 const std::string& avatarUrl = "");
    void cmdSmash(const std::string& userId);
    void handleStreamEvent(const platform::ChatMessage& msg);

    // ── Bot Fill ─────────────────────────────────────────────────────────
    void spawnBots();
    void updateBotAI(float dt);
    void respawnDeadBots(float dt);
    int   m_botFillTarget          = 0;      ///< Target player count to fill with bots (0 = disabled)
    int   m_botCounter              = 0;      ///< Counter for generating unique bot IDs
    float m_botAITimer              = 0.0f;   ///< Timer for bot decision-making
    bool  m_botKillFeed             = false;  ///< Show bots in the kill feed
    bool  m_botRespawn              = false;  ///< Respawn dead bots during play
    float m_botRespawnDelay         = 5.0f;   ///< Seconds before a dead bot respawns
    float m_botActionInterval       = 0.3f;   ///< Seconds between bot AI decisions
    float m_botSmashChance          = 0.2f;   ///< Base probability of smash per tick
    float m_botDangerSmashChance    = 0.6f;   ///< Smash probability when near black hole
    float m_botEventSmashChance     = 0.7f;   ///< Smash probability during cosmic events
    std::unordered_map<std::string, float> m_botRespawnTimers; ///< Dead bot → remaining respawn delay

    // ── Game logic ──────────────────────────────────────────────────────
    void startCountdown();  ///< Legacy: calls startPlaying() directly (no countdown in endless mode)
    void startPlaying();    ///< Resets game state; used by tests and bot fill
    void applyOrbitalForces(float dt);
    void applyBlackHoleGravity(float dt);
    void checkBlackHoleDeaths();
    void triggerSmash(Planet& p);
    void triggerSupernova(Planet& p);
    void updateKing();
    void triggerCosmicEvent();
    void updateCosmicEvent(float dt);
    void awardSurvivalPoints(double dt);
    void eliminatePlanet(Planet& p);
    void spawnPlanetBody(Planet& p);
    float chooseSpawnAngle();
    void updatePlanetTier(Planet& p);   ///< Sync tier, radius, mass & Box2D fixture

    // ── Rendering ───────────────────────────────────────────────────────
    void renderBlackHole(sf::RenderTarget& target);
    void renderPlanets(sf::RenderTarget& target, double alpha);
    void renderPlanet(sf::RenderTarget& target, const Planet& p, sf::Vector2f screenPos);
    void renderCrown(sf::RenderTarget& target, sf::Vector2f pos, float radius);
    void renderOrbitTrails(sf::RenderTarget& target);
    void renderParticles(sf::RenderTarget& target);
    void renderKillFeed(sf::RenderTarget& target);
    void renderUI(sf::RenderTarget& target);
    void renderCosmicEventWarning(sf::RenderTarget& target);
    void renderFloatingTexts(sf::RenderTarget& target);
    float currentBlackHoleGravity() const;

    /// Scale a font size by the current font scale factor.
    unsigned int fs(int base) const {
        return static_cast<unsigned int>(std::max(1.0f, base * m_fontScale));
    }

    /// Convert world coords → screen pixel coords.
    sf::Vector2f worldToScreen(float wx, float wy) const;
    sf::Vector2f worldToScreen(b2Vec2 pos) const;

    // ── Particles ───────────────────────────────────────────────────────
    void emitParticles(sf::Vector2f pos, sf::Color color, int count,
                       float speed = 100.f, float spread = 360.f, float life = 1.f);
    void emitExplosion(sf::Vector2f pos, sf::Color color, int count = 60);
    void emitTrail(sf::Vector2f pos, sf::Color color);
    void emitSupernovaWave(sf::Vector2f pos, sf::Color color);
    void updateParticles(float dt);

    // ── Floating text ("+50", etc) ──────────────────────────────────────
    struct FloatingText {
        std::string text;
        sf::Vector2f position;
        sf::Color color;
        float timer;
        float maxTime;
    };
    void addFloatingText(const std::string& text, sf::Vector2f pos, sf::Color color, float duration = 1.5f);

    // ── State ───────────────────────────────────────────────────────────
    GamePhase                                 m_phase = GamePhase::Lobby;
    std::unordered_map<std::string, Planet>   m_planets;     // userId -> Planet
    std::deque<KillFeedEntry>                 m_killFeed;
    std::vector<Particle>                     m_particles;
    std::vector<FloatingText>                 m_floatingTexts;

    // Physics
    std::unique_ptr<b2World>                  m_world;

    // Rendering
    rendering::Background      m_background;
    rendering::PostProcessing  m_postProcessing;
    AvatarCache                m_avatarCache;
    sf::Font                   m_font;
    bool                       m_fontLoaded = false;

    // Timing
    // Timing
    double m_countdownTimer      = 0.0;   // legacy – kept for test accessor compat
    int    m_lastCountdownBeep   = 0;     // legacy
    double m_gameTimer           = 0.0;   // total elapsed time (counts up forever)
    double m_lobbyTimer          = 0.0;   // legacy – kept for test accessor compat
    double m_survivalAccum       = 0.0;   // fractional survival point accumulator
    double m_gameDuration        = 300.0; // legacy (was per-round timer; kept for configure() compat)
    double m_lobbyDuration       = 30.0;  // legacy
    int    m_minPlayers          = 2;     // legacy

    // Epoch system (breathing black hole)
    double m_blackHoleEpochTimer = 900.0; // counts down to next Void Eruption
    double m_epochDuration       = 900.0; // full cycle length in seconds

    // CPU safety and runtime tuning (headless software-rendering defaults)
    bool   m_enablePostProcessing = false;
    int    m_maxParticles         = 500;
    double m_afkTimeoutSeconds    = 180.0;
    double m_anomalySpawnInterval = 150.0;
    double m_avatarCleanupTimer   = 0.0;

    // Cosmic Event (black hole pulse)
    double m_cosmicEventCooldown = 60.0;  // seconds between events
    double m_cosmicEventTimer    = 60.0;  // countdown to next event
    double m_cosmicEventDuration = 10.0;  // how long the event lasts
    double m_cosmicEventActive   = 0.0;   // > 0 means event is active
    float  m_spawnRadiusFactor            = 0.92f;
    float  m_spawnOrbitSpeed              = 7.25f;
    float  m_safeOrbitRadiusFactor        = 0.78f;
    float  m_orbitalGravityStrength       = 8.0f;
    float  m_orbitalOuterPullMultiplier   = 1.25f;
    float  m_orbitalSafeZonePullMultiplier = 0.35f;
    float  m_orbitalTangentialStrength    = 8.75f;
    float  m_blackHoleBaseGravity         = 6.0f;
    float  m_blackHoleTimeGrowthFactor    = 0.02f;
    float  m_blackHoleConsumeSizeFactor   = 1.75f;
    float  m_blackHoleConsumedGravityBonus = 0.0f;
    float  m_blackHoleGravityCap          = 30.0f;
    float  m_blackHoleKillRadiusMultiplier = 1.0f;
    float  m_eventGravityMul              = 2.2f;  // multiplier during event

    // Black hole visual
    float  m_blackHolePulse      = 0.0f;
    float  m_blackHoleRotation   = 0.0f;

    // ── Dynamic Camera Zoom ─────────────────────────────────────────────
    bool   m_cameraZoomEnabled    = true;   // feature toggle
    float  m_cameraZoom           = 1.0f;   // current zoom (1.0 = default, <1 = zoomed out)
    float  m_cameraTargetZoom     = 1.0f;   // smoothly interpolated toward this
    float  m_cameraZoomSpeed      = 3.5f;   // lerp speed (higher = faster tracking)
    float  m_cameraBufferMeters   = 1.5f;   // buffer beyond outermost player before zoom kicks in
    float  m_cameraMinZoom        = 0.3f;   // minimum allowed zoom (max zoom-out)
    float  m_cameraMaxZoom        = 2.5f;   // maximum allowed zoom (max zoom-in)

    /// Recompute m_cameraTargetZoom from current player positions.
    void   updateCameraZoom(float dt);

    // ── Per-Tier Planet Settings ─────────────────────────────────────────
    // Index: 0=Asteroid, 1=IcePlanet, 2=GasGiant, 3=Star
    float  m_tierRadius[4] = {0.5f, 0.7f, 0.95f, 1.25f};   // collision+visual radius (meters)
    float  m_tierMass[4]   = {1.0f, 1.5f, 2.2f,  3.5f};    // mass multiplier per tier

    // King tracking
    std::string m_currentKingId;

    // ── Sound Effects ───────────────────────────────────────────────────
    bool  m_sfxEnabled     = true;   ///< Master toggle for game SFX
    float m_sfxVolume      = 80.0f;  ///< Per-game volume (0–100)
    void  loadSfx();                 ///< Load all SFX from assets/audio/sfx/gravity_brawl/
    void  playSfx(const std::string& name, float volumeScale = 1.0f);
    bool  m_sfxLoaded      = false;  ///< True after first loadSfx() call

    // RNG
    std::mt19937 m_rng;

    // Hard upper bound regardless of runtime setting
    static constexpr size_t MAX_PARTICLES_HARD_CAP = 5000;

    // Trail emission rate limiter (seconds between trail emissions per planet)
    static constexpr float TRAIL_EMIT_INTERVAL = 0.05f; // 20 Hz instead of every frame
    float m_trailEmitTimer = 0.0f;

    // Vertex array for batched particle rendering
    sf::VertexArray m_particleVerts;

    // Contact listener
    class ContactListener : public b2ContactListener {
    public:
        explicit ContactListener(GravityBrawl& game) : m_game(game) {}
        void BeginContact(b2Contact* contact) override;
    private:
        GravityBrawl& m_game;
    };
    std::unique_ptr<ContactListener> m_contactListener;

    // Process a collision between two planets
    void onPlanetCollision(Planet& a, Planet& b, float impulse);
};

} // namespace is::games::gravity_brawl
