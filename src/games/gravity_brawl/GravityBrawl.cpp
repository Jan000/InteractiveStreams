#include "games/gravity_brawl/GravityBrawl.h"
#include "games/GameRegistry.h"
#include "core/Application.h"
#include "core/PlayerDatabase.h"

#include <spdlog/spdlog.h>
#include <cmath>
#include <algorithm>
#include <numeric>

namespace is::games::gravity_brawl {

// ── Registration ─────────────────────────────────────────────────────────────
REGISTER_GAME(GravityBrawl, "gravity_brawl");

// ── Tier Colors (must be before member functions that use them) ──────────────

static sf::Color getTierColor(PlanetTier tier) {
    switch (tier) {
    case PlanetTier::Asteroid:  return sf::Color(160, 160, 170);
    case PlanetTier::IcePlanet: return sf::Color(100, 180, 255);
    case PlanetTier::GasGiant:  return sf::Color(255, 100, 50);
    case PlanetTier::Star:      return sf::Color(255, 255, 150);
    default:                    return sf::Color::White;
    }
}

static sf::Color getTierGlowColor(PlanetTier tier) {
    switch (tier) {
    case PlanetTier::Asteroid:  return sf::Color(100, 100, 110, 60);
    case PlanetTier::IcePlanet: return sf::Color(80, 150, 255, 80);
    case PlanetTier::GasGiant:  return sf::Color(255, 80, 30, 100);
    case PlanetTier::Star:      return sf::Color(255, 255, 100, 120);
    default:                    return sf::Color(255, 255, 255, 60);
    }
}

// ── Contact Listener ─────────────────────────────────────────────────────────

void GravityBrawl::ContactListener::BeginContact(b2Contact* contact) {
    b2Body* bodyA = contact->GetFixtureA()->GetBody();
    b2Body* bodyB = contact->GetFixtureB()->GetBody();

    auto* dataA = reinterpret_cast<Planet*>(bodyA->GetUserData().pointer);
    auto* dataB = reinterpret_cast<Planet*>(bodyB->GetUserData().pointer);

    if (!dataA || !dataB) return;
    if (!dataA->alive || !dataB->alive) return;

    // Get collision impulse estimate from relative velocity
    b2Vec2 relVel = bodyA->GetLinearVelocity() - bodyB->GetLinearVelocity();
    float impulse = relVel.Length();

    m_game.onPlanetCollision(*dataA, *dataB, impulse);
}

// ── Constructor / Destructor ─────────────────────────────────────────────────

GravityBrawl::GravityBrawl() : m_rng(std::random_device{}()) {}
GravityBrawl::~GravityBrawl() = default;

// ── Helpers ──────────────────────────────────────────────────────────────────

sf::Vector2f GravityBrawl::worldToScreen(float wx, float wy) const {
    // Center of screen maps to WORLD_CENTER
    float sx = SCREEN_W / 2.0f + (wx - WORLD_CENTER_X) * PIXELS_PER_METER;
    float sy = SCREEN_H / 2.0f + (wy - WORLD_CENTER_Y) * PIXELS_PER_METER;
    return {sx, sy};
}

sf::Vector2f GravityBrawl::worldToScreen(b2Vec2 pos) const {
    return worldToScreen(pos.x, pos.y);
}

// ── Initialize / Shutdown ────────────────────────────────────────────────────

void GravityBrawl::initialize() {
    spdlog::info("[GravityBrawl] Initializing...");

    // Create Box2D world with zero gravity (we apply custom forces)
    m_world = std::make_unique<b2World>(b2Vec2(0.0f, 0.0f));
    m_contactListener = std::make_unique<ContactListener>(*this);
    m_world->SetContactListener(m_contactListener.get());

    // Initialize rendering
    m_background.initialize(
        static_cast<unsigned int>(SCREEN_W),
        static_cast<unsigned int>(SCREEN_H));
    m_postProcessing.initialize(
        static_cast<unsigned int>(SCREEN_W),
        static_cast<unsigned int>(SCREEN_H));

    // Load font
    if (m_font.loadFromFile("assets/fonts/JetBrainsMono-Regular.ttf")) {
        m_fontLoaded = true;
    } else {
        spdlog::warn("[GravityBrawl] Font not found, text rendering disabled.");
    }

    m_phase = GamePhase::Lobby;
    m_lobbyTimer = m_lobbyDuration;
    m_gameTimer = 0.0;
    m_cosmicEventTimer = m_cosmicEventCooldown;
    m_cosmicEventActive = 0.0;
    m_currentKingId.clear();
    m_blackHolePulse = 0.0f;
    m_blackHoleRotation = 0.0f;

    spdlog::info("[GravityBrawl] Initialized.");
}

void GravityBrawl::shutdown() {
    spdlog::info("[GravityBrawl] Shutting down...");

    // Destroy all bodies before the world
    for (auto& [id, p] : m_planets) {
        if (p.body && m_world) {
            m_world->DestroyBody(p.body);
            p.body = nullptr;
        }
    }
    m_planets.clear();
    m_particles.clear();
    m_killFeed.clear();
    m_floatingTexts.clear();
    m_contactListener.reset();
    m_world.reset();
}

// ── Chat Message Handling ────────────────────────────────────────────────────

void GravityBrawl::onChatMessage(const platform::ChatMessage& msg) {
    std::string text = msg.text;
    // Lowercase for command matching
    std::string lower;
    lower.resize(text.size());
    std::transform(text.begin(), text.end(), lower.begin(), ::tolower);

    // Trim leading whitespace
    size_t start = lower.find_first_not_of(" \t");
    if (start == std::string::npos) return;
    lower = lower.substr(start);

    if (lower == "!join" || lower == "!play") {
        cmdJoin(msg.userId, msg.displayName);
    } else if (lower == "!smash" || lower == "!s") {
        cmdSmash(msg.userId);
    }
}

void GravityBrawl::cmdJoin(const std::string& userId, const std::string& displayName) {
    // Check if already alive
    auto it = m_planets.find(userId);
    if (it != m_planets.end() && it->second.alive) {
        return; // Already in the game
    }

    // Create or reset planet
    Planet& p = m_planets[userId];
    p.userId = userId;
    p.displayName = displayName;
    p.alive = true;
    p.kills = 0;
    p.deaths = 0;
    p.survivalTime = 0.0;
    p.score = 0;
    p.hitCount = 0;
    p.tier = PlanetTier::Asteroid;
    p.baseRadius = 0.5f;
    p.radiusMeters = 0.5f;
    p.smashCooldown = 0.0f;
    p.comboCount = 0;
    p.comboWindowEnd = 0.0;
    p.lastHitBy.clear();
    p.lastHitTime = 0.0;
    p.isKing = false;
    p.hitFlashTimer = 0.0f;
    p.trailTimer = 0.0f;
    p.animTimer = 0.0f;
    p.glowPulse = 0.0f;
    p.supernovaTimer = 0.0f;

    // Destroy old body if exists
    if (p.body && m_world) {
        m_world->DestroyBody(p.body);
        p.body = nullptr;
    }

    // Spawn in safe orbit
    spawnPlanetBody(p);

    sendChatFeedback("☄️ " + displayName + " entered the orbit!");

    // Score: participation
    try {
        is::core::Application::instance().playerDatabase().recordResult(
            userId, displayName, "gravity_brawl", 1, false);
    } catch (...) {}

    // If in lobby and enough players, start countdown
    if (m_phase == GamePhase::Lobby) {
        int aliveCnt = 0;
        for (const auto& [_, pl] : m_planets)
            if (pl.alive) aliveCnt++;
        if (aliveCnt >= m_minPlayers && m_lobbyTimer > 5.0) {
            m_lobbyTimer = 5.0; // Shorten lobby
        }
    }
}

void GravityBrawl::cmdSmash(const std::string& userId) {
    auto it = m_planets.find(userId);
    if (it == m_planets.end() || !it->second.alive) return;
    if (m_phase != GamePhase::Playing) return;

    Planet& p = it->second;

    // Track combo
    double now = m_gameTimer;
    if (now < p.comboWindowEnd) {
        p.comboCount++;
    } else {
        p.comboCount = 1;
    }
    p.comboWindowEnd = now + Planet::COMBO_WINDOW;
    p.lastSmashTime = now;

    // Check supernova
    if (p.comboCount >= Planet::SUPERNOVA_COMBO) {
        triggerSupernova(p);
        p.comboCount = 0;
        return;
    }

    // Normal smash with cooldown
    if (p.smashCooldown > 0.0f) return;
    triggerSmash(p);
}

// ── Spawn ────────────────────────────────────────────────────────────────────

void GravityBrawl::spawnPlanetBody(Planet& p) {
    // Random angle on safe orbit
    std::uniform_real_distribution<float> angleDist(0.0f, 2.0f * b2_pi);
    float angle = angleDist(m_rng);
    float spawnRadius = ARENA_RADIUS * 0.8f; // Slightly inside the full orbit

    float x = WORLD_CENTER_X + spawnRadius * std::cos(angle);
    float y = WORLD_CENTER_Y + spawnRadius * std::sin(angle);

    // Create circular body
    b2BodyDef bodyDef;
    bodyDef.type = b2_dynamicBody;
    bodyDef.position.Set(x, y);
    bodyDef.linearDamping = 0.3f;
    bodyDef.angularDamping = 1.0f;
    bodyDef.fixedRotation = true;
    bodyDef.userData.pointer = reinterpret_cast<uintptr_t>(&p);

    p.body = m_world->CreateBody(&bodyDef);

    b2CircleShape shape;
    shape.m_radius = p.baseRadius;

    b2FixtureDef fixDef;
    fixDef.shape = &shape;
    fixDef.density = 1.0f;
    fixDef.friction = 0.3f;
    fixDef.restitution = 0.7f; // Bouncy!

    p.body->CreateFixture(&fixDef);

    // Give initial tangential velocity for orbit
    float speed = 5.0f; // orbital speed
    float vx = -speed * std::sin(angle);
    float vy =  speed * std::cos(angle);
    p.body->SetLinearVelocity(b2Vec2(vx, vy));

    // Store initial position for interpolation
    b2Vec2 pos = p.body->GetPosition();
    p.prevPosition = {pos.x, pos.y};
    p.renderPosition = p.prevPosition;
}

// ── Game Logic: Smash & Supernova ────────────────────────────────────────────

void GravityBrawl::triggerSmash(Planet& p) {
    if (!p.body) return;

    p.smashCooldown = Planet::SMASH_COOLDOWN;

    b2Vec2 vel = p.body->GetLinearVelocity();
    float speed = vel.Length();
    if (speed < 0.1f) {
        // Default dash direction: away from center
        b2Vec2 pos = p.body->GetPosition();
        b2Vec2 dir(pos.x - WORLD_CENTER_X, pos.y - WORLD_CENTER_Y);
        float len = dir.Length();
        if (len > 0.01f) dir = b2Vec2(dir.x / len, dir.y / len);
        p.body->ApplyLinearImpulseToCenter(b2Vec2(dir.x * 15.0f, dir.y * 15.0f), true);
    } else {
        // Boost in current direction
        b2Vec2 dir(vel.x / speed, vel.y / speed);
        float impulse = 12.0f + p.getMassScale() * 2.0f;
        p.body->ApplyLinearImpulseToCenter(b2Vec2(dir.x * impulse, dir.y * impulse), true);
    }

    // Trail effect
    b2Vec2 pos = p.body->GetPosition();
    sf::Vector2f screenPos = worldToScreen(pos);
    sf::Color trailColor = getTierColor(p.tier);
    emitTrail(screenPos, trailColor);
    p.trailTimer = 0.3f;
}

void GravityBrawl::triggerSupernova(Planet& p) {
    if (!p.body) return;

    p.smashCooldown = 2.0f; // Longer cooldown after supernova
    p.supernovaTimer = 0.5f;

    b2Vec2 pos = p.body->GetPosition();

    // Apply radial impulse to all nearby planets
    float supernovaRadius = 8.0f; // meters
    for (auto& [id, other] : m_planets) {
        if (!other.alive || !other.body || id == p.userId) continue;

        b2Vec2 otherPos = other.body->GetPosition();
        b2Vec2 diff(otherPos.x - pos.x, otherPos.y - pos.y);
        float dist = diff.Length();

        if (dist < supernovaRadius && dist > 0.1f) {
            b2Vec2 dir(diff.x / dist, diff.y / dist);
            float force = (1.0f - dist / supernovaRadius) * 30.0f;
            other.body->ApplyLinearImpulseToCenter(
                b2Vec2(dir.x * force, dir.y * force), true);
        }
    }

    // Massive particle effect
    sf::Vector2f screenPos = worldToScreen(pos);
    emitSupernovaWave(screenPos, getTierColor(p.tier));

    addFloatingText("SUPERNOVA!", screenPos, sf::Color(255, 200, 50), 2.0f);
}

// ── Collision Handler ────────────────────────────────────────────────────────

void GravityBrawl::onPlanetCollision(Planet& a, Planet& b, float impulse) {
    if (impulse < 2.0f) return; // Ignore gentle touches

    // Both planets get pushed — mark last-hit-by for kill attribution
    double now = m_gameTimer;

    // a hit b and b hit a
    b.lastHitBy = a.userId;
    b.lastHitTime = now;
    a.lastHitBy = b.userId;
    a.lastHitTime = now;

    // Hit flash
    a.hitFlashTimer = 0.15f;
    b.hitFlashTimer = 0.15f;

    // Hit marker particles
    b2Vec2 midPoint;
    midPoint.x = (a.body->GetPosition().x + b.body->GetPosition().x) / 2.0f;
    midPoint.y = (a.body->GetPosition().y + b.body->GetPosition().y) / 2.0f;
    sf::Vector2f screenMid = worldToScreen(midPoint);
    emitParticles(screenMid, sf::Color(255, 220, 100), 15, 80.f, 360.f, 0.5f);

    // +2 points for successful ram (both get credit)
    a.hitCount++;
    b.hitCount++;
    a.score += 2;
    b.score += 2;

    try {
        auto& db = is::core::Application::instance().playerDatabase();
        db.recordResult(a.userId, a.displayName, "gravity_brawl", 2, false);
        db.recordResult(b.userId, b.displayName, "gravity_brawl", 2, false);
    } catch (...) {}

    // Floating text
    addFloatingText("+2", worldToScreen(a.body->GetPosition()), sf::Color(100, 255, 100), 0.8f);
    addFloatingText("+2", worldToScreen(b.body->GetPosition()), sf::Color(100, 255, 100), 0.8f);
}

// ── Phase Management ─────────────────────────────────────────────────────────

void GravityBrawl::startCountdown() {
    m_phase = GamePhase::Countdown;
    m_countdownTimer = 3.0;
    spdlog::info("[GravityBrawl] Countdown started.");
}

void GravityBrawl::startPlaying() {
    m_phase = GamePhase::Playing;
    m_gameTimer = 0.0;
    m_cosmicEventTimer = m_cosmicEventCooldown;
    m_cosmicEventActive = 0.0;
    m_survivalAccum = 0.0;
    spdlog::info("[GravityBrawl] Game started!");
}

// ── Update ───────────────────────────────────────────────────────────────────

void GravityBrawl::update(double dt) {
    float fdt = static_cast<float>(dt);

    // Always update visuals
    m_background.update(fdt);
    m_blackHolePulse += fdt * 1.5f;
    m_blackHoleRotation += fdt * 0.5f;
    updateParticles(fdt);

    // Update floating texts
    for (auto it = m_floatingTexts.begin(); it != m_floatingTexts.end();) {
        it->timer -= fdt;
        it->position.y -= 40.0f * fdt; // Float upward
        if (it->timer <= 0.0f) {
            it = m_floatingTexts.erase(it);
        } else {
            ++it;
        }
    }

    // Update kill feed
    for (auto it = m_killFeed.begin(); it != m_killFeed.end();) {
        it->timeRemaining -= dt;
        if (it->timeRemaining <= 0.0) {
            it = m_killFeed.erase(it);
        } else {
            ++it;
        }
    }

    switch (m_phase) {
    case GamePhase::Lobby: {
        m_lobbyTimer -= dt;

        // Update planet positions even in lobby (orbiting looks cool)
        for (auto& [id, p] : m_planets) {
            if (p.body && p.alive) {
                b2Vec2 pos = p.body->GetPosition();
                p.prevPosition = {pos.x, pos.y};
                p.updateTimers(fdt);
            }
        }
        applyOrbitalForces(fdt);
        m_world->Step(fdt, 6, 2);
        for (auto& [id, p] : m_planets) {
            if (p.body && p.alive) {
                b2Vec2 pos = p.body->GetPosition();
                p.renderPosition = {pos.x, pos.y};
            }
        }

        if (m_lobbyTimer <= 0.0) {
            int aliveCnt = 0;
            for (const auto& [_, p] : m_planets)
                if (p.alive) aliveCnt++;
            if (aliveCnt >= m_minPlayers) {
                startCountdown();
            } else {
                m_lobbyTimer = m_lobbyDuration;
            }
        }
        break;
    }

    case GamePhase::Countdown: {
        m_countdownTimer -= dt;

        // Keep orbiting during countdown
        for (auto& [id, p] : m_planets) {
            if (p.body && p.alive) {
                b2Vec2 pos = p.body->GetPosition();
                p.prevPosition = {pos.x, pos.y};
                p.updateTimers(fdt);
            }
        }
        applyOrbitalForces(fdt);
        m_world->Step(fdt, 6, 2);
        for (auto& [id, p] : m_planets) {
            if (p.body && p.alive) {
                b2Vec2 pos = p.body->GetPosition();
                p.renderPosition = {pos.x, pos.y};
            }
        }

        if (m_countdownTimer <= 0.0) {
            startPlaying();
        }
        break;
    }

    case GamePhase::Playing: {
        m_gameTimer += dt;

        // Store previous positions for interpolation
        for (auto& [id, p] : m_planets) {
            if (p.body && p.alive) {
                b2Vec2 pos = p.body->GetPosition();
                p.prevPosition = {pos.x, pos.y};
                p.updateTimers(fdt);
            }
        }

        // Apply forces
        applyOrbitalForces(fdt);
        applyBlackHoleGravity(fdt);
        updateCosmicEvent(fdt);

        // Step physics
        m_world->Step(fdt, 8, 3);

        // Post-physics: update render positions
        for (auto& [id, p] : m_planets) {
            if (p.body && p.alive) {
                b2Vec2 pos = p.body->GetPosition();
                p.renderPosition = {pos.x, pos.y};

                // Survival time
                p.survivalTime += dt;

                // Trail particles for higher tiers
                if (p.tier >= PlanetTier::IcePlanet) {
                    sf::Vector2f screenPos = worldToScreen(pos);
                    emitTrail(screenPos, getTierColor(p.tier));
                }

                // Star tier ambient particles
                if (p.tier == PlanetTier::Star && static_cast<int>(p.animTimer * 10) % 3 == 0) {
                    sf::Vector2f screenPos = worldToScreen(pos);
                    emitParticles(screenPos, sf::Color(255, 255, 150, 180), 1, 20.f, 360.f, 0.8f);
                }

                // Clamp velocity to prevent crazy speeds
                b2Vec2 vel = p.body->GetLinearVelocity();
                float maxSpeed = 25.0f;
                if (vel.Length() > maxSpeed) {
                    vel.Normalize();
                    vel = b2Vec2(vel.x * maxSpeed, vel.y * maxSpeed);
                    p.body->SetLinearVelocity(vel);
                }

                // Update fixture radius if grown
                float newRadius = p.getVisualRadius();
                if (std::abs(newRadius - p.radiusMeters) > 0.01f) {
                    p.radiusMeters = newRadius;
                    // Recreate fixture with new size
                    if (p.body->GetFixtureList()) {
                        p.body->DestroyFixture(p.body->GetFixtureList());
                    }
                    b2CircleShape shape;
                    shape.m_radius = newRadius;
                    b2FixtureDef fixDef;
                    fixDef.shape = &shape;
                    fixDef.density = 1.0f * p.getMassScale();
                    fixDef.friction = 0.3f;
                    fixDef.restitution = 0.7f;
                    p.body->CreateFixture(&fixDef);
                }
            }
        }

        // Check deaths (black hole)
        checkBlackHoleDeaths();

        // Survival points
        awardSurvivalPoints(dt);

        // Update king
        updateKing();

        // Cosmic event
        m_cosmicEventTimer -= dt;
        if (m_cosmicEventTimer <= 0.0 && m_cosmicEventActive <= 0.0) {
            triggerCosmicEvent();
        }

        // Check game over (time limit or <= 1 alive)
        int aliveCnt = 0;
        for (const auto& [_, p] : m_planets)
            if (p.alive) aliveCnt++;

        if (m_gameTimer >= m_gameDuration || (aliveCnt <= 1 && m_planets.size() > 1)) {
            m_phase = GamePhase::GameOver;
            spdlog::info("[GravityBrawl] Game over!");

            // Award win to the last player standing or highest score
            std::string winnerId;
            int bestScore = -1;
            for (const auto& [id, p] : m_planets) {
                if (p.score > bestScore) {
                    bestScore = p.score;
                    winnerId = id;
                }
            }
            if (!winnerId.empty()) {
                const auto& winner = m_planets[winnerId];
                try {
                    is::core::Application::instance().playerDatabase().recordResult(
                        winnerId, winner.displayName, "gravity_brawl", 100, true);
                } catch (...) {}
                sendChatFeedback("🏆 " + winner.displayName + " wins Gravity Brawl with " +
                                 std::to_string(winner.score) + " points!");
            }
        }

        // Allow late joins during playing phase
        break;
    }

    case GamePhase::GameOver:
        // Brief display before the GameManager handles transition
        break;
    }
}

// ── Orbital Forces ───────────────────────────────────────────────────────────

void GravityBrawl::applyOrbitalForces(float dt) {
    for (auto& [id, p] : m_planets) {
        if (!p.alive || !p.body) continue;

        b2Vec2 pos = p.body->GetPosition();
        b2Vec2 toCenter(WORLD_CENTER_X - pos.x, WORLD_CENTER_Y - pos.y);
        float dist = toCenter.Length();

        if (dist < 0.1f) continue;

        b2Vec2 dir(toCenter.x / dist, toCenter.y / dist);

        // Gravitational pull toward center (inversely weakens near safe orbit)
        float targetDist = ARENA_RADIUS * 0.7f;
        float pullStrength = m_normalGravity;

        // Stronger pull if too far out, weaker if near safe orbit
        if (dist > targetDist * 1.2f) {
            pullStrength *= 1.5f;
        } else if (dist > targetDist * 0.8f && dist < targetDist * 1.2f) {
            pullStrength *= 0.5f; // Gentle in safe zone
        }

        // Tangential force for orbit (perpendicular to radial direction)
        b2Vec2 tangent(-dir.y, dir.x);
        float orbitalSpeed = 4.0f;

        // Apply forces
        float mass = p.body->GetMass();
        p.body->ApplyForceToCenter(
            b2Vec2(dir.x * pullStrength * mass,
                   dir.y * pullStrength * mass), true);
        p.body->ApplyForceToCenter(
            b2Vec2(tangent.x * orbitalSpeed * mass,
                   tangent.y * orbitalSpeed * mass), true);
    }
}

void GravityBrawl::applyBlackHoleGravity(float dt) {
    float gravMul = (m_cosmicEventActive > 0.0) ? m_eventGravityMul : 1.0f;

    for (auto& [id, p] : m_planets) {
        if (!p.alive || !p.body) continue;

        b2Vec2 pos = p.body->GetPosition();
        b2Vec2 toCenter(WORLD_CENTER_X - pos.x, WORLD_CENTER_Y - pos.y);
        float dist = toCenter.Length();

        if (dist < 0.5f) continue;

        // Black hole pull: stronger the closer you get
        float pullForce = (m_normalGravity * gravMul * 2.0f) / (dist * 0.5f);
        pullForce = std::min(pullForce, 80.0f); // Cap it

        b2Vec2 dir(toCenter.x / dist, toCenter.y / dist);
        float mass = p.body->GetMass();
        p.body->ApplyForceToCenter(
            b2Vec2(dir.x * pullForce * mass,
                   dir.y * pullForce * mass), true);
    }
}

// ── Black Hole Deaths ────────────────────────────────────────────────────────

void GravityBrawl::checkBlackHoleDeaths() {
    for (auto& [id, p] : m_planets) {
        if (!p.alive || !p.body) continue;

        b2Vec2 pos = p.body->GetPosition();
        float dx = pos.x - WORLD_CENTER_X;
        float dy = pos.y - WORLD_CENTER_Y;
        float dist = std::sqrt(dx * dx + dy * dy);

        if (dist < BLACK_HOLE_RADIUS) {
            eliminatePlanet(p);
        }
    }
}

void GravityBrawl::eliminatePlanet(Planet& p) {
    if (!p.alive) return;
    p.alive = false;
    p.deaths++;

    // Explosion particles
    sf::Vector2f screenPos = worldToScreen(p.body->GetPosition());
    emitExplosion(screenPos, getTierColor(p.tier), 80);

    // Kill attribution
    std::string killerId = p.lastHitBy;
    double timeSinceHit = m_gameTimer - p.lastHitTime;
    bool attributed = !killerId.empty() && timeSinceHit < 5.0;

    bool wasBounty = p.isKing;

    if (attributed) {
        auto kit = m_planets.find(killerId);
        if (kit != m_planets.end() && kit->second.alive) {
            Planet& killer = kit->second;

            // Kill points
            int killPoints = 50;
            killer.kills++;
            killer.score += killPoints;

            // Floating text
            sf::Vector2f killerScreen = worldToScreen(killer.body->GetPosition());
            addFloatingText("+50", killerScreen, sf::Color(255, 215, 0), 1.5f);

            // Bounty bonus
            if (wasBounty) {
                int bountyBonus = 150;
                killer.score += bountyBonus;
                addFloatingText("+150 BOUNTY!", killerScreen, sf::Color(255, 50, 50), 2.0f);

                // Massive particle rain
                emitExplosion(killerScreen, sf::Color(255, 215, 0), 150);

                try {
                    is::core::Application::instance().playerDatabase().recordResult(
                        killerId, killer.displayName, "gravity_brawl", killPoints + bountyBonus, false);
                } catch (...) {}

                sendChatFeedback("👑💀 " + killer.displayName + " destroyed the King " +
                                 p.displayName + " for +" + std::to_string(bountyBonus) + " bounty!");
            } else {
                try {
                    is::core::Application::instance().playerDatabase().recordResult(
                        killerId, killer.displayName, "gravity_brawl", killPoints, false);
                } catch (...) {}
            }

            // Kill feed
            m_killFeed.push_front({killer.displayName, p.displayName, wasBounty, 6.0});
            if (m_killFeed.size() > 8) m_killFeed.pop_back();

            sendChatFeedback("🕳️ " + killer.displayName + " smashed " +
                             p.displayName + " into the void!");
        }
    } else {
        // Died by gravity (no attribution)
        m_killFeed.push_front({"The Void", p.displayName, wasBounty, 6.0});
        if (m_killFeed.size() > 8) m_killFeed.pop_back();
        sendChatFeedback("🕳️ " + p.displayName + " was consumed by the void!");
    }

    // Destroy physics body
    if (p.body && m_world) {
        m_world->DestroyBody(p.body);
        p.body = nullptr;
    }
}

// ── King / Bounty System ─────────────────────────────────────────────────────

void GravityBrawl::updateKing() {
    // Find the alive player with the most kills (tiebreak: longest survival)
    std::string bestId;
    int bestKills = 0;
    double bestSurvival = 0.0;

    for (const auto& [id, p] : m_planets) {
        if (!p.alive) continue;
        if (p.kills > bestKills || (p.kills == bestKills && p.survivalTime > bestSurvival)) {
            bestKills = p.kills;
            bestSurvival = p.survivalTime;
            bestId = id;
        }
    }

    // Need at least 1 kill to be king
    if (bestKills < 1) bestId.clear();

    // Update king flag
    if (bestId != m_currentKingId) {
        for (auto& [id, p] : m_planets) p.isKing = false;
        if (!bestId.empty()) {
            m_planets[bestId].isKing = true;
        }
        m_currentKingId = bestId;
    }
}

// ── Cosmic Event ─────────────────────────────────────────────────────────────

void GravityBrawl::triggerCosmicEvent() {
    m_cosmicEventActive = m_cosmicEventDuration;
    m_cosmicEventTimer = m_cosmicEventCooldown;

    spdlog::info("[GravityBrawl] COSMIC EVENT: Black hole is hungry!");
    sendChatFeedback("🚨 THE BLACK HOLE IS HUNGRY! Spam !s to escape! 🚨");
}

void GravityBrawl::updateCosmicEvent(float dt) {
    if (m_cosmicEventActive > 0.0) {
        m_cosmicEventActive -= dt;

        if (m_cosmicEventActive <= 0.0) {
            // Event ended — award survival bonus
            m_cosmicEventActive = 0.0;
            int survivorBonus = 25;
            for (auto& [id, p] : m_planets) {
                if (p.alive) {
                    p.score += survivorBonus;
                    try {
                        is::core::Application::instance().playerDatabase().recordResult(
                            id, p.displayName, "gravity_brawl", survivorBonus, false);
                    } catch (...) {}
                    sf::Vector2f screenPos = worldToScreen(p.body->GetPosition());
                    addFloatingText("+25 SURVIVED!", screenPos, sf::Color(100, 255, 100), 1.5f);
                }
            }
            sendChatFeedback("✅ The black hole calms down. Survivors get +" +
                             std::to_string(survivorBonus) + " points!");
        }
    }
}

// ── Survival Points ──────────────────────────────────────────────────────────

void GravityBrawl::awardSurvivalPoints(double dt) {
    m_survivalAccum += dt;
    if (m_survivalAccum >= 10.0) {
        m_survivalAccum -= 10.0;
        for (auto& [id, p] : m_planets) {
            if (p.alive) {
                p.score += 1;
                try {
                    is::core::Application::instance().playerDatabase().recordResult(
                        id, p.displayName, "gravity_brawl", 1, false);
                } catch (...) {}
            }
        }
    }
}

// ── Particle System ──────────────────────────────────────────────────────────

void GravityBrawl::emitParticles(sf::Vector2f pos, sf::Color color, int count,
                                  float speed, float spread, float life) {
    std::uniform_real_distribution<float> angleDist(0.f, spread);
    std::uniform_real_distribution<float> speedDist(speed * 0.5f, speed);
    std::uniform_real_distribution<float> lifeDist(life * 0.5f, life);
    std::uniform_real_distribution<float> sizeDist(1.5f, 4.0f);

    float baseAngle = (spread < 360.f) ? 0.f : 0.f;

    for (int i = 0; i < count && m_particles.size() < MAX_PARTICLES; ++i) {
        float angle = (angleDist(m_rng) - spread / 2.0f) * (3.14159f / 180.0f);
        float spd = speedDist(m_rng);
        float lt = lifeDist(m_rng);

        Particle part;
        part.position = pos;
        part.velocity = {spd * std::cos(angle), spd * std::sin(angle)};
        part.color = color;
        part.life = lt;
        part.maxLife = lt;
        part.size = sizeDist(m_rng);
        part.drag = 0.97f;
        m_particles.push_back(part);
    }
}

void GravityBrawl::emitExplosion(sf::Vector2f pos, sf::Color color, int count) {
    emitParticles(pos, color, count, 200.f, 360.f, 1.2f);
    // Add some bright white particles
    emitParticles(pos, sf::Color(255, 255, 255, 200), count / 3, 150.f, 360.f, 0.6f);
}

void GravityBrawl::emitTrail(sf::Vector2f pos, sf::Color color) {
    color.a = 120;
    emitParticles(pos, color, 2, 15.f, 360.f, 0.4f);
}

void GravityBrawl::emitSupernovaWave(sf::Vector2f pos, sf::Color color) {
    // Ring of particles expanding outward
    emitParticles(pos, color, 100, 300.f, 360.f, 1.0f);
    emitParticles(pos, sf::Color(255, 255, 255, 200), 50, 250.f, 360.f, 0.8f);
    emitParticles(pos, sf::Color(255, 200, 50, 180), 30, 200.f, 360.f, 1.2f);
}

void GravityBrawl::updateParticles(float dt) {
    for (auto it = m_particles.begin(); it != m_particles.end();) {
        it->life -= dt;
        if (it->life <= 0.0f) {
            it = m_particles.erase(it);
            continue;
        }
        it->velocity.x *= it->drag;
        it->velocity.y *= it->drag;
        it->position.x += it->velocity.x * dt;
        it->position.y += it->velocity.y * dt;

        // Fade alpha
        if (it->fadeAlpha) {
            float frac = it->life / it->maxLife;
            it->color.a = static_cast<sf::Uint8>(frac * 255);
        }
        // Shrink
        if (it->shrink) {
            float frac = it->life / it->maxLife;
            it->size = std::max(0.5f, it->size * (0.5f + 0.5f * frac));
        }
        ++it;
    }
}

// ── Floating Texts ───────────────────────────────────────────────────────────

void GravityBrawl::addFloatingText(const std::string& text, sf::Vector2f pos,
                                    sf::Color color, float duration) {
    m_floatingTexts.push_back({text, pos, color, duration, duration});
}

// ══════════════════════════════════════════════════════════════════════════════
// ██ RENDERING ████████████████████████████████████████████████████████████████
// ══════════════════════════════════════════════════════════════════════════════

void GravityBrawl::render(sf::RenderTarget& target, double alpha) {
    // Background
    m_background.render(target);

    // Orbit guides (faint circle showing safe orbit)
    renderOrbitTrails(target);

    // Black hole
    renderBlackHole(target);

    // Planets
    renderPlanets(target, alpha);

    // Particles on top of planets
    renderParticles(target);

    // Floating texts
    renderFloatingTexts(target);

    // UI overlay
    renderUI(target);
    renderKillFeed(target);
    renderLeaderboard(target);

    // Cosmic event warning
    if (m_cosmicEventActive > 0.0) {
        renderCosmicEventWarning(target);
    }

    // Countdown
    if (m_phase == GamePhase::Countdown) {
        renderCountdown(target);
    }

    // Post-processing
    auto* rt = dynamic_cast<sf::RenderTexture*>(&target);
    if (rt) {
        m_postProcessing.applyVignette(*rt, 0.5f);
        m_postProcessing.applyBloom(*rt, 0.6f, 0.4f);
        m_postProcessing.applyChromaticAberration(*rt, 1.2f);
    }
    m_postProcessing.applyScanlines(target, 0.02f);
}

// ── Black Hole Rendering ─────────────────────────────────────────────────────

void GravityBrawl::renderBlackHole(sf::RenderTarget& target) {
    sf::Vector2f center = worldToScreen(WORLD_CENTER_X, WORLD_CENTER_Y);
    float baseRadius = BLACK_HOLE_RADIUS * PIXELS_PER_METER;

    // Pulsing during cosmic event
    float pulseScale = 1.0f;
    sf::Color ringColor(100, 50, 200);

    if (m_cosmicEventActive > 0.0) {
        pulseScale = 1.0f + 0.15f * std::sin(m_blackHolePulse * 6.0f);
        ringColor = sf::Color(
            static_cast<sf::Uint8>(200 + 55 * std::sin(m_blackHolePulse * 4.0f)),
            30,
            30
        );
    }

    float displayRadius = baseRadius * pulseScale;

    // Accretion disk (outer glow rings)
    for (int i = 5; i >= 0; --i) {
        float r = displayRadius + i * 12.0f;
        sf::CircleShape ring(r);
        ring.setOrigin(r, r);
        ring.setPosition(center);
        sf::Uint8 a = static_cast<sf::Uint8>(30 - i * 4);
        sf::Color c = ringColor;
        c.a = a;
        ring.setFillColor(sf::Color::Transparent);
        ring.setOutlineColor(c);
        ring.setOutlineThickness(3.0f);
        target.draw(ring);
    }

    // Inner glow
    float glowR = displayRadius + 15.0f;
    sf::CircleShape glow(glowR);
    glow.setOrigin(glowR, glowR);
    glow.setPosition(center);
    sf::Color glowColor = ringColor;
    glowColor.a = 40;
    glow.setFillColor(glowColor);
    target.draw(glow);

    // Event horizon (solid black center)
    sf::CircleShape hole(displayRadius);
    hole.setOrigin(displayRadius, displayRadius);
    hole.setPosition(center);
    hole.setFillColor(sf::Color(5, 0, 15));
    hole.setOutlineColor(sf::Color(80, 40, 120, 180));
    hole.setOutlineThickness(2.0f);
    target.draw(hole);

    // Particle drain toward center
    if (static_cast<int>(m_blackHolePulse * 5) % 2 == 0) {
        std::uniform_real_distribution<float> aDist(0.f, 360.f);
        float a = aDist(m_rng) * (3.14159f / 180.0f);
        float dist = (displayRadius + 30.0f);
        sf::Vector2f pPos(center.x + dist * std::cos(a), center.y + dist * std::sin(a));
        sf::Vector2f pVel(-(dist * 0.5f) * std::cos(a), -(dist * 0.5f) * std::sin(a));

        if (m_particles.size() < MAX_PARTICLES) {
            Particle part;
            part.position = pPos;
            part.velocity = pVel;
            part.color = ringColor;
            part.color.a = 150;
            part.life = 0.8f;
            part.maxLife = 0.8f;
            part.size = 2.5f;
            part.drag = 0.95f;
            m_particles.push_back(part);
        }
    }
}

// ── Orbit Trail Guides ───────────────────────────────────────────────────────

void GravityBrawl::renderOrbitTrails(sf::RenderTarget& target) {
    sf::Vector2f center = worldToScreen(WORLD_CENTER_X, WORLD_CENTER_Y);
    float orbitR = ARENA_RADIUS * 0.7f * PIXELS_PER_METER;

    // Faint orbit circle
    sf::CircleShape orbit(orbitR);
    orbit.setOrigin(orbitR, orbitR);
    orbit.setPosition(center);
    orbit.setFillColor(sf::Color::Transparent);
    orbit.setOutlineColor(sf::Color(80, 80, 120, 30));
    orbit.setOutlineThickness(1.5f);
    target.draw(orbit);

    // Danger zone ring (inner)
    float dangerR = BLACK_HOLE_RADIUS * 2.0f * PIXELS_PER_METER;
    sf::CircleShape danger(dangerR);
    danger.setOrigin(dangerR, dangerR);
    danger.setPosition(center);
    danger.setFillColor(sf::Color::Transparent);

    sf::Uint8 dangerAlpha = 20;
    if (m_cosmicEventActive > 0.0) {
        dangerAlpha = static_cast<sf::Uint8>(40 + 30 * std::sin(m_blackHolePulse * 4.0f));
    }
    danger.setOutlineColor(sf::Color(200, 50, 50, dangerAlpha));
    danger.setOutlineThickness(1.5f);
    target.draw(danger);
}

// ── Planet Rendering ─────────────────────────────────────────────────────────

void GravityBrawl::renderPlanets(sf::RenderTarget& target, double alpha) {
    for (const auto& [id, p] : m_planets) {
        if (!p.alive) continue;

        // Interpolate position
        float a = static_cast<float>(alpha);
        sf::Vector2f worldPos(
            p.prevPosition.x + (p.renderPosition.x - p.prevPosition.x) * a,
            p.prevPosition.y + (p.renderPosition.y - p.prevPosition.y) * a);
        sf::Vector2f screenPos = worldToScreen(worldPos.x, worldPos.y);

        renderPlanet(target, p, screenPos);
    }
}

void GravityBrawl::renderPlanet(sf::RenderTarget& target, const Planet& p, sf::Vector2f screenPos) {
    float pixelRadius = p.getVisualRadius() * PIXELS_PER_METER;
    sf::Color baseColor = getTierColor(p.tier);
    sf::Color glowColor = getTierGlowColor(p.tier);

    // Hit flash
    if (p.hitFlashTimer > 0.0f) {
        baseColor = sf::Color::White;
    }

    // Supernova shockwave ring
    if (p.supernovaTimer > 0.0f) {
        float progress = 1.0f - p.supernovaTimer / 0.5f;
        float ringR = pixelRadius + progress * 200.0f;
        sf::CircleShape ring(ringR);
        ring.setOrigin(ringR, ringR);
        ring.setPosition(screenPos);
        sf::Uint8 ringA = static_cast<sf::Uint8>((1.0f - progress) * 200);
        ring.setFillColor(sf::Color::Transparent);
        ring.setOutlineColor(sf::Color(255, 255, 200, ringA));
        ring.setOutlineThickness(3.0f + (1.0f - progress) * 5.0f);
        target.draw(ring);
    }

    // Outer glow (larger for higher tiers)
    float glowMul = 1.0f;
    switch (p.tier) {
    case PlanetTier::IcePlanet: glowMul = 1.5f; break;
    case PlanetTier::GasGiant:  glowMul = 2.0f; break;
    case PlanetTier::Star:      glowMul = 2.5f + 0.3f * std::sin(p.glowPulse); break;
    default: break;
    }

    float glowR = pixelRadius * glowMul;
    sf::CircleShape glow(glowR);
    glow.setOrigin(glowR, glowR);
    glow.setPosition(screenPos);
    glow.setFillColor(glowColor);
    target.draw(glow);

    // Main planet body
    sf::CircleShape body(pixelRadius);
    body.setOrigin(pixelRadius, pixelRadius);
    body.setPosition(screenPos);
    body.setFillColor(baseColor);

    // Outline based on tier
    float outlineThickness = 1.5f;
    sf::Color outlineColor = baseColor;
    outlineColor.r = std::min(255, outlineColor.r + 40);
    outlineColor.g = std::min(255, outlineColor.g + 40);
    outlineColor.b = std::min(255, outlineColor.b + 40);

    if (p.isKing) {
        outlineColor = sf::Color(255, 215, 0);
        outlineThickness = 3.0f;
    }

    body.setOutlineColor(outlineColor);
    body.setOutlineThickness(outlineThickness);
    target.draw(body);

    // Surface details based on tier
    if (p.tier == PlanetTier::GasGiant || p.tier == PlanetTier::Star) {
        // Bands (horizontal stripes)
        for (int i = -2; i <= 2; ++i) {
            float y = screenPos.y + i * pixelRadius * 0.3f;
            float halfW = std::sqrt(std::max(0.0f,
                pixelRadius * pixelRadius - (i * pixelRadius * 0.3f) * (i * pixelRadius * 0.3f)));
            sf::RectangleShape band(sf::Vector2f(halfW * 1.6f, 2.0f));
            band.setOrigin(halfW * 0.8f, 1.0f);
            band.setPosition(screenPos.x, y);
            sf::Color bandColor = baseColor;
            bandColor.r = static_cast<sf::Uint8>(std::max(0, bandColor.r - 30));
            bandColor.a = 80;
            band.setFillColor(bandColor);
            target.draw(band);
        }
    }

    // King crown
    if (p.isKing) {
        renderCrown(target, screenPos, pixelRadius);
    }

    // Name label
    if (m_fontLoaded) {
        sf::Text nameText;
        nameText.setFont(m_font);
        nameText.setString(p.displayName);
        nameText.setCharacterSize(fs(11));
        nameText.setFillColor(sf::Color(255, 255, 255, 220));
        nameText.setOutlineColor(sf::Color(0, 0, 0, 180));
        nameText.setOutlineThickness(1.0f);
        sf::FloatRect bounds = nameText.getLocalBounds();
        nameText.setOrigin(bounds.left + bounds.width / 2.0f, bounds.top);
        nameText.setPosition(screenPos.x, screenPos.y + pixelRadius + 6.0f);
        target.draw(nameText);

        // Score below name
        sf::Text scoreText;
        scoreText.setFont(m_font);
        scoreText.setString(std::to_string(p.score));
        scoreText.setCharacterSize(fs(9));
        scoreText.setFillColor(sf::Color(200, 200, 100, 180));
        scoreText.setOutlineColor(sf::Color(0, 0, 0, 150));
        scoreText.setOutlineThickness(1.0f);
        sf::FloatRect sBounds = scoreText.getLocalBounds();
        scoreText.setOrigin(sBounds.left + sBounds.width / 2.0f, sBounds.top);
        scoreText.setPosition(screenPos.x, screenPos.y + pixelRadius + 6.0f + fs(12));
        target.draw(scoreText);
    }
}

// ── Crown Rendering ──────────────────────────────────────────────────────────

void GravityBrawl::renderCrown(sf::RenderTarget& target, sf::Vector2f pos, float radius) {
    // Simple crown above the planet
    float crownY = pos.y - radius - 12.0f;
    float crownW = radius * 1.2f;

    // Base
    sf::RectangleShape base(sf::Vector2f(crownW, 5.0f));
    base.setOrigin(crownW / 2.0f, 5.0f);
    base.setPosition(pos.x, crownY);
    base.setFillColor(sf::Color(255, 215, 0));
    target.draw(base);

    // Points of the crown (3 triangles)
    for (int i = -1; i <= 1; ++i) {
        sf::ConvexShape point(3);
        float px = pos.x + i * crownW * 0.3f;
        point.setPoint(0, sf::Vector2f(px - 4.0f, crownY - 5.0f));
        point.setPoint(1, sf::Vector2f(px + 4.0f, crownY - 5.0f));
        point.setPoint(2, sf::Vector2f(px, crownY - 14.0f));
        point.setFillColor(sf::Color(255, 215, 0));
        target.draw(point);
    }

    // Gems (small circles)
    for (int i = -1; i <= 1; ++i) {
        float gx = pos.x + i * crownW * 0.3f;
        sf::CircleShape gem(2.5f);
        gem.setOrigin(2.5f, 2.5f);
        gem.setPosition(gx, crownY - 10.0f);
        sf::Color gemColor = (i == 0) ? sf::Color(255, 50, 50) : sf::Color(100, 200, 255);
        gem.setFillColor(gemColor);
        target.draw(gem);
    }
}

// ── Particle Rendering ───────────────────────────────────────────────────────

void GravityBrawl::renderParticles(sf::RenderTarget& target) {
    for (const auto& p : m_particles) {
        sf::CircleShape shape(p.size);
        shape.setOrigin(p.size, p.size);
        shape.setPosition(p.position);
        shape.setFillColor(p.color);
        target.draw(shape);
    }
}

// ── Kill Feed ────────────────────────────────────────────────────────────────

void GravityBrawl::renderKillFeed(sf::RenderTarget& target) {
    if (!m_fontLoaded || m_killFeed.empty()) return;

    float y = 100.0f;
    for (const auto& entry : m_killFeed) {
        float alpha = std::min(1.0f, static_cast<float>(entry.timeRemaining / 1.0));
        sf::Uint8 a = static_cast<sf::Uint8>(alpha * 220);

        std::string text = entry.killer + " ▸ " + entry.victim;
        if (entry.wasBounty) text += " 👑";

        sf::Text feedText;
        feedText.setFont(m_font);
        feedText.setString(text);
        feedText.setCharacterSize(fs(11));
        feedText.setFillColor(entry.wasBounty ? sf::Color(255, 215, 0, a) : sf::Color(255, 200, 200, a));
        feedText.setOutlineColor(sf::Color(0, 0, 0, a));
        feedText.setOutlineThickness(1.0f);

        sf::FloatRect bounds = feedText.getLocalBounds();
        feedText.setPosition(SCREEN_W - bounds.width - 15.0f, y);
        target.draw(feedText);

        y += fs(14);
    }
}

// ── Leaderboard ──────────────────────────────────────────────────────────────

void GravityBrawl::renderLeaderboard(sf::RenderTarget& target) {
    if (!m_fontLoaded) return;

    // Sort players by score
    std::vector<const Planet*> sorted;
    for (const auto& [id, p] : m_planets) {
        sorted.push_back(&p);
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const Planet* a, const Planet* b) { return a->score > b->score; });

