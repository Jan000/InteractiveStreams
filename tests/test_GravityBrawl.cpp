#include <doctest/doctest.h>

#include <SFML/Graphics/RenderTexture.hpp>

#include <cmath>
#include <cstddef>
#include <string>

#include "core/Application.h"
#include "core/PlayerDatabase.h"
#include "games/gravity_brawl/GravityBrawl.h"

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

void PlayerDatabase::recordResult(const std::string&, const std::string&, const std::string&, int, bool) {}

} // namespace is::core

namespace is::games::gravity_brawl {

struct GravityBrawlTestAccess {
    static std::unordered_map<std::string, Planet>& planets(GravityBrawl& game) { return game.m_planets; }
    static const std::unordered_map<std::string, Planet>& planets(const GravityBrawl& game) { return game.m_planets; }
    static void spawnPlanetBody(GravityBrawl& game, Planet& planet) { game.spawnPlanetBody(planet); }
    static void spawnBots(GravityBrawl& game) { game.spawnBots(); }
    static void startPlaying(GravityBrawl& game) { game.startPlaying(); }
    static void eliminatePlanet(GravityBrawl& game, Planet& planet) { game.eliminatePlanet(planet); }
    static float currentBlackHoleGravity(const GravityBrawl& game) { return game.currentBlackHoleGravity(); }
    static float spawnRadiusFactor(const GravityBrawl& game) { return game.m_spawnRadiusFactor; }
    static float spawnOrbitSpeed(const GravityBrawl& game) { return game.m_spawnOrbitSpeed; }
    static float blackHoleKillRadiusMultiplier(const GravityBrawl& game) { return game.m_blackHoleKillRadiusMultiplier; }
    static float& blackHoleConsumedGravityBonus(GravityBrawl& game) { return game.m_blackHoleConsumedGravityBonus; }
    static float blackHoleConsumeSizeFactor(const GravityBrawl& game) { return game.m_blackHoleConsumeSizeFactor; }
    static double& gameTimer(GravityBrawl& game) { return game.m_gameTimer; }
};

} // namespace is::games::gravity_brawl

namespace {

using namespace is::games::gravity_brawl;

Planet& addPlanet(GravityBrawl& game,
                  const std::string& userId,
                  const std::string& displayName,
                  float angle,
                  int orbitDirection = 1) {
    Planet& p = GravityBrawlTestAccess::planets(game)[userId];
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

    GravityBrawlTestAccess::spawnPlanetBody(game, p);

    const float spawnRadius = ARENA_RADIUS * GravityBrawlTestAccess::spawnRadiusFactor(game);
    const float x = WORLD_CENTER_X + spawnRadius * std::cos(angle);
    const float y = WORLD_CENTER_Y + spawnRadius * std::sin(angle);
    const float dir = static_cast<float>(orbitDirection);
    const float vx = -GravityBrawlTestAccess::spawnOrbitSpeed(game) * std::sin(angle) * dir;
    const float vy =  GravityBrawlTestAccess::spawnOrbitSpeed(game) * std::cos(angle) * dir;

    p.orbitDirection = orbitDirection;
    p.body->SetTransform(b2Vec2(x, y), 0.0f);
    p.body->SetLinearVelocity(b2Vec2(vx, vy));
    p.prevPosition = {x, y};
    p.renderPosition = {x, y};
    return p;
}

int aliveCount(const GravityBrawl& game) {
    int count = 0;
    for (const auto& [_, p] : GravityBrawlTestAccess::planets(game)) {
        if (p.alive) {
            ++count;
        }
    }
    return count;
}

float distanceToCenter(const Planet& p) {
    const b2Vec2 pos = p.body->GetPosition();
    const float dx = pos.x - WORLD_CENTER_X;
    const float dy = pos.y - WORLD_CENTER_Y;
    return std::sqrt(dx * dx + dy * dy);
}

} // namespace

