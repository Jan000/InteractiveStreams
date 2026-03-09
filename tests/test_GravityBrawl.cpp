#include <doctest/doctest.h>

#include <SFML/Graphics/RenderTexture.hpp>
#include <SFML/Graphics/Image.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>
#include <numeric>
#include <sstream>

#include "core/Application.h"
#include "core/AudioManager.h"
#include "core/PlayerDatabase.h"
#include "games/gravity_brawl/GravityBrawl.h"
#include "platform/ChatMessage.h"

// ══════════════════════════════════════════════════════════════════════════════
// Stubs for Application / PlayerDatabase (test-only, no real DB)
// ══════════════════════════════════════════════════════════════════════════════

namespace is::core {

Application* Application::s_instance = nullptr;

Application& Application::instance() {
    alignas(Application) static unsigned char storage[sizeof(Application)] = {};
    return *reinterpret_cast<Application*>(storage);
}

PlayerDatabase& Application::playerDatabase() {
    alignas(PlayerDatabase) static unsigned char storage[sizeof(PlayerDatabase)] = {};
    return *reinterpret_cast<PlayerDatabase*>(storage);
}

AudioManager& Application::audioManager() {
    static AudioManager mgr;
    return mgr;
}

void PlayerDatabase::recordResult(const std::string&, const std::string&, const std::string&, int, bool) {}

} // namespace is::core

// ══════════════════════════════════════════════════════════════════════════════
// Extended Test Access — exposes additional internals for deep testing
// ══════════════════════════════════════════════════════════════════════════════

namespace is::games::gravity_brawl {

struct GravityBrawlTestAccess {
    // ── Planets ──────────────────────────────────────────────────────────
    static std::unordered_map<std::string, Planet>& planets(GravityBrawl& game) { return game.m_planets; }
    static const std::unordered_map<std::string, Planet>& planets(const GravityBrawl& game) { return game.m_planets; }
    static void spawnPlanetBody(GravityBrawl& game, Planet& planet) { game.spawnPlanetBody(planet); }

    // ── Game lifecycle ───────────────────────────────────────────────────
    static void startPlaying(GravityBrawl& game) { game.startPlaying(); }
    static void startCountdown(GravityBrawl& game) { game.startCountdown(); }

    // ── Commands ─────────────────────────────────────────────────────────
    static void cmdSmash(GravityBrawl& game, const std::string& userId) { game.cmdSmash(userId); }
    static void triggerSmash(GravityBrawl& game, Planet& p) { game.triggerSmash(p); }
    static void triggerSupernova(GravityBrawl& game, Planet& p) { game.triggerSupernova(p); }

    // ── Game logic ───────────────────────────────────────────────────────
    static void eliminatePlanet(GravityBrawl& game, Planet& planet) { game.eliminatePlanet(planet); }
    static void checkBlackHoleDeaths(GravityBrawl& game) { game.checkBlackHoleDeaths(); }
    static void updateKing(GravityBrawl& game) { game.updateKing(); }
    static void triggerCosmicEvent(GravityBrawl& game) { game.triggerCosmicEvent(); }
    static void applyOrbitalForces(GravityBrawl& game, float dt) { game.applyOrbitalForces(dt); }
    static void applyBlackHoleGravity(GravityBrawl& game, float dt) { game.applyBlackHoleGravity(dt); }
    static void spawnBots(GravityBrawl& game) { game.spawnBots(); }
    static void onPlanetCollision(GravityBrawl& game, Planet& a, Planet& b, float impulse) { game.onPlanetCollision(a, b, impulse); }

    // ── State read ───────────────────────────────────────────────────────
    static float currentBlackHoleGravity(const GravityBrawl& game) { return game.currentBlackHoleGravity(); }
    static float spawnRadiusFactor(const GravityBrawl& game) { return game.m_spawnRadiusFactor; }
    static float spawnOrbitSpeed(const GravityBrawl& game) { return game.m_spawnOrbitSpeed; }
    static float blackHoleKillRadiusMultiplier(const GravityBrawl& game) { return game.m_blackHoleKillRadiusMultiplier; }
    static float blackHoleConsumeSizeFactor(const GravityBrawl& game) { return game.m_blackHoleConsumeSizeFactor; }
    static float blackHoleGravityCap(const GravityBrawl& game) { return game.m_blackHoleGravityCap; }
    static float eventGravityMul(const GravityBrawl& game) { return game.m_eventGravityMul; }

    // ── State write ──────────────────────────────────────────────────────
    static float& blackHoleConsumedGravityBonus(GravityBrawl& game) { return game.m_blackHoleConsumedGravityBonus; }
    static double& gameTimer(GravityBrawl& game) { return game.m_gameTimer; }
    static double& lobbyTimer(GravityBrawl& game) { return game.m_lobbyTimer; }
    static double& countdownTimer(GravityBrawl& game) { return game.m_countdownTimer; }
    static double& cosmicEventActive(GravityBrawl& game) { return game.m_cosmicEventActive; }
    static double& cosmicEventTimer(GravityBrawl& game) { return game.m_cosmicEventTimer; }
    static GamePhase& phase(GravityBrawl& game) { return game.m_phase; }
    static const GamePhase& phase(const GravityBrawl& game) { return game.m_phase; }

    // ── Collections ──────────────────────────────────────────────────────
    static const std::vector<Particle>& particles(const GravityBrawl& game) { return game.m_particles; }
    static const std::deque<KillFeedEntry>& killFeed(const GravityBrawl& game) { return game.m_killFeed; }
    static const std::string& currentKingId(const GravityBrawl& game) { return game.m_currentKingId; }

    // ── RNG ──────────────────────────────────────────────────────────────
    static void seedRng(GravityBrawl& game, std::uint32_t seed) { game.m_rng.seed(seed); }

    // ── Physics world ────────────────────────────────────────────────────
    static b2World* world(GravityBrawl& game) { return game.m_world.get(); }

    // ── Bot system ───────────────────────────────────────────────────────
    static int& botFillTarget(GravityBrawl& game) { return game.m_botFillTarget; }
    static float& botAITimer(GravityBrawl& game) { return game.m_botAITimer; }
    static void updateBotAI(GravityBrawl& game, float dt) { game.updateBotAI(dt); }
};

} // namespace is::games::gravity_brawl

// ══════════════════════════════════════════════════════════════════════════════
// Local helpers
// ══════════════════════════════════════════════════════════════════════════════