    float x = 15.0f;
    float y = 100.0f;

    // Title
    sf::Text title;
    title.setFont(m_font);
    title.setString("LEADERBOARD");
    title.setCharacterSize(fs(12));
    title.setFillColor(sf::Color(255, 215, 0, 200));
    title.setOutlineColor(sf::Color(0, 0, 0, 180));
    title.setOutlineThickness(1.0f);
    title.setPosition(x, y);
    target.draw(title);
    y += fs(16);

    int shown = 0;
    for (const auto* p : sorted) {
        if (shown >= 10) break;

        std::string prefix = p->isKing ? "👑 " : "";
        std::string status = p->alive ? "" : " 💀";
        std::string text = prefix + p->displayName + status +
                          " - " + std::to_string(p->score) +
                          " (" + std::to_string(p->kills) + "K)";

        sf::Text entry;
        entry.setFont(m_font);
        entry.setString(text);
        entry.setCharacterSize(fs(10));

        sf::Color color = p->alive ? sf::Color(220, 220, 220, 200) : sf::Color(120, 120, 120, 150);
        if (p->isKing) color = sf::Color(255, 215, 0, 220);

        entry.setFillColor(color);
        entry.setOutlineColor(sf::Color(0, 0, 0, 150));
        entry.setOutlineThickness(1.0f);
        entry.setPosition(x, y);
        target.draw(entry);

        y += fs(13);
        shown++;
    }
}