TEST_SUITE("Gravity Brawl") {

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

    TEST_CASE("Bot fill can automatically populate the configured target") {
        GravityBrawl game;
        game.initialize();
        game.configure({{"bot_fill", 3}});

        GravityBrawlTestAccess::spawnBots(game);

        CHECK(aliveCount(game) == 3);
        CHECK(GravityBrawlTestAccess::planets(game).count("__bot_1") == 1);
        CHECK(GravityBrawlTestAccess::planets(game).count("__bot_2") == 1);
        CHECK(GravityBrawlTestAccess::planets(game).count("__bot_3") == 1);

        game.shutdown();
    }

    TEST_CASE("Default tuning keeps starting planets alive for multiple seconds") {
        GravityBrawl game;
        game.initialize();
        addPlanet(game, "p1", "Alpha", 0.0f, 1);
        addPlanet(game, "p2", "Beta", 2.0943951f, -1);
        addPlanet(game, "p3", "Gamma", 4.1887902f, 1);
        GravityBrawlTestAccess::startPlaying(game);

        for (int i = 0; i < 300; ++i) {
            game.update(1.0 / 60.0);
        }

        CHECK(aliveCount(game) == 3);
        for (const auto& [_, planet] : GravityBrawlTestAccess::planets(game)) {
            CHECK(planet.alive);
            CHECK(distanceToCenter(planet) > BLACK_HOLE_RADIUS * GravityBrawlTestAccess::blackHoleKillRadiusMultiplier(game));
        }

        game.shutdown();
    }

    TEST_CASE("Black hole gravity grows over time and when consuming larger planets") {
        GravityBrawl game;
        game.initialize();
        GravityBrawlTestAccess::startPlaying(game);

        const float baseGravity = GravityBrawlTestAccess::currentBlackHoleGravity(game);
        GravityBrawlTestAccess::gameTimer(game) = 120.0;
        const float timeGrownGravity = GravityBrawlTestAccess::currentBlackHoleGravity(game);

        CHECK(timeGrownGravity > baseGravity);

        Planet& victim = addPlanet(game, "victim", "Victim", 0.0f, 1);
        victim.radiusMeters = 0.9f;
        victim.lastHitBy.clear();
        const float beforeConsumeBonus = GravityBrawlTestAccess::blackHoleConsumedGravityBonus(game);

        GravityBrawlTestAccess::eliminatePlanet(game, victim);

        CHECK(GravityBrawlTestAccess::blackHoleConsumedGravityBonus(game) == doctest::Approx(beforeConsumeBonus + 0.9f * GravityBrawlTestAccess::blackHoleConsumeSizeFactor(game)));
        CHECK(GravityBrawlTestAccess::currentBlackHoleGravity(game) > timeGrownGravity);

        game.shutdown();
    }

    TEST_CASE("Render smoke test produces visible frame data") {
        GravityBrawl game;
        game.initialize();
        addPlanet(game, "p1", "Alpha", 0.0f, 1);
        addPlanet(game, "p2", "Beta", 3.1415926f, -1);
        GravityBrawlTestAccess::startPlaying(game);
        game.update(1.0 / 30.0);

        sf::RenderTexture target;
        if (!target.create(static_cast<unsigned int>(SCREEN_W), static_cast<unsigned int>(SCREEN_H))) {
            game.shutdown();
            return;
        }

        target.clear(sf::Color::Black);
        game.render(target, 0.0);
        target.display();

        const sf::Image image = target.getTexture().copyToImage();
        std::size_t brightSamples = 0;
        for (unsigned int y = 0; y < image.getSize().y; y += 80) {
            for (unsigned int x = 0; x < image.getSize().x; x += 80) {
                const auto pixel = image.getPixel(x, y);
                if (static_cast<int>(pixel.r) + static_cast<int>(pixel.g) + static_cast<int>(pixel.b) > 30) {
                    ++brightSamples;
                }
            }
        }

        const auto center = image.getPixel(image.getSize().x / 2, image.getSize().y / 2);
        CHECK(brightSamples >= 4);
        CHECK(static_cast<int>(center.r) + static_cast<int>(center.g) + static_cast<int>(center.b) < 80);

        game.shutdown();
    }
}