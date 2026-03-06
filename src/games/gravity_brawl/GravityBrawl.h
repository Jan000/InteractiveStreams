#pragma once

#include "games/IGame.h"
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

    // Visual timers
    float       hitFlashTimer  = 0.0f;
    float       trailTimer     = 0.0f;
    float       animTimer      = 0.0f;
    float       glowPulse      = 0.0f;  // for star-tier pulsing
    float       supernovaTimer = 0.0f;  // visual shockwave effect

    // Interpolation
    sf::Vector2f prevPosition;
    sf::Vector2f renderPosition;

    // Whether this player is the current king (bounty target)
    bool        isKing         = false;

    void updateTimers(float dt) {
        smashCooldown  = std::max(0.0f, smashCooldown - dt);
        hitFlashTimer  = std::max(0.0f, hitFlashTimer - dt);
        trailTimer     = std::max(0.0f, trailTimer    - dt);
        supernovaTimer = std::max(0.0f, supernovaTimer - dt);
        animTimer     += dt;
        glowPulse     += dt * 2.0f;

        // Update tier based on kills
        tier = tierFromKills(kills);
    }

    /// Get visual radius in meters (grows with kills)
    float getVisualRadius() const {
        float bonus = kills * 0.02f;  // +2% per kill
        return baseRadius + bonus;
    }

    /// Get mass multiplier (king is heavier)
    float getMassScale() const {
        float scale = 1.0f + kills * 0.05f;
        if (isKing) scale *= 1.5f;
        return scale;
    }

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
        return "Galactic moshpit! Orbit the black hole, !s to smash enemies into the void. "
               "Last planet standing earns the crown!";
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

private:
    // ── Commands ─────────────────────────────────────────────────────────
    void cmdJoin(const std::string& userId, const std::string& displayName);
    void cmdSmash(const std::string& userId);

    // ── Game logic ──────────────────────────────────────────────────────
    void startCountdown();
    void startPlaying();
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

    // ── Rendering ───────────────────────────────────────────────────────
    void renderBlackHole(sf::RenderTarget& target);
    void renderPlanets(sf::RenderTarget& target, double alpha);
    void renderPlanet(sf::RenderTarget& target, const Planet& p, sf::Vector2f screenPos);
    void renderCrown(sf::RenderTarget& target, sf::Vector2f pos, float radius);
    void renderOrbitTrails(sf::RenderTarget& target);
    void renderParticles(sf::RenderTarget& target);
    void renderKillFeed(sf::RenderTarget& target);
    void renderLeaderboard(sf::RenderTarget& target);
    void renderUI(sf::RenderTarget& target);
    void renderCountdown(sf::RenderTarget& target);
    void renderCosmicEventWarning(sf::RenderTarget& target);
    void renderFloatingTexts(sf::RenderTarget& target);

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
    sf::Font                   m_font;
    bool                       m_fontLoaded = false;

    // Timing
    double m_countdownTimer    = 0.0;
    double m_gameTimer         = 0.0;  // total elapsed game time
    double m_lobbyTimer        = 0.0;
    double m_survivalAccum     = 0.0;  // fractional survival point accumulator
    double m_gameDuration      = 300.0; // 5 minutes per session
    double m_lobbyDuration     = 30.0;
    int    m_minPlayers        = 2;

    // Cosmic Event (black hole pulse)
    double m_cosmicEventCooldown = 60.0;  // seconds between events
    double m_cosmicEventTimer    = 60.0;  // countdown to next event
    double m_cosmicEventDuration = 10.0;  // how long the event lasts
    double m_cosmicEventActive   = 0.0;   // > 0 means event is active
    float  m_normalGravity       = 12.0f; // base pull toward center
    float  m_eventGravityMul     = 3.0f;  // multiplier during event

    // Black hole visual
    float  m_blackHolePulse      = 0.0f;
    float  m_blackHoleRotation   = 0.0f;

    // King tracking
    std::string m_currentKingId;

    // RNG
    std::mt19937 m_rng;

    // Particle capacity
    static constexpr size_t MAX_PARTICLES = 20000;

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