// ── Floating Texts ───────────────────────────────────────────────────────────

void GravityBrawl::renderFloatingTexts(sf::RenderTarget& target) {
    if (!m_fontLoaded) return;

    for (const auto& ft : m_floatingTexts) {
        float alpha = std::min(1.0f, ft.timer / (ft.maxTime * 0.3f));
        sf::Uint8 a = static_cast<sf::Uint8>(alpha * 255);

        sf::Text text;
        text.setFont(m_font);
        text.setString(ft.text);
        text.setCharacterSize(fs(16));
        sf::Color c = ft.color;
        c.a = a;
        text.setFillColor(c);
        text.setOutlineColor(sf::Color(0, 0, 0, a));
        text.setOutlineThickness(2.0f);

        sf::FloatRect bounds = text.getLocalBounds();
        text.setOrigin(bounds.left + bounds.width / 2.0f, bounds.top + bounds.height / 2.0f);
        text.setPosition(ft.position);

        // Scale up as it fades
        float scale = 1.0f + (1.0f - alpha) * 0.5f;
        text.setScale(scale, scale);

        target.draw(text);
    }
}

// ── UI Overlay ───────────────────────────────────────────────────────────────

void GravityBrawl::renderUI(sf::RenderTarget& target) {
    if (!m_fontLoaded) return;

    // Game title
    sf::Text titleText;
    titleText.setFont(m_font);
    titleText.setString("GRAVITY BRAWL");
    titleText.setCharacterSize(fs(22));
    titleText.setFillColor(sf::Color(200, 180, 255, 200));
    titleText.setOutlineColor(sf::Color(0, 0, 0, 200));
    titleText.setOutlineThickness(2.0f);
    sf::FloatRect tb = titleText.getLocalBounds();
    titleText.setOrigin(tb.left + tb.width / 2.0f, 0.0f);
    titleText.setPosition(SCREEN_W / 2.0f, 15.0f);
    target.draw(titleText);

    // Player count and timer
    int aliveCount = 0, totalCount = 0;
    for (const auto& [_, p] : m_planets) {
        totalCount++;
        if (p.alive) aliveCount++;
    }

    std::string infoStr;
    if (m_phase == GamePhase::Lobby) {
        infoStr = "LOBBY - " + std::to_string(aliveCount) + " planets | Starting in " +
                  std::to_string(static_cast<int>(m_lobbyTimer)) + "s";
    } else if (m_phase == GamePhase::Playing) {
        int remaining = static_cast<int>(m_gameDuration - m_gameTimer);
        infoStr = std::to_string(aliveCount) + "/" + std::to_string(totalCount) +
                  " alive | " + std::to_string(remaining) + "s left";
    } else if (m_phase == GamePhase::GameOver) {
        infoStr = "GAME OVER";
    }

    sf::Text infoText;
    infoText.setFont(m_font);
    infoText.setString(infoStr);
    infoText.setCharacterSize(fs(13));
    infoText.setFillColor(sf::Color(180, 180, 200, 200));
    infoText.setOutlineColor(sf::Color(0, 0, 0, 180));
    infoText.setOutlineThickness(1.0f);
    sf::FloatRect ib = infoText.getLocalBounds();
    infoText.setOrigin(ib.left + ib.width / 2.0f, 0.0f);
    infoText.setPosition(SCREEN_W / 2.0f, 45.0f);
    target.draw(infoText);

    // Join hint
    if (m_phase == GamePhase::Lobby || m_phase == GamePhase::Playing) {
        sf::Text joinHint;
        joinHint.setFont(m_font);
        joinHint.setString("Type !join to play | !s to smash");
        joinHint.setCharacterSize(fs(11));
        joinHint.setFillColor(sf::Color(150, 150, 180, 160));
        joinHint.setOutlineColor(sf::Color(0, 0, 0, 120));
        joinHint.setOutlineThickness(1.0f);
        sf::FloatRect jb = joinHint.getLocalBounds();
        joinHint.setOrigin(jb.left + jb.width / 2.0f, 0.0f);
        joinHint.setPosition(SCREEN_W / 2.0f, SCREEN_H - 40.0f);
        target.draw(joinHint);
    }

    // Cosmic event warning bar
    if (m_cosmicEventActive <= 0.0 && m_cosmicEventTimer < 10.0 && m_phase == GamePhase::Playing) {
        sf::Text warningText;
        warningText.setFont(m_font);
        warningText.setString("⚠ BLACK HOLE HUNGERS IN " +
                              std::to_string(static_cast<int>(m_cosmicEventTimer)) + "s");
        warningText.setCharacterSize(fs(12));
        warningText.setFillColor(sf::Color(255, 100, 100, 200));
        warningText.setOutlineColor(sf::Color(0, 0, 0, 200));
        warningText.setOutlineThickness(1.0f);
        sf::FloatRect wb = warningText.getLocalBounds();
        warningText.setOrigin(wb.left + wb.width / 2.0f, 0.0f);
        warningText.setPosition(SCREEN_W / 2.0f, 70.0f);
        target.draw(warningText);
    }
}