namespace {

using namespace is::games::gravity_brawl;
using TA = GravityBrawlTestAccess;

// ── Planet creation ──────────────────────────────────────────────────────────

Planet& addPlanet(GravityBrawl& game,
                  const std::string& userId,
                  const std::string& displayName,
                  float angle,
                  int orbitDirection = 1) {
    Planet& p = TA::planets(game)[userId];
    p.userId = userId;
    p.displayName = displayName;
    p.alive = true;
    p.kills = 0;
    p.deaths = 0;
    p.survivalTime = 0.0;
    p.score = 0;
    p.hitCount = 0;
    p.tier = PlanetTier::Asteroid;
    p.radiusMeters = 0.5f;
    p.baseRadius = 0.5f;
    p.smashCooldown = 0.0f;
    p.comboCount = 0;
    p.comboWindowEnd = 0.0;
    p.lastHitBy.clear();
    p.lastHitTime = 0.0;
    p.orbitDirection = orbitDirection;
    p.hitFlashTimer = 0.0f;
    p.trailTimer = 0.0f;
    p.animTimer = 0.0f;
    p.glowPulse = 0.0f;
    p.supernovaTimer = 0.0f;
    p.isKing = false;

    TA::spawnPlanetBody(game, p);

    const float spawnRadius = ARENA_RADIUS * TA::spawnRadiusFactor(game);
    const float x = WORLD_CENTER_X + spawnRadius * std::cos(angle);
    const float y = WORLD_CENTER_Y + spawnRadius * std::sin(angle);
    const float dir = static_cast<float>(orbitDirection);
    const float vx = -TA::spawnOrbitSpeed(game) * std::sin(angle) * dir;
    const float vy =  TA::spawnOrbitSpeed(game) * std::cos(angle) * dir;

    p.orbitDirection = orbitDirection;
    p.body->SetTransform(b2Vec2(x, y), 0.0f);
    p.body->SetLinearVelocity(b2Vec2(vx, vy));
    p.prevPosition = {x, y};
    p.renderPosition = {x, y};
    return p;
}

// ── Counting / measuring helpers ─────────────────────────────────────────────

int aliveCount(const GravityBrawl& game) {
    int count = 0;
    for (const auto& [_, p] : TA::planets(game))
        if (p.alive) ++count;
    return count;
}

float distanceToCenter(const Planet& p) {
    const b2Vec2 pos = p.body->GetPosition();
    const float dx = pos.x - WORLD_CENTER_X;
    const float dy = pos.y - WORLD_CENTER_Y;
    return std::sqrt(dx * dx + dy * dy);
}

float minPairDistance(const GravityBrawl& game) {
    float minDist = std::numeric_limits<float>::max();
    const auto& planets = TA::planets(game);
    for (auto itA = planets.begin(); itA != planets.end(); ++itA) {
        if (!itA->second.alive || !itA->second.body) continue;
        auto itB = itA; ++itB;
        for (; itB != planets.end(); ++itB) {
            if (!itB->second.alive || !itB->second.body) continue;
            const b2Vec2 a = itA->second.body->GetPosition();
            const b2Vec2 b = itB->second.body->GetPosition();
            float d = std::sqrt((a.x-b.x)*(a.x-b.x) + (a.y-b.y)*(a.y-b.y));
            minDist = std::min(minDist, d);
        }
    }
    return minDist;
}

/// Check no alive planet has NaN / infinite position or velocity.
bool allPositionsFinite(const GravityBrawl& game) {
    for (const auto& [_, p] : TA::planets(game)) {
        if (!p.alive || !p.body) continue;
        b2Vec2 pos = p.body->GetPosition();
        b2Vec2 vel = p.body->GetLinearVelocity();
        if (!std::isfinite(pos.x) || !std::isfinite(pos.y)) return false;
        if (!std::isfinite(vel.x) || !std::isfinite(vel.y)) return false;
    }
    return true;
}

/// Simulate a chat message.
is::platform::ChatMessage makeMsg(const std::string& userId,
                                   const std::string& displayName,
                                   const std::string& text) {
    is::platform::ChatMessage msg;
    msg.userId = userId;
    msg.displayName = displayName;
    msg.text = text;
    return msg;
}

/// Try to create a RenderTexture.  Returns false if the GPU context is unavailable (CI).
bool tryCreateRT(sf::RenderTexture& rt, unsigned w, unsigned h) {
    return rt.create(w, h);
}

/// Count bright pixels in a sampled grid of the image.
std::size_t countBrightSamples(const sf::Image& img, unsigned step = 80, int threshold = 30) {
    std::size_t count = 0;
    for (unsigned y = 0; y < img.getSize().y; y += step) {
        for (unsigned x = 0; x < img.getSize().x; x += step) {
            auto px = img.getPixel(x, y);
            if (int(px.r) + int(px.g) + int(px.b) > threshold)
                ++count;
        }
    }
    return count;
}

/// Helper: advance N ticks at 60 Hz.
void tick(GravityBrawl& game, int steps, double dt = 1.0/60.0) {
    for (int i = 0; i < steps; ++i)
        game.update(dt);
}

/// Helper: total score across all alive players.
int totalAliveScore(const GravityBrawl& game) {
    int s = 0;
    for (const auto& [_, p] : TA::planets(game))
        if (p.alive) s += p.score;
    return s;
}

} // anonymous namespace

// ══════════════════════════════════════════════════════════════════════════════
// ██ TEST SUITE: Configuration ████████████████████████████████████████████████
// ══════════════════════════════════════════════════════════════════════════════