// ── Cosmic Event Warning ─────────────────────────────────────────────────────

void GravityBrawl::renderCosmicEventWarning(sf::RenderTarget& target) {
    if (!m_fontLoaded) return;

    // Flashing red overlay at screen edges
    float pulse = std::abs(std::sin(m_blackHolePulse * 3.0f));
    sf::Uint8 a = static_cast<sf::Uint8>(pulse * 40);

    sf::RectangleShape overlay(sf::Vector2f(SCREEN_W, SCREEN_H));
    overlay.setFillColor(sf::Color(255, 0, 0, a));
    target.draw(overlay);

    // Warning text
    sf::Text warning;
    warning.setFont(m_font);
    warning.setString("🚨 BLACK HOLE SURGE! SPAM !s TO ESCAPE! 🚨");
    warning.setCharacterSize(fs(16));
    warning.setFillColor(sf::Color(255, 50, 50, static_cast<sf::Uint8>(180 + pulse * 75)));
    warning.setOutlineColor(sf::Color(0, 0, 0, 220));
    warning.setOutlineThickness(2.0f);
    sf::FloatRect wb = warning.getLocalBounds();
    warning.setOrigin(wb.left + wb.width / 2.0f, wb.top + wb.height / 2.0f);
    warning.setPosition(SCREEN_W / 2.0f, 75.0f);

    float scale = 1.0f + pulse * 0.1f;
    warning.setScale(scale, scale);
    target.draw(warning);

    // Timer
    int remaining = static_cast<int>(m_cosmicEventActive);
    sf::Text timerText;
    timerText.setFont(m_font);
    timerText.setString(std::to_string(remaining) + "s");
    timerText.setCharacterSize(fs(24));
    timerText.setFillColor(sf::Color(255, 80, 80, 220));
    timerText.setOutlineColor(sf::Color(0, 0, 0, 220));
    timerText.setOutlineThickness(2.0f);
    sf::FloatRect tb = timerText.getLocalBounds();
    timerText.setOrigin(tb.left + tb.width / 2.0f, tb.top + tb.height / 2.0f);

    // Position near black hole
    sf::Vector2f center = worldToScreen(WORLD_CENTER_X, WORLD_CENTER_Y);
    timerText.setPosition(center.x, center.y - BLACK_HOLE_RADIUS * PIXELS_PER_METER - 30.0f);
    target.draw(timerText);
}

// ── Countdown ────────────────────────────────────────────────────────────────

void GravityBrawl::renderCountdown(sf::RenderTarget& target) {
    if (!m_fontLoaded) return;

    int count = static_cast<int>(std::ceil(m_countdownTimer));
    if (count <= 0) return;

    sf::Text text;
    text.setFont(m_font);
    text.setString(std::to_string(count));
    text.setCharacterSize(fs(80));
    text.setFillColor(sf::Color(255, 255, 255, 230));
    text.setOutlineColor(sf::Color(100, 50, 200, 200));
    text.setOutlineThickness(4.0f);
    sf::FloatRect bounds = text.getLocalBounds();
    text.setOrigin(bounds.left + bounds.width / 2.0f, bounds.top + bounds.height / 2.0f);
    text.setPosition(SCREEN_W / 2.0f, SCREEN_H * 0.3f);

    float frac = static_cast<float>(m_countdownTimer - static_cast<int>(m_countdownTimer));
    float scale = 1.0f + frac * 0.5f;
    text.setScale(scale, scale);
    target.draw(text);
}

// ── IGame Interface ──────────────────────────────────────────────────────────

bool GravityBrawl::isRoundComplete() const {
    return m_phase == GamePhase::GameOver || m_phase == GamePhase::Lobby;
}