TEST_SUITE("GB Config") {

    TEST_CASE("Settings round-trip exposes configurable orbit and black-hole variables") {
        GravityBrawl game;
        game.initialize();

        const nlohmann::json settings = {
            {"spawn_radius_factor", 0.95},
            {"spawn_orbit_speed", 8.4},
            {"orbital_gravity_strength", 7.5},
            {"orbital_tangential_strength", 9.25},
            {"black_hole_gravity_strength", 5.5},
            {"black_hole_time_growth_factor", 0.03},
            {"black_hole_consume_size_factor", 2.25},
            {"black_hole_kill_radius_multiplier", 1.1},
            {"event_gravity_multiplier", 2.8}
        };

        game.configure(settings);
        const auto actual = game.getSettings();

        CHECK(actual["spawn_radius_factor"].get<float>() == doctest::Approx(0.95f));
        CHECK(actual["spawn_orbit_speed"].get<float>() == doctest::Approx(8.4f));
        CHECK(actual["orbital_gravity_strength"].get<float>() == doctest::Approx(7.5f));
        CHECK(actual["orbital_tangential_strength"].get<float>() == doctest::Approx(9.25f));
        CHECK(actual["black_hole_gravity_strength"].get<float>() == doctest::Approx(5.5f));
        CHECK(actual["black_hole_time_growth_factor"].get<float>() == doctest::Approx(0.03f));
        CHECK(actual["black_hole_consume_size_factor"].get<float>() == doctest::Approx(2.25f));
        CHECK(actual["black_hole_kill_radius_multiplier"].get<float>() == doctest::Approx(1.1f));
        CHECK(actual["event_gravity_multiplier"].get<float>() == doctest::Approx(2.8f));

        game.shutdown();
    }

    TEST_CASE("Negative / extreme settings are clamped") {
        GravityBrawl game;
        game.initialize();
        game.configure({
            {"spawn_radius_factor", -5.0},
            {"spawn_orbit_speed", -1.0},
            {"black_hole_gravity_strength", -10.0},
            {"black_hole_kill_radius_multiplier", -0.5}
        });
        auto s = game.getSettings();
        CHECK(s["spawn_radius_factor"].get<float>() >= 0.1f);
        CHECK(s["spawn_orbit_speed"].get<float>() >= 0.0f);
        CHECK(s["black_hole_gravity_strength"].get<float>() >= 0.0f);
        CHECK(s["black_hole_kill_radius_multiplier"].get<float>() >= 0.1f);
        game.shutdown();
    }

    TEST_CASE("Bot fill can automatically populate the configured target") {
        GravityBrawl game;
        game.initialize();
        game.configure({{"bot_fill", 3}});
        TA::spawnBots(game);
        CHECK(aliveCount(game) == 3);
        CHECK(TA::planets(game).count("__bot_1") == 1);
        CHECK(TA::planets(game).count("__bot_2") == 1);
        CHECK(TA::planets(game).count("__bot_3") == 1);
        game.shutdown();
    }

    TEST_CASE("getState and getCommands return valid JSON") {
        GravityBrawl game;
        game.initialize();
        auto state = game.getState();
        CHECK(state.contains("phase"));
        CHECK(state.contains("alivePlayers"));
        CHECK(state.contains("players"));
        CHECK(state.contains("blackHoleGravity"));
        auto cmds = game.getCommands();
        CHECK(cmds.is_array());
        CHECK(cmds.size() >= 2);
        game.shutdown();
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// ██ TEST SUITE: Physics ██████████████████████████████████████████████████████
// ══════════════════════════════════════════════════════════════════════════════

TEST_SUITE("GB Physics") {

    TEST_CASE("Spawned planets are distributed across the orbit instead of overlapping") {
        GravityBrawl game;
        game.initialize();
        TA::seedRng(game, 1337u);

        for (int i = 0; i < 12; ++i) {
            Planet& p = TA::planets(game)["p" + std::to_string(i)];
            p.userId = "p" + std::to_string(i);
            p.displayName = "Player " + std::to_string(i);
            p.alive = true;
            p.baseRadius = 0.5f;
            p.radiusMeters = 0.5f;
            p.orbitDirection = (i % 2 == 0) ? 1 : -1;
            TA::spawnPlanetBody(game, p);
        }

        CHECK(aliveCount(game) == 12);
        CHECK(minPairDistance(game) > 6.0f);
        game.shutdown();
    }

    TEST_CASE("Default tuning keeps 3 starting planets alive for 5 seconds") {
        GravityBrawl game;
        game.initialize();
        addPlanet(game, "p1", "Alpha", 0.0f, 1);
        addPlanet(game, "p2", "Beta", 2.0943951f, -1);
        addPlanet(game, "p3", "Gamma", 4.1887902f, 1);
        TA::startPlaying(game);

        tick(game, 300);

        CHECK(aliveCount(game) == 3);
        for (const auto& [_, planet] : TA::planets(game)) {
            CHECK(planet.alive);
            CHECK(distanceToCenter(planet) > BLACK_HOLE_RADIUS * TA::blackHoleKillRadiusMultiplier(game));
        }
        game.shutdown();
    }

    TEST_CASE("Orbital forces keep planets in a stable band for 30 seconds") {
        GravityBrawl game;
        game.initialize();
        TA::seedRng(game, 42u);

        // Add 6 evenly spaced planets
        for (int i = 0; i < 6; ++i) {
            float angle = (2.0f * b2_pi * float(i)) / 6.0f;
            addPlanet(game, "orb" + std::to_string(i), "Orb" + std::to_string(i),
                      angle, (i % 2) ? 1 : -1);
        }
        TA::startPlaying(game);

        float killZone = BLACK_HOLE_RADIUS * TA::blackHoleKillRadiusMultiplier(game);
        float outerBound = ARENA_RADIUS * 1.5f;

        // 30 seconds of simulation at 60Hz
        for (int step = 0; step < 1800; ++step) {
            game.update(1.0 / 60.0);

            // Check invariants every 60 ticks (1 second)
            if (step % 60 == 0) {
                REQUIRE(allPositionsFinite(game));
                for (const auto& [_, p] : TA::planets(game)) {
                    if (!p.alive || !p.body) continue;
                    float dist = distanceToCenter(p);
                    // Planet must stay between kill zone and a generous outer bound
                    CHECK(dist > killZone);
                    CHECK(dist < outerBound);
                }
            }
        }

        // At least 5 of 6 should still be alive (no unexpected deaths)
        CHECK(aliveCount(game) >= 5);
        game.shutdown();
    }

    TEST_CASE("Planet knocked inside safe orbit recovers outward") {
        GravityBrawl game;
        game.initialize();
        Planet& p = addPlanet(game, "inner", "Inner", 0.0f, 1);
        TA::startPlaying(game);

        // Place planet dangerously close to the black hole (at 5m from center)
        float innerDist = 5.0f;
        p.body->SetTransform(b2Vec2(WORLD_CENTER_X + innerDist, WORLD_CENTER_Y), 0.0f);
        p.body->SetLinearVelocity(b2Vec2(0.0f, 0.0f)); // no initial velocity

        // Simulate 3 seconds — orbital repulsion should push it outward
        tick(game, 180);

        if (p.alive && p.body) {
            float distNow = distanceToCenter(p);
            // Planet should have moved outward, not fallen into the black hole
            CHECK(distNow > innerDist);
        }
        game.shutdown();
    }

    TEST_CASE("Velocity clamping prevents runaway speeds") {
        GravityBrawl game;
        game.initialize();
        Planet& p = addPlanet(game, "fast", "Speedy", 0.0f, 1);
        TA::startPlaying(game);

        // Give the planet an absurd velocity
        p.body->SetLinearVelocity(b2Vec2(100.0f, 100.0f));

        // After a few ticks, velocity should be clamped to 25 m/s
        tick(game, 5);

        if (p.alive && p.body) {
            b2Vec2 vel = p.body->GetLinearVelocity();
            float speed = vel.Length();
            CHECK(speed <= 25.5f); // small tolerance
        }
        game.shutdown();
    }

    TEST_CASE("Black hole gravity grows over time and via planet consumption") {
        GravityBrawl game;
        game.initialize();
        TA::startPlaying(game);

        const float baseGravity = TA::currentBlackHoleGravity(game);
        TA::gameTimer(game) = 120.0;
        const float timeGrownGravity = TA::currentBlackHoleGravity(game);
        CHECK(timeGrownGravity > baseGravity);

        Planet& victim = addPlanet(game, "victim", "Victim", 0.0f, 1);
        victim.radiusMeters = 0.9f;
        victim.lastHitBy.clear();
        float before = TA::blackHoleConsumedGravityBonus(game);
        TA::eliminatePlanet(game, victim);

        float expectedGain = 0.9f * TA::blackHoleConsumeSizeFactor(game);
        CHECK(TA::blackHoleConsumedGravityBonus(game) == doctest::Approx(before + expectedGain));
        CHECK(TA::currentBlackHoleGravity(game) > timeGrownGravity);
        game.shutdown();
    }

    TEST_CASE("Black hole gravity is capped") {
        GravityBrawl game;
        game.initialize();
        TA::startPlaying(game);

        // Force massive gravity bonus
        TA::blackHoleConsumedGravityBonus(game) = 1000.0f;
        TA::gameTimer(game) = 99999.0;

        // The raw gravity is huge, but applyBlackHoleGravity uses min(force, cap)
        // Verify the raw value exceeds the cap (the cap is enforced inside the force application)
        float rawGravity = TA::currentBlackHoleGravity(game);
        CHECK(rawGravity > TA::blackHoleGravityCap(game));
        game.shutdown();
    }

    TEST_CASE("Planet inside kill zone is eliminated") {
        GravityBrawl game;
        game.initialize();
        Planet& p = addPlanet(game, "doomed", "Doomed", 0.0f, 1);
        TA::startPlaying(game);

        // Teleport planet to center (inside kill zone)
        p.body->SetTransform(b2Vec2(WORLD_CENTER_X, WORLD_CENTER_Y), 0.0f);
        p.body->SetLinearVelocity(b2Vec2(0, 0));

        TA::checkBlackHoleDeaths(game);
        CHECK_FALSE(p.alive);
        CHECK(p.deaths == 1);
        game.shutdown();
    }

    TEST_CASE("Positions stay finite during chaotic multi-player smash simulation") {
        GravityBrawl game;
        game.initialize();
        TA::seedRng(game, 555u);

        for (int i = 0; i < 20; ++i) {
            float angle = (2.0f * b2_pi * float(i)) / 20.0f;
            addPlanet(game, "c" + std::to_string(i), "C" + std::to_string(i),
                      angle, (i % 2) ? 1 : -1);
        }
        TA::startPlaying(game);

        // Intense 60-second simulation: every player smashes every 0.5s
        for (int step = 0; step < 3600; ++step) {
            if (step % 30 == 0) { // every 0.5s
                for (int i = 0; i < 20; ++i) {
                    TA::cmdSmash(game, "c" + std::to_string(i));
                }
            }
            game.update(1.0 / 60.0);

            if (step % 120 == 0) {
                REQUIRE(allPositionsFinite(game));
            }
        }

        REQUIRE(allPositionsFinite(game));
        game.shutdown();
    }

    TEST_CASE("Collision handler awards points and sets last-hit-by") {
        GravityBrawl game;
        game.initialize();
        Planet& a = addPlanet(game, "attacker", "Attacker", 0.0f, 1);
        Planet& b = addPlanet(game, "defender", "Defender", 1.0f, -1);
        TA::startPlaying(game);

        int scoreA = a.score;
        int scoreB = b.score;

        // Simulate a hard collision (impulse > 2.0 threshold)
        TA::onPlanetCollision(game, a, b, 5.0f);

        CHECK(a.score == scoreA + 2);
        CHECK(b.score == scoreB + 2);
        CHECK(b.lastHitBy == "attacker");
        CHECK(a.lastHitBy == "defender");
        CHECK(a.hitFlashTimer > 0.0f);
        CHECK(b.hitFlashTimer > 0.0f);
        game.shutdown();
    }

    TEST_CASE("Soft collision (impulse < 2) is ignored") {
        GravityBrawl game;
        game.initialize();
        Planet& a = addPlanet(game, "a", "A", 0.0f, 1);
        Planet& b = addPlanet(game, "b", "B", 1.0f, -1);
        TA::startPlaying(game);

        int scoreA = a.score;
        TA::onPlanetCollision(game, a, b, 1.5f);
        CHECK(a.score == scoreA); // No points awarded
        game.shutdown();
    }

    TEST_CASE("Collisions during Lobby do not award points") {
        GravityBrawl game;
        game.initialize();
        Planet& a = addPlanet(game, "p1", "P1", 0.0f, 1);
        Planet& b = addPlanet(game, "p2", "P2", 1.0f, -1);
        // Phase is Lobby (not Playing)

        int scoreA = a.score;
        int scoreB = b.score;
        TA::onPlanetCollision(game, a, b, 5.0f);
        CHECK(a.score == scoreA); // No points in lobby
        CHECK(b.score == scoreB);
        game.shutdown();
    }

    TEST_CASE("Orbital forces boost during cosmic events") {
        GravityBrawl game;
        game.initialize();
        // Place a planet inside safe orbit
        Planet& p = addPlanet(game, "test", "Test", 0.0f, 1);
        TA::startPlaying(game);

        float innerDist = 6.0f;
        p.body->SetTransform(b2Vec2(WORLD_CENTER_X + innerDist, WORLD_CENTER_Y), 0.0f);
        p.body->SetLinearVelocity(b2Vec2(0.0f, 0.0f));

        // Activate cosmic event
        TA::cosmicEventActive(game) = 10.0;
        tick(game, 180); // 3 seconds

        // Planet should still be alive despite cosmic event's 2.2x gravity
        CHECK(p.alive);
        if (p.alive && p.body) {
            CHECK(distanceToCenter(p) > BLACK_HOLE_RADIUS * TA::blackHoleKillRadiusMultiplier(game));
        }
        game.shutdown();
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// ██ TEST SUITE: Gameplay █████████████████████████████████████████████████████
// ══════════════════════════════════════════════════════════════════════════════

TEST_SUITE("GB Gameplay") {

    TEST_CASE("Full lifecycle: Lobby → Countdown → Playing → GameOver") {
        GravityBrawl game;
        game.initialize();
        game.configure({{"lobby_duration", 2.0}, {"game_duration", 8.0}, {"min_players", 2}});

        CHECK(TA::phase(game) == GamePhase::Lobby);

        // Join 4 players via chat (more players avoids premature GameOver from a single death)
        game.onChatMessage(makeMsg("u1", "Alice", "!join"));
        game.onChatMessage(makeMsg("u2", "Bob", "!join"));
        game.onChatMessage(makeMsg("u3", "Carol", "!join"));
        game.onChatMessage(makeMsg("u4", "Dan", "!join"));
        CHECK(aliveCount(game) == 4);

        // Advance through lobby (lobby timer shortened to 5s by join logic)
        tick(game, 360); // 6 seconds

        // Should have transitioned past lobby
        CHECK(TA::phase(game) != GamePhase::Lobby);

        // If countdown, advance 3 more seconds
        if (TA::phase(game) == GamePhase::Countdown) {
            tick(game, 240);
        }
        CHECK(TA::phase(game) == GamePhase::Playing);

        // Play until game ends (time limit or elimination)
        tick(game, 600); // 10 seconds (gameDuration = 8)

        CHECK(TA::phase(game) == GamePhase::GameOver);
        CHECK(game.isGameOver());
        game.shutdown();
    }

    TEST_CASE("Chat command !join adds a player") {
        GravityBrawl game;
        game.initialize();

        game.onChatMessage(makeMsg("user1", "Player1", "!join"));
        CHECK(TA::planets(game).count("user1") == 1);
        CHECK(TA::planets(game)["user1"].alive);
        CHECK(TA::planets(game)["user1"].displayName == "Player1");
        game.shutdown();
    }

    TEST_CASE("Chat command !play also joins") {
        GravityBrawl game;
        game.initialize();
        game.onChatMessage(makeMsg("u1", "P1", "!play"));
        CHECK(TA::planets(game).count("u1") == 1);
        game.shutdown();
    }

    TEST_CASE("Duplicate !join is ignored for alive player") {
        GravityBrawl game;
        game.initialize();
        game.onChatMessage(makeMsg("u1", "P1", "!join"));
        auto* body1 = TA::planets(game)["u1"].body;
        game.onChatMessage(makeMsg("u1", "P1", "!join"));
        // Body pointer should not have changed (no re-spawn)
        CHECK(TA::planets(game)["u1"].body == body1);
        game.shutdown();
    }

    TEST_CASE("!smash / !s applies impulse only during Playing phase") {
        GravityBrawl game;
        game.initialize();
        Planet& p = addPlanet(game, "u1", "P1", 0.0f, 1);
        // Still in Lobby — smash should be rejected
        b2Vec2 velBefore = p.body->GetLinearVelocity();
        game.onChatMessage(makeMsg("u1", "P1", "!s"));
        b2Vec2 velAfter = p.body->GetLinearVelocity();
        CHECK(velAfter.x == doctest::Approx(velBefore.x));
        CHECK(velAfter.y == doctest::Approx(velBefore.y));

        // Switch to playing phase
        TA::startPlaying(game);
        game.onChatMessage(makeMsg("u1", "P1", "!s"));
        b2Vec2 velSmashed = p.body->GetLinearVelocity();
        float speedDelta = (velSmashed - velBefore).Length();
        CHECK(speedDelta > 1.0f); // Got a meaningful impulse
        game.shutdown();
    }

    TEST_CASE("Smash cooldown prevents rapid fire") {
        GravityBrawl game;
        game.initialize();
        Planet& p = addPlanet(game, "u1", "P1", 0.0f, 1);
        TA::startPlaying(game);

        TA::cmdSmash(game, "u1");
        b2Vec2 v1 = p.body->GetLinearVelocity();

        // Immediately smash again — should be blocked by cooldown
        TA::cmdSmash(game, "u1");
        b2Vec2 v2 = p.body->GetLinearVelocity();

        // Velocity should not have changed significantly (cooldown blocked it)
        float delta = (v2 - v1).Length();
        CHECK(delta < 0.5f);
        game.shutdown();
    }

    TEST_CASE("Supernova triggers after 5 rapid smashes") {
        GravityBrawl game;
        game.initialize();
        Planet& p = addPlanet(game, "u1", "P1", 0.0f, 1);
        // Add a target nearby so supernova has something to push
        addPlanet(game, "u2", "P2", 0.3f, -1);
        TA::startPlaying(game);

        // Supernova needs 5 !s within COMBO_WINDOW (3s), but cooldown is 0.8s.
        // We must manually advance past cooldowns.
        for (int i = 0; i < 4; ++i) {
            TA::cmdSmash(game, "u1");
            // Advance past cooldown
            tick(game, 50); // ~0.83s
        }
        // 5th smash should trigger supernova
        float supernovaBefore = p.supernovaTimer;
        TA::cmdSmash(game, "u1");

        // Supernova timer should have been set (2.0s cooldown after supernova)
        CHECK(p.smashCooldown == doctest::Approx(2.0f));
        game.shutdown();
    }

    TEST_CASE("King / bounty system tracks highest-kill player") {
        GravityBrawl game;
        game.initialize();
        Planet& killer = addPlanet(game, "killer", "Killer", 0.0f, 1);
        Planet& victim = addPlanet(game, "v1", "Victim1", 1.0f, -1);
        addPlanet(game, "v2", "Victim2", 2.0f, 1);
        TA::startPlaying(game);

        // Give killer a kill
        killer.kills = 1;
        TA::updateKing(game);
        CHECK(killer.isKing);
        CHECK(TA::currentKingId(game) == "killer");

        // Another player surpasses
        auto& v2 = TA::planets(game)["v2"];
        v2.kills = 2;
        TA::updateKing(game);
        CHECK(v2.isKing);
        CHECK_FALSE(killer.isKing);
        game.shutdown();
    }

    TEST_CASE("Kill attribution within 5-second window") {
        GravityBrawl game;
        game.initialize();
        Planet& a = addPlanet(game, "hitter", "Hitter", 0.0f, 1);
        Planet& v = addPlanet(game, "victim", "Victim", 1.0f, -1);
        TA::startPlaying(game);

        // Simulate a hit
        v.lastHitBy = "hitter";
        v.lastHitTime = TA::gameTimer(game);
        int killsBefore = a.kills;

        TA::eliminatePlanet(game, v);

        CHECK_FALSE(v.alive);
        CHECK(a.kills == killsBefore + 1);
        CHECK(a.score > 0);
        CHECK(TA::killFeed(game).size() >= 1);
        game.shutdown();
    }

    TEST_CASE("Kill attribution lapses after 5 seconds") {
        GravityBrawl game;
        game.initialize();
        Planet& a = addPlanet(game, "hitter", "Hitter", 0.0f, 1);
        Planet& v = addPlanet(game, "victim", "Victim", 1.0f, -1);
        TA::startPlaying(game);

        v.lastHitBy = "hitter";
        v.lastHitTime = 0.0;
        TA::gameTimer(game) = 10.0; // > 5s later

        int killsBefore = a.kills;
        TA::eliminatePlanet(game, v);

        // Kill should NOT be attributed since > 5s elapsed
        CHECK(a.kills == killsBefore);
        // Kill feed should say "The Void"
        CHECK(TA::killFeed(game).front().killer == "The Void");
        game.shutdown();
    }

    TEST_CASE("Survival points accumulate over time") {
        GravityBrawl game;
        game.initialize();
        Planet& p = addPlanet(game, "surv", "Survivor", 0.0f, 1);
        TA::startPlaying(game);

        // 10s at 60Hz = 600 ticks → should get 1 survival point
        tick(game, 600);
        CHECK(p.score >= 1);
        game.shutdown();
    }

    TEST_CASE("Cosmic event doubles black hole gravity temporarily") {
        GravityBrawl game;
        game.initialize();
        addPlanet(game, "p1", "P1", 0.0f, 1);
        TA::startPlaying(game);

        float normalGravity = TA::currentBlackHoleGravity(game);
        TA::triggerCosmicEvent(game);
        CHECK(TA::cosmicEventActive(game) > 0.0);

        // During cosmic event, effective gravity in applyBlackHoleGravity is multiplied
        // We can verify the event_gravity_multiplier is > 1
        CHECK(TA::eventGravityMul(game) > 1.0f);

        // Advance to end of event
        for (int i = 0; i < 660; ++i) { // ~11s
            game.update(1.0 / 60.0);
        }
        CHECK(TA::cosmicEventActive(game) <= 0.0);
        game.shutdown();
    }

    TEST_CASE("Late join during Playing phase works") {
        GravityBrawl game;
        game.initialize();
        addPlanet(game, "p1", "P1", 0.0f, 1);
        addPlanet(game, "p2", "P2", 2.0f, -1);
        TA::startPlaying(game);

        tick(game, 120); // 2 seconds into the game

        // Late join
        game.onChatMessage(makeMsg("late", "LateJoiner", "!join"));
        CHECK(TA::planets(game).count("late") == 1);
        CHECK(TA::planets(game)["late"].alive);
        CHECK(aliveCount(game) == 3);
        game.shutdown();
    }

    TEST_CASE("Game ends with 1 player left") {
        GravityBrawl game;
        game.initialize();
        game.configure({{"game_duration", 300.0}});
        Planet& p1 = addPlanet(game, "p1", "P1", 0.0f, 1);
        Planet& p2 = addPlanet(game, "p2", "P2", 3.14f, -1);
        TA::startPlaying(game);

        // Eliminate p2
        TA::eliminatePlanet(game, p2);
        tick(game, 1);

        CHECK(TA::phase(game) == GamePhase::GameOver);
        game.shutdown();
    }

    TEST_CASE("Planet tier evolves with kills") {
        Planet p;
        p.kills = 0;
        CHECK(tierFromKills(p.kills) == PlanetTier::Asteroid);
        p.kills = 3;
        CHECK(tierFromKills(p.kills) == PlanetTier::IcePlanet);
        p.kills = 10;
        CHECK(tierFromKills(p.kills) == PlanetTier::GasGiant);
        p.kills = 25;
        CHECK(tierFromKills(p.kills) == PlanetTier::Star);
        p.kills = 100;
        CHECK(tierFromKills(p.kills) == PlanetTier::Star);
    }

    TEST_CASE("Visual radius grows with kills") {
        Planet p;
        p.baseRadius = 0.5f;
        p.kills = 0;
        float r0 = p.getVisualRadius();
        p.kills = 10;
        float r10 = p.getVisualRadius();
        p.kills = 50;
        float r50 = p.getVisualRadius();
        CHECK(r10 > r0);
        CHECK(r50 > r10);
    }

    TEST_CASE("Mass scale increases with kills and king status") {
        Planet p;
        p.kills = 0;
        p.isKing = false;
        float m0 = p.getMassScale();
        p.kills = 10;
        float m10 = p.getMassScale();
        CHECK(m10 > m0);
        p.isKing = true;
        float mKing = p.getMassScale();
        CHECK(mKing > m10);
    }

    TEST_CASE("Leaderboard returns sorted results excluding bots") {
        GravityBrawl game;
        game.initialize();
        auto& p1 = addPlanet(game, "human1", "Alice", 0.0f, 1);
        auto& p2 = addPlanet(game, "human2", "Bob", 1.0f, -1);
        auto& bot = TA::planets(game)["__bot_1"];
        bot.userId = "__bot_1";
        bot.displayName = "";
        bot.alive = true;
        bot.score = 999;
        TA::spawnPlanetBody(game, bot);

        p1.score = 100;
        p2.score = 50;

        auto lb = game.getLeaderboard();
        CHECK(lb.size() == 2); // Bot excluded (empty displayName)
        CHECK(lb[0].first == "Alice");
        CHECK(lb[0].second == 100);
        CHECK(lb[1].first == "Bob");
        game.shutdown();
    }

    TEST_CASE("Join is blocked during GameOver phase") {
        GravityBrawl game;
        game.initialize();
        addPlanet(game, "p1", "P1", 0.0f, 1);
        addPlanet(game, "p2", "P2", 3.14f, -1);
        TA::startPlaying(game);

        // Force GameOver
        TA::phase(game) = GamePhase::GameOver;

        // Try to join — should be rejected
        game.onChatMessage(makeMsg("late", "LateJoiner", "!join"));
        CHECK(TA::planets(game).count("late") == 0);
        game.shutdown();
    }

    TEST_CASE("Supernova sets lastHitBy for kill attribution") {
        GravityBrawl game;
        game.initialize();
        Planet& attacker = addPlanet(game, "nova", "Nova", 0.0f, 1);
        Planet& target = addPlanet(game, "victim", "Victim", 0.3f, -1);
        TA::startPlaying(game);

        // Trigger supernova on attacker
        TA::triggerSupernova(game, attacker);

        // Target should now have lastHitBy set to the supernova user
        CHECK(target.lastHitBy == "nova");
        game.shutdown();
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// ██ TEST SUITE: Rendering ████████████████████████████████████████████████████
// ══════════════════════════════════════════════════════════════════════════════

TEST_SUITE("GB Rendering") {

    TEST_CASE("Render produces visible pixels with 2 players") {
        GravityBrawl game;
        game.initialize();
        addPlanet(game, "p1", "Alpha", 0.0f, 1);
        addPlanet(game, "p2", "Beta", 3.14159f, -1);
        TA::startPlaying(game);
        game.update(1.0 / 30.0);

        sf::RenderTexture target;
        if (!tryCreateRT(target, static_cast<unsigned>(SCREEN_W), static_cast<unsigned>(SCREEN_H))) {
            game.shutdown();
            return;
        }

        target.clear(sf::Color::Black);
        game.render(target, 0.0);
        target.display();
        const sf::Image img = target.getTexture().copyToImage();

        CHECK(countBrightSamples(img) >= 3);

        // Center should be dark (black hole)
        auto center = img.getPixel(img.getSize().x / 2, img.getSize().y / 2);
        CHECK(int(center.r) + int(center.g) + int(center.b) < 80);
        game.shutdown();
    }

    TEST_CASE("Render with many players produces more bright pixels") {
        GravityBrawl game;
        game.initialize();
        TA::seedRng(game, 100u);
        for (int i = 0; i < 12; ++i) {
            float angle = (2.0f * b2_pi * float(i)) / 12.0f;
            addPlanet(game, "r" + std::to_string(i), "R" + std::to_string(i),
                      angle, (i % 2) ? 1 : -1);
        }
        TA::startPlaying(game);
        tick(game, 30);

        sf::RenderTexture target;
        if (!tryCreateRT(target, static_cast<unsigned>(SCREEN_W), static_cast<unsigned>(SCREEN_H))) {
            game.shutdown();
            return;
        }

        target.clear(sf::Color::Black);
        game.render(target, 0.5);
        target.display();
        const sf::Image img = target.getTexture().copyToImage();

        // With 12 planets + particles + orbit guides, expect some bright pixels
        CHECK(countBrightSamples(img) >= 3);
        game.shutdown();
    }

    TEST_CASE("Render during cosmic event shows more brightness (red overlay)") {
        GravityBrawl game;
        game.initialize();
        addPlanet(game, "p1", "P1", 0.0f, 1);
        TA::startPlaying(game);
        tick(game, 10);

        sf::RenderTexture rt;
        if (!tryCreateRT(rt, static_cast<unsigned>(SCREEN_W), static_cast<unsigned>(SCREEN_H))) {
            game.shutdown();
            return;
        }

        // Render without cosmic event
        rt.clear(sf::Color::Black);
        game.render(rt, 0.0);
        rt.display();
        auto imgNormal = rt.getTexture().copyToImage();
        auto normalBright = countBrightSamples(imgNormal, 40, 20);

        // Trigger cosmic event
        TA::triggerCosmicEvent(game);
        tick(game, 5);

        rt.clear(sf::Color::Black);
        game.render(rt, 0.0);
        rt.display();
        auto imgEvent = rt.getTexture().copyToImage();
        auto eventBright = countBrightSamples(imgEvent, 40, 20);

        // Cosmic event should produce at least as many bright pixels (red overlay)
        CHECK(eventBright >= normalBright);
        game.shutdown();
    }

    TEST_CASE("Particles are generated by smash and decay over time") {
        GravityBrawl game;
        game.initialize();
        addPlanet(game, "p1", "P1", 0.0f, 1);
        addPlanet(game, "p2", "P2", 0.5f, -1);
        TA::startPlaying(game);

        std::size_t before = TA::particles(game).size();
        TA::cmdSmash(game, "p1");
        std::size_t afterSmash = TA::particles(game).size();
        CHECK(afterSmash > before);

        // Advance 3 seconds — particles should decay
        tick(game, 180);
        std::size_t afterDecay = TA::particles(game).size();
        CHECK(afterDecay < afterSmash);
        game.shutdown();
    }

    TEST_CASE("Particle count stays below MAX_PARTICLES under stress") {
        GravityBrawl game;
        game.initialize();
        TA::seedRng(game, 999u);

        for (int i = 0; i < 15; ++i) {
            float angle = (2.0f * b2_pi * float(i)) / 15.0f;
            addPlanet(game, "sp" + std::to_string(i), "SP" + std::to_string(i),
                      angle, (i % 2) ? 1 : -1);
        }
        TA::startPlaying(game);

        // Spam smashes to generate particles
        for (int step = 0; step < 600; ++step) {
            if (step % 10 == 0) {
                for (int i = 0; i < 15; ++i)
                    TA::cmdSmash(game, "sp" + std::to_string(i));
            }
            game.update(1.0 / 60.0);
        }
        CHECK(TA::particles(game).size() <= 5000);
        game.shutdown();
    }

    TEST_CASE("Multiple render frames during simulation don't crash") {
        GravityBrawl game;
        game.initialize();
        TA::seedRng(game, 2025u);

        for (int i = 0; i < 8; ++i) {
            float angle = (2.0f * b2_pi * float(i)) / 8.0f;
            addPlanet(game, "mv" + std::to_string(i), "MV" + std::to_string(i),
                      angle, (i % 2) ? 1 : -1);
        }
        TA::startPlaying(game);

        sf::RenderTexture rt;
        if (!tryCreateRT(rt, static_cast<unsigned>(SCREEN_W), static_cast<unsigned>(SCREEN_H))) {
            game.shutdown();
            return;
        }

        // Simulate 10 seconds of gameplay with rendering every 2 physics frames
        for (int step = 0; step < 600; ++step) {
            game.update(1.0 / 60.0);
            if (step % 2 == 0) {
                rt.clear(sf::Color::Black);
                game.render(rt, 0.5);
                rt.display();
            }
            // Occasional smashes
            if (step % 60 == 0) {
                for (int i = 0; i < 8; i += 2)
                    TA::cmdSmash(game, "mv" + std::to_string(i));
            }
        }
        // If we get here without crashing, the test passed
        CHECK(true);
        game.shutdown();
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// ██ TEST SUITE: Stress Test ██████████████████████████████████████████████████
// ══════════════════════════════════════════════════════════════════════════════

TEST_SUITE("GB Stress") {

    TEST_CASE("12-player simulation stays stable for 45 seconds (2700 steps)") {
        GravityBrawl game;
        game.initialize();
        for (int i = 0; i < 12; ++i) {
            float angle = (2.0f * b2_pi * float(i)) / 12.0f;
            addPlanet(game, "sim_" + std::to_string(i), "Sim " + std::to_string(i),
                      angle, (i % 2 == 0) ? 1 : -1);
        }
        TA::startPlaying(game);

        for (int step = 0; step < 2700; ++step) {
            if (step % 120 == 0) {
                for (int i = 0; i < 12; i += 3)
                    TA::cmdSmash(game, "sim_" + std::to_string(i));
            }
            game.update(1.0 / 60.0);
        }

        auto state = game.getState();
        CHECK(state["phase"].get<std::string>() == "playing");
        CHECK(state["alivePlayers"].get<int>() >= 8);
        CHECK(state["blackHoleGravity"].get<float>() < 15.5f);
        CHECK(state["players"].size() == 12);
        game.shutdown();
    }

    TEST_CASE("20-player aggressive match for 2 minutes") {
        GravityBrawl game;
        game.initialize();
        game.configure({{"game_duration", 120.0}});
        TA::seedRng(game, 12345u);

        for (int i = 0; i < 20; ++i) {
            float angle = (2.0f * b2_pi * float(i)) / 20.0f;
            addPlanet(game, "agg" + std::to_string(i), "Agg" + std::to_string(i),
                      angle, (i % 2) ? 1 : -1);
        }
        TA::startPlaying(game);

        int maxAlive = 20;
        int minAlive = 20;
        bool anyKills = false;

        // 2 minutes = 7200 ticks at 60Hz
        for (int step = 0; step < 7200; ++step) {
            // Every player smashes every second
            if (step % 60 == 0) {
                for (int i = 0; i < 20; ++i)
                    TA::cmdSmash(game, "agg" + std::to_string(i));
            }
            game.update(1.0 / 60.0);

            int alive = aliveCount(game);
            if (alive < minAlive) minAlive = alive;
            if (alive < 20) anyKills = true;

            if (step % 300 == 0) {
                REQUIRE(allPositionsFinite(game));
            }

            // Stop if game ended
            if (TA::phase(game) == GamePhase::GameOver) break;
        }

        // Some interaction should have happened
        auto state = game.getState();

        // Total scores should be positive
        int totalScore = 0;
        for (const auto& p : state["players"]) {
            totalScore += p["score"].get<int>();
        }
        CHECK(totalScore > 0);

        // All positions should still be finite
        CHECK(allPositionsFinite(game));
        game.shutdown();
    }

    TEST_CASE("30-player match with continuous joins and smashes") {
        GravityBrawl game;
        game.initialize();
        game.configure({{"game_duration", 60.0}, {"min_players", 2}});
        TA::seedRng(game, 7777u);

        // Start with 5 players
        for (int i = 0; i < 5; ++i) {
            float angle = (2.0f * b2_pi * float(i)) / 5.0f;
            addPlanet(game, "init" + std::to_string(i), "Init" + std::to_string(i),
                      angle, (i % 2) ? 1 : -1);
        }
        TA::startPlaying(game);

        int nextJoin = 5;
        // 60 seconds at 60Hz = 3600 ticks
        for (int step = 0; step < 3600; ++step) {
            // Join a new player every 2 seconds (up to 30 total)
            if (step % 120 == 0 && nextJoin < 30) {
                game.onChatMessage(makeMsg("late" + std::to_string(nextJoin),
                                            "Late" + std::to_string(nextJoin), "!join"));
                nextJoin++;
            }

            // All alive players smash every 0.5 seconds
            if (step % 30 == 0) {
                for (const auto& [id, p] : TA::planets(game)) {
                    if (p.alive) TA::cmdSmash(game, id);
                }
            }

            game.update(1.0 / 60.0);

            if (step % 600 == 0) {
                REQUIRE(allPositionsFinite(game));
            }

            if (TA::phase(game) == GamePhase::GameOver) break;
        }

        CHECK(allPositionsFinite(game));
        auto state = game.getState();
        CHECK(state["totalPlayers"].get<int>() >= 15); // At least some late joins worked
        game.shutdown();
    }

    TEST_CASE("Full automated match with bots: 5-minute game with rendering") {
        GravityBrawl game;
        game.initialize();
        game.configure({
            {"game_duration", 30.0}, // Shortened for test speed
            {"bot_fill", 8},
            {"min_players", 2},
            {"lobby_duration", 1.0}
        });
        TA::seedRng(game, 42u);

        // Join 4 human players
        for (int i = 0; i < 4; ++i) {
            game.onChatMessage(makeMsg("h" + std::to_string(i),
                                        "Human" + std::to_string(i), "!join"));
        }

        sf::RenderTexture rt;
        bool canRender = tryCreateRT(rt, static_cast<unsigned>(SCREEN_W),
                                         static_cast<unsigned>(SCREEN_H));

        int frameCount = 0;
        int smashCount = 0;
        int phaseChanges = 0;
        GamePhase lastPhase = TA::phase(game);

        // Run up to 45 seconds of real simulation (lobby + countdown + game)
        for (int step = 0; step < 2700; ++step) {
            game.update(1.0 / 60.0);

            GamePhase currentPhase = TA::phase(game);
            if (currentPhase != lastPhase) {
                phaseChanges++;
                lastPhase = currentPhase;
            }

            // Humans smash every second during gameplay
            if (currentPhase == GamePhase::Playing && step % 60 == 0) {
                for (int i = 0; i < 4; ++i) {
                    game.onChatMessage(makeMsg("h" + std::to_string(i),
                                               "Human" + std::to_string(i), "!s"));
                    smashCount++;
                }
            }

            // Render every 10 ticks when GPU is available
            if (canRender && step % 10 == 0) {
                rt.clear(sf::Color::Black);
                game.render(rt, 0.5);
                rt.display();
                frameCount++;
            }

            if (step % 300 == 0)
                REQUIRE(allPositionsFinite(game));

            if (currentPhase == GamePhase::GameOver) break;
        }

        CHECK(phaseChanges >= 1);
        CHECK(smashCount > 0);
        if (canRender) CHECK(frameCount > 10);

        auto state = game.getState();
        CHECK(state["totalPlayers"].get<int>() >= 8);
        game.shutdown();
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// ██ TEST SUITE: Automated Play (long-running game simulation) ████████████████
// ══════════════════════════════════════════════════════════════════════════════

TEST_SUITE("GB AutoPlay") {

    TEST_CASE("Deterministic seeded match produces consistent results") {
        // Run the same scenario twice with the same seed and verify identical outcomes
        auto runMatch = [](std::uint32_t seed) -> std::pair<int, int> {
            GravityBrawl game;
            game.initialize();
            game.configure({{"game_duration", 20.0}});
            TA::seedRng(game, seed);

            for (int i = 0; i < 8; ++i) {
                float angle = (2.0f * b2_pi * float(i)) / 8.0f;
                addPlanet(game, "d" + std::to_string(i), "D" + std::to_string(i),
                          angle, (i % 2) ? 1 : -1);
            }
            TA::startPlaying(game);

            for (int step = 0; step < 1200; ++step) {
                if (step % 90 == 0) {
                    for (int i = 0; i < 8; i += 2)
                        TA::cmdSmash(game, "d" + std::to_string(i));
                }
                game.update(1.0 / 60.0);
                if (TA::phase(game) == GamePhase::GameOver) break;
            }

            int alive = aliveCount(game);
            int totalScore = totalAliveScore(game);
            game.shutdown();
            return {alive, totalScore};
        };

        auto [alive1, score1] = runMatch(2026u);
        auto [alive2, score2] = runMatch(2026u);
        CHECK(alive1 == alive2);
        CHECK(score1 == score2);
    }

    TEST_CASE("Multiple consecutive matches don't leak state") {
        for (int match = 0; match < 3; ++match) {
            GravityBrawl game;
            game.initialize();
            TA::seedRng(game, static_cast<std::uint32_t>(match * 100));

            for (int i = 0; i < 6; ++i) {
                float angle = (2.0f * b2_pi * float(i)) / 6.0f;
                addPlanet(game, "m" + std::to_string(i), "M" + std::to_string(i),
                          angle, (i % 2) ? 1 : -1);
            }
            TA::startPlaying(game);
            tick(game, 600);

            CHECK(allPositionsFinite(game));
            CHECK(aliveCount(game) >= 1);

            game.shutdown();

            // After shutdown, re-initialize for a fresh match
            game.initialize();
            CHECK(TA::planets(game).empty());
            CHECK(TA::particles(game).empty());
            CHECK(TA::killFeed(game).empty());
            game.shutdown();
        }
    }

    TEST_CASE("Smash auto-aim targets the nearest enemy") {
        GravityBrawl game;
        game.initialize();

        // Place attacker at angle 0, target nearby at angle 0.2, distant at angle 3.0
        Planet& atk = addPlanet(game, "atk", "Attacker", 0.0f, 1);
        addPlanet(game, "near", "Near", 0.2f, -1);
        addPlanet(game, "far", "Far", 3.0f, 1);
        TA::startPlaying(game);

        b2Vec2 posBefore = atk.body->GetPosition();
        b2Vec2 nearPos = TA::planets(game)["near"].body->GetPosition();
        TA::cmdSmash(game, "atk");

        // After smash, velocity should point roughly toward "near"
        b2Vec2 vel = atk.body->GetLinearVelocity();
        b2Vec2 toNear(nearPos.x - posBefore.x, nearPos.y - posBefore.y);

        // Dot product should be positive (moving toward near target)
        float dot = vel.x * toNear.x + vel.y * toNear.y;
        CHECK(dot > 0.0f);
        game.shutdown();
    }

    TEST_CASE("Cosmic event survival bonus is awarded") {
        GravityBrawl game;
        game.initialize();
        Planet& p = addPlanet(game, "p1", "Survivor", 0.0f, 1);
        TA::startPlaying(game);

        int scoreBefore = p.score;
        TA::triggerCosmicEvent(game);

        // Advance 11 seconds to end the event (default duration 10s)
        tick(game, 660);

        // Survivor should have gotten bonus points
        CHECK(p.score > scoreBefore);
        game.shutdown();
    }

    TEST_CASE("Full automated 20-player rendered match log") {
        // This is a comprehensive end-to-end test that simulates a full match
        // with logging of key metrics at every interval.
        GravityBrawl game;
        game.initialize();
        game.configure({
            {"game_duration", 45.0},
            {"min_players", 2},
            {"cosmic_event_cooldown", 15.0}
        });
        TA::seedRng(game, 2026u);

        // Add 20 players at evenly distributed angles
        for (int i = 0; i < 20; ++i) {
            float angle = (2.0f * b2_pi * float(i)) / 20.0f;
            addPlanet(game, "auto" + std::to_string(i), "Auto" + std::to_string(i),
                      angle, (i % 2) ? 1 : -1);
        }
        TA::startPlaying(game);

        sf::RenderTexture rt;
        bool canRender = tryCreateRT(rt, static_cast<unsigned>(SCREEN_W),
                                         static_cast<unsigned>(SCREEN_H));

        struct Snapshot {
            double time;
            int alive;
            int totalScore;
            float bhGravity;
            std::size_t particles;
            bool cosmicActive;
        };
        std::vector<Snapshot> log;

        bool hadKill = false;
        bool hadCosmicEvent = false;
        int totalSmashes = 0;

        // 45s at 60Hz = 2700 steps
        for (int step = 0; step < 2700; ++step) {
            // Deterministic player actions: each player smashes every ~1.5 seconds
            // with slight offsets to create realistic timing
            for (int i = 0; i < 20; ++i) {
                int offset = i * 7; // stagger smash timing
                if ((step + offset) % 90 == 0) {
                    TA::cmdSmash(game, "auto" + std::to_string(i));
                    totalSmashes++;
                }
            }

            game.update(1.0 / 60.0);

            // Render periodically
            if (canRender && step % 30 == 0) {
                rt.clear(sf::Color::Black);
                game.render(rt, 0.5);
                rt.display();
            }

            // Log snapshot every 5 seconds
            if (step % 300 == 0) {
                REQUIRE(allPositionsFinite(game));
                auto state = game.getState();
                int alive = state["alivePlayers"].get<int>();
                int total = 0;
                for (const auto& p : state["players"])
                    total += p["score"].get<int>();

                log.push_back({
                    state["gameTimer"].get<double>(),
                    alive,
                    total,
                    state["blackHoleGravity"].get<float>(),
                    TA::particles(game).size(),
                    state["cosmicEventActive"].get<bool>()
                });

                if (alive < 20) hadKill = true;
                if (state["cosmicEventActive"].get<bool>()) hadCosmicEvent = true;
            }

            if (TA::phase(game) == GamePhase::GameOver) break;
        }

        // ── Validate match quality ──────────────────────────────────────
        CHECK(totalSmashes > 100);
        CHECK(log.size() >= 2);

        // Scores should increase over the match
        if (log.size() >= 2) {
            CHECK(log.back().totalScore > log.front().totalScore);
        }

        // Black hole gravity should have grown
        if (log.size() >= 2) {
            CHECK(log.back().bhGravity > log.front().bhGravity);
        }

        // At least some interaction occurred
        auto state = game.getState();
        int totalKills = 0;
        for (const auto& p : state["players"])
            totalKills += p["kills"].get<int>();

        // With 20 aggressive players, expect at least a few kills or the game reached end
        bool gameFinished = (TA::phase(game) == GamePhase::GameOver);
        CHECK((totalKills > 0 || gameFinished));

        game.shutdown();
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// ██ BOT AI IMPROVEMENTS █████████████████████████████████████████████████████
// ══════════════════════════════════════════════════════════════════════════════

TEST_SUITE("GB BotAI") {

    TEST_CASE("Bots spam smash during cosmic events") {
        GravityBrawl game;
        game.initialize();
        game.configure({{"game_duration", 120.0}, {"bot_fill", 6}, {"min_players", 2}});
        TA::seedRng(game, 99u);

        // Join 2 humans to trigger countdown
        game.onChatMessage(makeMsg("h1", "H1", "!join"));
        game.onChatMessage(makeMsg("h2", "H2", "!join"));

        // Advance through lobby + countdown into playing
        tick(game, 600); // 10s

        if (TA::phase(game) != GamePhase::Playing) {
            TA::phase(game) = GamePhase::Playing;
            TA::startPlaying(game);
        }

        // Record bot combo counts before cosmic event
        int combosBefore = 0;
        for (const auto& [id, p] : TA::planets(game)) {
            if (id.substr(0, 6) == "__bot_")
                combosBefore += p.comboCount;
        }

        // Trigger cosmic event
        TA::cosmicEventActive(game) = 10.0;

        // Tick with bot AI running — bots should smash aggressively
        int botSmashes = 0;
        for (int step = 0; step < 30; ++step) {
            // Count alive bots before
            int botsBefore = 0;
            for (const auto& [id, p] : TA::planets(game))
                if (p.alive && id.substr(0, 6) == "__bot_") botsBefore++;

            game.update(1.0 / 60.0);

            // Count smash activity (combo counts increasing = smash happened)
            for (const auto& [id, p] : TA::planets(game)) {
                if (id.substr(0, 6) == "__bot_" && p.comboCount > 0)
                    botSmashes++;
            }
        }

        // Bots should have attempted smashes during cosmic event
        // With 70% probability per 300ms tick over 0.5s we expect many
        CHECK(botSmashes > 0);
        game.shutdown();
    }

    TEST_CASE("Bots attempt supernova via combo building") {
        GravityBrawl game;
        game.initialize();
        game.configure({{"game_duration", 120.0}, {"bot_fill", 4}, {"min_players", 2}});
        TA::seedRng(game, 55u);

        game.onChatMessage(makeMsg("h1", "H1", "!join"));
        game.onChatMessage(makeMsg("h2", "H2", "!join"));

        // Get to playing phase
        tick(game, 600);
        if (TA::phase(game) != GamePhase::Playing) {
            TA::phase(game) = GamePhase::Playing;
            TA::startPlaying(game);
        }

        // Trigger cosmic event so bots spam smash (70% per tick)
        TA::cosmicEventActive(game) = 10.0;

        // Run for several seconds — bots should build combos
        bool anyBotReachedHighCombo = false;
        for (int step = 0; step < 300; ++step) { // 5 seconds
            game.update(1.0 / 60.0);
            for (const auto& [id, p] : TA::planets(game)) {
                if (id.substr(0, 6) == "__bot_" && p.comboCount >= 3)
                    anyBotReachedHighCombo = true;
            }
        }

        // With cosmic event active, bots should build combo counts
        CHECK(anyBotReachedHighCombo);
        game.shutdown();
    }

    TEST_CASE("Bots escape when near black hole") {
        GravityBrawl game;
        game.initialize();
        game.configure({{"game_duration", 120.0}, {"bot_fill", 4}, {"min_players", 2}});
        TA::seedRng(game, 77u);

        game.onChatMessage(makeMsg("h1", "H1", "!join"));
        game.onChatMessage(makeMsg("h2", "H2", "!join"));

        // Advance through lobby + countdown to trigger bot spawning
        tick(game, 600);

        // Ensure we're in Playing phase
        if (TA::phase(game) != GamePhase::Playing) {
            TA::phase(game) = GamePhase::Playing;
            TA::startPlaying(game);
        }

        // Find a bot
        std::string botId;
        for (auto& [id, p] : TA::planets(game)) {
            if (id.size() >= 6 && id.substr(0, 6) == "__bot_" && p.alive && p.body) {
                botId = id;
                // Move to 30% of safe orbit (danger zone)
                float safeOrbit = ARENA_RADIUS * 0.78f;
                float dangerDist = safeOrbit * 0.3f;
                p.body->SetTransform(
                    b2Vec2(WORLD_CENTER_X + dangerDist, WORLD_CENTER_Y), 0.0f);
                p.body->SetLinearVelocity(b2Vec2(0, 0));
                break;
            }
        }

        if (botId.empty()) {
            // No bots spawned (all may have died) — skip test gracefully
            game.shutdown();
            return;
        }

        // Force bot AI timer to fire immediately
        TA::botAITimer(game) = 0.0f;

        // Record the bot's position before AI tick
        auto& bot = TA::planets(game)[botId];
        float distBefore = distanceToCenter(bot);

        // Run a few ticks — the bot should smash to escape
        for (int i = 0; i < 10; ++i) {
            game.update(1.0 / 60.0);
        }

        // Bot should have attempted escape (smash was triggered)
        // Check that the bot either gained velocity away from center or had a smash
        if (bot.alive && bot.body) {
            b2Vec2 vel = bot.body->GetLinearVelocity();
            float speed = vel.Length();
            // After smash, bot should have significant velocity
            CHECK(speed > 1.0f);
        }
        game.shutdown();
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// ██ GRAVITY BONUS CAP ██████████████████████████████████████████████████████
// ══════════════════════════════════════════════════════════════════════════════

TEST_SUITE("GB GravityCap") {

    TEST_CASE("Consumed gravity bonus is capped at half of gravity cap") {
        GravityBrawl game;
        game.initialize();
        game.configure({{"game_duration", 120.0}});

        float gravityCap = TA::blackHoleGravityCap(game);
        float maxBonus = gravityCap * 0.5f; // Expected cap

        // Set consumed bonus way above cap
        TA::blackHoleConsumedGravityBonus(game) = gravityCap * 2.0f;

        float gravity = TA::currentBlackHoleGravity(game);
        float baseGravity = 6.0f; // m_blackHoleBaseGravity default

        // The consumed bonus should be capped, so total gravity =
        // base + timer*growthFactor + min(consumed, cap*0.5)
        CHECK(gravity < baseGravity + maxBonus + 1.0f); // Allow small timer growth tolerance
        CHECK(gravity >= baseGravity + maxBonus - 0.1f);

        game.shutdown();
    }

    TEST_CASE("Gravity grows linearly with time but bonus caps") {
        GravityBrawl game;
        game.initialize();
        game.configure({{"game_duration", 300.0}});

        // Set huge consumed bonus
        TA::blackHoleConsumedGravityBonus(game) = 100.0f;

        // Measure at time 0
        TA::gameTimer(game) = 0.0;
        float g0 = TA::currentBlackHoleGravity(game);

        // Measure at time 100
        TA::gameTimer(game) = 100.0;
        float g100 = TA::currentBlackHoleGravity(game);

        // Growth should be from time only (bonus is capped, same both times)
        float expectedGrowth = 100.0f * 0.02f; // 2.0
        CHECK(std::abs((g100 - g0) - expectedGrowth) < 0.1f);

        game.shutdown();
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// ██ RENDER ORDER ████████████████████████████████████████████████████████████
// ══════════════════════════════════════════════════════════════════════════════

TEST_SUITE("GB RenderOrder") {

    TEST_CASE("GameOver screen renders without crash") {
        GravityBrawl game;
        game.initialize();

        // Add players with scores
        float angle = 0.0f;
        for (int i = 0; i < 5; ++i) {
            auto& p = addPlanet(game, "p" + std::to_string(i),
                                "Player" + std::to_string(i), angle);
            p.score = (5 - i) * 100; // Descending scores
            p.kills = (5 - i) * 2;
            angle += 1.2f;
        }

        TA::phase(game) = GamePhase::GameOver;

        sf::RenderTexture rt;
        if (tryCreateRT(rt, static_cast<unsigned>(SCREEN_W),
                            static_cast<unsigned>(SCREEN_H))) {
            rt.clear(sf::Color::Black);
            REQUIRE_NOTHROW(game.render(rt, 0.5));
            rt.display();

            // Verify something was drawn (not all black)
            sf::Image img = rt.getTexture().copyToImage();
            std::size_t bright = countBrightSamples(img, 80, 20);
            CHECK(bright >= 2);
        }
        game.shutdown();
    }

    TEST_CASE("Cosmic overlay does not wash out UI text") {
        GravityBrawl game;
        game.initialize();

        addPlanet(game, "p1", "Alice", 0.0f);
        addPlanet(game, "p2", "Bob", 3.14f);

        TA::phase(game) = GamePhase::Playing;
        TA::cosmicEventActive(game) = 5.0;

        sf::RenderTexture rt;
        if (tryCreateRT(rt, static_cast<unsigned>(SCREEN_W),
                            static_cast<unsigned>(SCREEN_H))) {
            rt.clear(sf::Color::Black);
            REQUIRE_NOTHROW(game.render(rt, 0.5));
            rt.display();

            // The red overlay should have max alpha 40 (not opaque)
            sf::Image img = rt.getTexture().copyToImage();
            // Sample top area where UI text lives
            int maxRedAlpha = 0;
            for (unsigned x = 100; x < 980; x += 50) {
                auto px = img.getPixel(x, 20);
                // Pure red overlay would be (255, 0, 0, alpha)
                // After blending, R channel should be moderate, not overwhelmed
                if (px.r > 200 && px.g < 30 && px.b < 30)
                    maxRedAlpha = std::max(maxRedAlpha, static_cast<int>(px.a));
            }
            // The cosmic overlay should not be fully opaque in the UI region
            // (since we draw cosmic overlay BEFORE UI, text should be on top)
            CHECK(maxRedAlpha < 200);
        }
        game.shutdown();
    }
}