bool GravityBrawl::isGameOver() const {
    return m_phase == GamePhase::GameOver;
}

nlohmann::json GravityBrawl::getState() const {
    nlohmann::json state;
    state["phase"] = [&]() {
        switch (m_phase) {
        case GamePhase::Lobby:     return "lobby";
        case GamePhase::Countdown: return "countdown";
        case GamePhase::Playing:   return "playing";
        case GamePhase::GameOver:  return "game_over";
        }
        return "unknown";
    }();

    state["gameTimer"] = m_gameTimer;
    state["gameDuration"] = m_gameDuration;
    state["cosmicEventActive"] = m_cosmicEventActive > 0.0;
    state["cosmicEventTimer"] = m_cosmicEventTimer;

    int alive = 0, total = 0;
    nlohmann::json players = nlohmann::json::array();
    for (const auto& [id, p] : m_planets) {
        total++;
        if (p.alive) alive++;
        players.push_back({
            {"id", id},
            {"name", p.displayName},
            {"alive", p.alive},
            {"kills", p.kills},
            {"score", p.score},
            {"tier", static_cast<int>(p.tier)},
            {"isKing", p.isKing}
        });
    }
    state["alivePlayers"] = alive;
    state["totalPlayers"] = total;
    state["players"] = players;
    state["currentKing"] = m_currentKingId;

    return state;
}

nlohmann::json GravityBrawl::getCommands() const {
    return nlohmann::json::array({
        {{"command", "!join"},  {"aliases", nlohmann::json::array({"!play"})},
         {"description", "Enter the orbit as a planet"}},
        {{"command", "!smash"}, {"aliases", nlohmann::json::array({"!s"})},
         {"description", "Dash forward / smash nearby planets. Spam 5x fast for SUPERNOVA!"}}
    });
}

} // namespace is::games::gravity_brawl
