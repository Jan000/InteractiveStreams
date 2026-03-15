#include "games/country_elimination/CountryElimination.h"
#include "games/GameRegistry.h"
#include "core/Application.h"
#include "core/PlayerDatabase.h"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <sstream>

namespace is::games::country_elimination {

// Auto-register this game
REGISTER_GAME(CountryElimination, "country_elimination");

// ═════════════════════════════════════════════════════════════════════════════
// ██ CONSTRUCTION ██████████████████████████████████████████████████████████████
// ═════════════════════════════════════════════════════════════════════════════

CountryElimination::CountryElimination()
    : m_rng(std::random_device{}())
{
}

CountryElimination::~CountryElimination() {
    shutdown();
}

// ═════════════════════════════════════════════════════════════════════════════
// ██ LIFECYCLE ████████████████████████████████████████████████████████████████
// ═════════════════════════════════════════════════════════════════════════════

void CountryElimination::initialize() {
    spdlog::info("[CountryElimination] Initializing...");

    // Register configurable text elements
    if (m_textElements.empty()) {
        registerTextElement("title",         "Game Title",         50.0f,  1.5f, 28, TextAlign::Center, true, "#FFFFFFDD");
        registerTextElement("phase",         "Phase Indicator",    50.0f,  4.0f, 22, TextAlign::Center, true, "#CCCCCCCC");
        registerTextElement("join_hint",     "Join Hint",          50.0f, 96.0f, 20, TextAlign::Center, true, "#AAAAAA99");
        registerTextElement("countdown",     "Countdown Number",   50.0f, 50.0f, 80, TextAlign::Center, false);
        registerTextElement("winner_text",   "Winner Overlay",     50.0f, 45.0f, 48, TextAlign::Center, false, "#FFD700FF");
        registerTextElement("winner_label",  "Winner Country",     50.0f, 52.0f, 32, TextAlign::Center, false, "#FFFFFFDD");
        registerTextElement("elim_feed",     "Elimination Feed",   98.0f,  6.0f, 16, TextAlign::Right,  true, "#FF6666CC");
        registerTextElement("player_count",  "Player Count",       50.0f,  6.5f, 18, TextAlign::Center, true, "#CCCCCCAA");
        registerTextElement("sub_info",      "Sub Reward Info",    50.0f, 98.0f,  9, TextAlign::Center, true, "#AAAACC99");
    }

    // Create Box2D world with downward gravity (for eliminated balls)
    m_world = new b2World(b2Vec2(0.0f, 15.0f));

    // Create the static floor at bottom of screen
    {
        b2BodyDef floorDef;
        floorDef.type = b2_staticBody;
        floorDef.position.Set(ARENA_CENTER_X, FLOOR_Y);
        m_floorBody = m_world->CreateBody(&floorDef);

        b2PolygonShape floorShape;
        floorShape.SetAsBox(SCREEN_W / PIXELS_PER_METER, 0.5f);

        b2FixtureDef floorFix;
        floorFix.shape = &floorShape;
        floorFix.restitution = 0.3f;
        floorFix.friction = 0.8f;
        m_floorBody->CreateFixture(&floorFix);
    }

    // Create arena boundary
    createArenaBody();

    // Initialize post-processing
    m_postProcessing.initialize(static_cast<int>(SCREEN_W), static_cast<int>(SCREEN_H));

    // Load font
    if (m_font.loadFromFile("assets/fonts/JetBrainsMono-Regular.ttf")) {
        m_fontLoaded = true;
        spdlog::info("[CountryElimination] Font loaded.");
    } else {
        spdlog::warn("[CountryElimination] Font not found, text rendering disabled.");
    }

    m_phase = GamePhase::Lobby;
    m_lobbyTimer = 0.0;
    m_roundNumber = 0;

    spdlog::info("[CountryElimination] Initialized.");
}

void CountryElimination::shutdown() {
    spdlog::info("[CountryElimination] Shutting down...");

    for (auto& [id, p] : m_players) {
        if (p.body && m_world) {
            m_world->DestroyBody(p.body);
            p.body = nullptr;
        }
    }
    m_players.clear();
    m_eliminationFeed.clear();

    destroyArenaBody();

    if (m_floorBody && m_world) {
        m_world->DestroyBody(m_floorBody);
        m_floorBody = nullptr;
    }

    delete m_world;
    m_world = nullptr;
}

// ═════════════════════════════════════════════════════════════════════════════
// ██ ARENA CREATION ███████████████████████████████████████████████████████████
// ═════════════════════════════════════════════════════════════════════════════

void CountryElimination::createArenaBody() {
    if (!m_world) return;
    destroyArenaBody();

    b2BodyDef bodyDef;
    bodyDef.type = b2_kinematicBody;
    bodyDef.position.Set(ARENA_CENTER_X, ARENA_CENTER_Y);
    bodyDef.angle = 0.0f;
    m_arenaBody = m_world->CreateBody(&bodyDef);

    // Build wall segments in a circle, skipping the gap
    const float angleStep = 2.0f * b2_pi / WALL_SEGMENTS;
    const float segLen = ARENA_RADIUS * angleStep;  // approximate chord length

    for (int i = 0; i < WALL_SEGMENTS; ++i) {
        float angle = i * angleStep;

        // Skip segments that fall within the gap region
        // Gap centered at angle 0 (right side of circle)
        float normAngle = angle;
        if (normAngle > b2_pi) normAngle -= 2.0f * b2_pi;
        if (std::abs(normAngle) < GAP_HALF_ANGLE) continue;

        float midX = ARENA_RADIUS * std::cos(angle);
        float midY = ARENA_RADIUS * std::sin(angle);

        b2PolygonShape seg;
        seg.SetAsBox(segLen * 0.55f, WALL_THICKNESS * 0.5f,
                     b2Vec2(midX, midY), angle + b2_pi / 2.0f);

        b2FixtureDef fix;
        fix.shape = &seg;
        fix.restitution = 1.0f;
        fix.friction = 0.0f;
        m_arenaBody->CreateFixture(&fix);
    }

    m_arenaAngle = 0.0f;
    spdlog::info("[CountryElimination] Arena body created with {} segments.", WALL_SEGMENTS);
}

void CountryElimination::destroyArenaBody() {
    if (m_arenaBody && m_world) {
        m_world->DestroyBody(m_arenaBody);
        m_arenaBody = nullptr;
    }
}

b2Body* CountryElimination::createPlayerBody(float x, float y, float radius) {
    b2BodyDef bd;
    bd.type = b2_dynamicBody;
    bd.position.Set(x, y);
    bd.bullet = true;             // CCD for fast-moving balls
    bd.linearDamping = 0.05f;     // very slight damping
    bd.gravityScale = 0.0f;       // no gravity while inside arena

    b2Body* body = m_world->CreateBody(&bd);

    b2CircleShape circle;
    circle.m_radius = radius;

    b2FixtureDef fix;
    fix.shape = &circle;
    fix.density = 1.0f;
    fix.restitution = m_restitution;
    fix.friction = 0.0f;

    body->CreateFixture(&fix);
    return body;
}

// ═════════════════════════════════════════════════════════════════════════════
// ██ CHAT HANDLING ████████████████████████████████████████████████████████████
// ═════════════════════════════════════════════════════════════════════════════

void CountryElimination::onChatMessage(const platform::ChatMessage& msg) {
    handleStreamEvent(msg);

    if (msg.text.empty()) return;

    std::istringstream iss(msg.text);
    std::string cmd;
    iss >> cmd;

    // Strip optional leading '!'
    if (!cmd.empty() && cmd[0] == '!') cmd = cmd.substr(1);
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);

    if (cmd == "join" || cmd == "play") {
        std::string label;
        std::getline(iss >> std::ws, label);
        if (label.empty()) label = "?";
        // Trim trailing whitespace / \r
        while (!label.empty() && (label.back() == '\r' || label.back() == '\n' || label.back() == ' '))
            label.pop_back();
        if (label.size() > 16) label = label.substr(0, 16);
        cmdJoin(msg.userId, msg.displayName, label);
    }
}

void CountryElimination::cmdJoin(const std::string& userId, const std::string& displayName,
                                  const std::string& label) {
    if (m_phase != GamePhase::Lobby && m_phase != GamePhase::Battle) return;
    if (m_players.count(userId)) return; // already joined

    // Spawn at random position near arena center
    std::uniform_real_distribution<float> angleDist(0.0f, 2.0f * b2_pi);
    std::uniform_real_distribution<float> radiusDist(0.0f, ARENA_RADIUS * 0.6f);
    float a = angleDist(m_rng);
    float r = radiusDist(m_rng);
    float spawnX = ARENA_CENTER_X + r * std::cos(a);
    float spawnY = ARENA_CENTER_Y + r * std::sin(a);

    Player p;
    p.userId = userId;
    p.displayName = displayName;
    p.label = label;
    p.color = generateColor();
    p.radiusM = BALL_RADIUS;
    p.alive = true;
    p.eliminated = false;
    p.body = createPlayerBody(spawnX, spawnY, BALL_RADIUS);

    // Give random initial velocity
    if (m_phase == GamePhase::Battle) {
        float va = angleDist(m_rng);
        p.body->SetLinearVelocity(b2Vec2(m_initialSpeed * std::cos(va),
                                          m_initialSpeed * std::sin(va)));
    }

    p.prevPos = p.body->GetPosition();
    p.currPos = p.prevPos;

    m_players[userId] = std::move(p);
    sendChatFeedback(displayName + " [" + label + "] joined!");

    spdlog::info("[CountryElimination] {} joined as '{}'.", displayName, label);
}

void CountryElimination::handleStreamEvent(const platform::ChatMessage& msg) {
    if (msg.eventType.empty()) return;
    if (m_phase != GamePhase::Battle && m_phase != GamePhase::Lobby) return;

    // Auto-join if not in game
    if (!m_players.count(msg.userId)) {
        cmdJoin(msg.userId, msg.displayName, msg.displayName.substr(0, 2));
        if (!m_players.count(msg.userId)) return;
    }

    Player& p = m_players[msg.userId];

    if (msg.eventType == "yt_subscribe" || msg.eventType == "twitch_sub") {
        // Shield + score
        p.hasShield = true;
        p.shieldTimer = 15.0f;
        p.score += 300;
        sendChatFeedback("🛡️ " + p.displayName + " got a shield from subscribing! +300");
    } else if ((msg.eventType == "yt_superchat" || msg.eventType == "twitch_bits") && msg.amount > 100) {
        // Bigger ball + shield
        p.hasShield = true;
        p.shieldTimer = 20.0f;
        p.score += 500;

        // Increase radius (make heavier)
        if (p.body) {
            float newRadius = BALL_RADIUS * 1.5f;
            p.radiusM = newRadius;

            // Recreate fixture with larger radius
            b2Fixture* fix = p.body->GetFixtureList();
            if (fix) {
                p.body->DestroyFixture(fix);
                b2CircleShape circle;
                circle.m_radius = newRadius;
                b2FixtureDef fd;
                fd.shape = &circle;
                fd.density = 1.5f;
                fd.restitution = m_restitution;
                fd.friction = 0.0f;
                p.body->CreateFixture(&fd);
            }
        }
        sendChatFeedback("⭐ " + p.displayName + " powered up! Bigger & shielded! +500");
    } else if (msg.eventType == "twitch_channel_points") {
        p.hasShield = true;
        p.shieldTimer = 10.0f;
        p.score += 100;
        sendChatFeedback("✨ " + p.displayName + " got a shield! +100");
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// ██ GAME LOGIC ███████████████████████████████████████████████████████████████
// ═════════════════════════════════════════════════════════════════════════════

void CountryElimination::startCountdown() {
    if (m_phase != GamePhase::Lobby) return;

    int playerCount = 0;
    for (const auto& [_, p] : m_players) {
        if (p.alive) playerCount++;
    }
    if (playerCount < m_minPlayers) return;

    m_phase = GamePhase::Countdown;
    m_countdownTimer = 3.0;
    spdlog::info("[CountryElimination] Countdown started with {} players.", playerCount);
}

void CountryElimination::startBattle() {
    m_phase = GamePhase::Battle;
    m_roundTimer = 0.0;
    m_roundNumber++;

    // Set arena rotation
    if (m_arenaBody) {
        m_arenaBody->SetAngularVelocity(m_arenaAngularVel);
    }

    // Give all players a random initial push
    std::uniform_real_distribution<float> angleDist(0.0f, 2.0f * b2_pi);
    for (auto& [id, p] : m_players) {
        if (!p.alive || !p.body) continue;
        float a = angleDist(m_rng);
        p.body->SetLinearVelocity(b2Vec2(m_initialSpeed * std::cos(a),
                                          m_initialSpeed * std::sin(a)));
    }

    spdlog::info("[CountryElimination] Battle started! Round {}.", m_roundNumber);
}

void CountryElimination::checkEliminations() {
    float arenaR2 = (ARENA_RADIUS + WALL_THICKNESS) * (ARENA_RADIUS + WALL_THICKNESS);

    for (auto& [id, p] : m_players) {
        if (!p.alive || !p.body) continue;

        b2Vec2 pos = p.body->GetPosition();
        float dx = pos.x - ARENA_CENTER_X;
        float dy = pos.y - ARENA_CENTER_Y;
        float dist2 = dx * dx + dy * dy;

        // Check if ball has escaped the arena
        if (dist2 > arenaR2) {
            if (p.hasShield) {
                // Shield absorbs one escape — push ball back toward center
                p.hasShield = false;
                p.shieldTimer = 0.0f;
                b2Vec2 dir(-dx, -dy);
                float len = dir.Length();
                if (len > 0.01f) dir *= (m_initialSpeed * 2.0f / len);
                p.body->SetLinearVelocity(dir);
                // Reset position to just inside
                float pullback = ARENA_RADIUS * 0.8f / std::sqrt(dist2);
                p.body->SetTransform(
                    b2Vec2(ARENA_CENTER_X + dx * pullback, ARENA_CENTER_Y + dy * pullback),
                    p.body->GetAngle());
                sendChatFeedback("🛡️ " + p.displayName + "'s shield saved them!");
                continue;
            }

            p.alive = false;
            // Enable gravity so ball falls to the floor
            p.body->SetGravityScale(1.0f);
            // Give slight outward push
            b2Vec2 vel = p.body->GetLinearVelocity();
            p.body->SetLinearVelocity(b2Vec2(vel.x * 0.3f, 2.0f));

            m_eliminationFeed.push_front({p.displayName, p.label, 4.0});
            if (m_eliminationFeed.size() > 10) m_eliminationFeed.pop_back();

            sendChatFeedback("💀 " + p.displayName + " [" + p.label + "] eliminated!");
            spdlog::info("[CountryElimination] {} eliminated.", p.displayName);

            // Record participation point
            try {
                is::core::Application::instance().playerDatabase().recordResult(
                    p.userId, p.displayName, "country_elimination", 1, false);
            } catch (...) {}
        }
    }

    // Mark floor-bound balls as fully eliminated
    for (auto& [id, p] : m_players) {
        if (p.alive || p.eliminated || !p.body) continue;
        b2Vec2 pos = p.body->GetPosition();
        if (pos.y > FLOOR_Y - 1.0f) {
            p.eliminated = true;
            p.body->SetLinearVelocity(b2Vec2(0.0f, 0.0f));
            p.body->SetType(b2_staticBody); // freeze on floor
        }
    }
}

void CountryElimination::checkRoundEnd() {
    int aliveCount = 0;
    std::string lastAlive;

    for (const auto& [id, p] : m_players) {
        if (p.alive) {
            aliveCount++;
            lastAlive = id;
        }
    }

    if (aliveCount <= 1 && m_players.size() >= 2) {
        if (aliveCount == 1) {
            m_winnerId = lastAlive;
        } else {
            m_winnerId.clear(); // draw
        }
        endRound();
    }
}

void CountryElimination::endRound() {
    m_phase = GamePhase::RoundEnd;
    m_roundEndTimer = m_roundEndDuration;

    // Stop arena rotation
    if (m_arenaBody) {
        m_arenaBody->SetAngularVelocity(0.0f);
    }

    if (!m_winnerId.empty() && m_players.count(m_winnerId)) {
        Player& winner = m_players[m_winnerId];
        winner.score += 100;
        sendChatFeedback("🏆 " + winner.displayName + " [" + winner.label + "] wins the round! +100");
        spdlog::info("[CountryElimination] Winner: {} [{}].", winner.displayName, winner.label);

        try {
            is::core::Application::instance().playerDatabase().recordResult(
                winner.userId, winner.displayName, "country_elimination", 100, true);
        } catch (...) {}
    } else {
        sendChatFeedback("It's a draw! No one wins this round.");
    }
}

void CountryElimination::resetForNextRound() {
    // Destroy all player bodies
    for (auto& [id, p] : m_players) {
        if (p.body && m_world) {
            m_world->DestroyBody(p.body);
            p.body = nullptr;
        }
    }
    m_players.clear();
    m_eliminationFeed.clear();
    m_winnerId.clear();

    // Recreate arena (resets rotation)
    createArenaBody();

    m_phase = GamePhase::Lobby;
    m_lobbyTimer = 0.0;
    spdlog::info("[CountryElimination] Reset for next round.");
}

// ═════════════════════════════════════════════════════════════════════════════
// ██ UPDATE ███████████████████████████████████████████████████████████████████
// ═════════════════════════════════════════════════════════════════════════════

void CountryElimination::update(double dt) {
    float fdt = static_cast<float>(dt);

    // Update shield timers
    for (auto& [id, p] : m_players) {
        if (p.hasShield) {
            p.shieldTimer -= fdt;
            if (p.shieldTimer <= 0.0f) {
                p.hasShield = false;
                p.shieldTimer = 0.0f;
            }
        }
    }

    // Update elimination feed timers
    for (auto& e : m_eliminationFeed) {
        e.timeRemaining -= dt;
    }
    while (!m_eliminationFeed.empty() && m_eliminationFeed.back().timeRemaining <= 0.0) {
        m_eliminationFeed.pop_back();
    }

    switch (m_phase) {
    case GamePhase::Lobby: {
        m_lobbyTimer += dt;

        int pCount = 0;
        for (const auto& [_, p] : m_players) {
            if (p.alive) pCount++;
        }

        if (pCount >= m_minPlayers && m_lobbyTimer >= 5.0) {
            startCountdown();
        }
        break;
    }

    case GamePhase::Countdown: {
        m_countdownTimer -= dt;
        if (m_countdownTimer <= 0.0) {
            startBattle();
        }
        break;
    }

    case GamePhase::Battle: {
        m_roundTimer += dt;

        // Store previous positions for interpolation
        for (auto& [id, p] : m_players) {
            if (p.body) {
                p.prevPos = p.currPos;
            }
        }

        // Step physics
        m_world->Step(fdt, 8, 3);

        // Update current positions
        for (auto& [id, p] : m_players) {
            if (p.body) {
                p.currPos = p.body->GetPosition();
            }
        }

        // Speed up arena rotation over time
        m_arenaAngularVel += m_arenaSpeedIncrease * fdt;
        if (m_arenaBody) {
            m_arenaBody->SetAngularVelocity(m_arenaAngularVel);
            m_arenaAngle = m_arenaBody->GetAngle();
        }

        // Check eliminations & round end
        checkEliminations();
        checkRoundEnd();
        break;
    }

    case GamePhase::RoundEnd: {
        // Still step physics so eliminated balls settle
        m_world->Step(fdt, 6, 2);
        for (auto& [id, p] : m_players) {
            if (p.body) {
                p.prevPos = p.currPos;
                p.currPos = p.body->GetPosition();
            }
        }

        m_roundEndTimer -= dt;
        if (m_roundEndTimer <= 0.0) {
            resetForNextRound();
        }
        break;
    }
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// ██ RENDERING ████████████████████████████████████████████████████████████████
// ═════════════════════════════════════════════════════════════════════════════

void CountryElimination::render(sf::RenderTarget& target, double alpha) {
    // Dark background
    if (auto* rt = dynamic_cast<sf::RenderTexture*>(&target)) {
        rt->clear(sf::Color(15, 15, 30));
    }

    renderArena(target);
    renderPlayers(target, alpha);
    renderUI(target);
    renderEliminationFeed(target);

    if (m_phase == GamePhase::Countdown) {
        renderCountdown(target);
    }

    // Post-processing
    if (auto* rt = dynamic_cast<sf::RenderTexture*>(&target)) {
        m_postProcessing.applyVignette(*rt, 0.4f);
    }
}

// ── Arena Rendering ──────────────────────────────────────────────────────────

void CountryElimination::renderArena(sf::RenderTarget& target) {
    sf::Vector2f center = worldToScreen(ARENA_CENTER_X, ARENA_CENTER_Y);
    float radiusPx = ARENA_RADIUS * PIXELS_PER_METER;
    float wallPx = WALL_THICKNESS * PIXELS_PER_METER;

    // Draw the circular wall segments
    float angleStep = 2.0f * b2_pi / WALL_SEGMENTS;
    float currentAngle = m_arenaAngle;

    for (int i = 0; i < WALL_SEGMENTS; ++i) {
        float segAngle = currentAngle + i * angleStep;

        // Skip gap segments (same logic as physics)
        float baseAngle = i * angleStep;
        float normAngle = baseAngle;
        if (normAngle > b2_pi) normAngle -= 2.0f * b2_pi;
        if (std::abs(normAngle) < GAP_HALF_ANGLE) continue;

        float x1 = center.x + radiusPx * std::cos(segAngle);
        float y1 = center.y + radiusPx * std::sin(segAngle);
        float x2 = center.x + radiusPx * std::cos(segAngle + angleStep);
        float y2 = center.y + radiusPx * std::sin(segAngle + angleStep);

        // Draw thick line as a rotated rectangle
        float dx = x2 - x1;
        float dy = y2 - y1;
        float len = std::sqrt(dx * dx + dy * dy);
        float angle = std::atan2(dy, dx) * 180.0f / b2_pi;

        sf::RectangleShape seg(sf::Vector2f(len, wallPx));
        seg.setOrigin(0.0f, wallPx / 2.0f);
        seg.setPosition(x1, y1);
        seg.setRotation(angle);
        seg.setFillColor(sf::Color(80, 100, 200, 200));
        seg.setOutlineColor(sf::Color(120, 150, 255, 255));
        seg.setOutlineThickness(1.0f);
        target.draw(seg);
    }

    // Draw a faint circle guide
    sf::CircleShape guide(radiusPx, 64);
    guide.setOrigin(radiusPx, radiusPx);
    guide.setPosition(center);
    guide.setFillColor(sf::Color::Transparent);
    guide.setOutlineColor(sf::Color(60, 80, 160, 60));
    guide.setOutlineThickness(1.0f);
    target.draw(guide);

    // Draw gap indicator (arrow / highlight)
    float gapAngle = currentAngle;
    float gapX = center.x + (radiusPx + 15.0f) * std::cos(gapAngle);
    float gapY = center.y + (radiusPx + 15.0f) * std::sin(gapAngle);

    sf::CircleShape gapMarker(8.0f, 3); // triangle
    gapMarker.setOrigin(8.0f, 8.0f);
    gapMarker.setPosition(gapX, gapY);
    gapMarker.setRotation(gapAngle * 180.0f / b2_pi + 90.0f);
    gapMarker.setFillColor(sf::Color(255, 100, 100, 200));
    target.draw(gapMarker);
}

// ── Player Rendering ─────────────────────────────────────────────────────────

void CountryElimination::renderPlayers(sf::RenderTarget& target, double alpha) {
    float a = static_cast<float>(alpha);

    for (const auto& [id, p] : m_players) {
        if (!p.body) continue;

        // Interpolate position
        float ix = p.prevPos.x + (p.currPos.x - p.prevPos.x) * a;
        float iy = p.prevPos.y + (p.currPos.y - p.prevPos.y) * a;
        sf::Vector2f screenPos = worldToScreen(ix, iy);
        float radiusPx = p.radiusM * PIXELS_PER_METER;

        // Draw ball
        sf::CircleShape ball(radiusPx);
        ball.setOrigin(radiusPx, radiusPx);
        ball.setPosition(screenPos);

        sf::Color ballColor = p.color;
        if (!p.alive) ballColor.a = 140; // faded for eliminated
        ball.setFillColor(ballColor);
        ball.setOutlineColor(sf::Color(255, 255, 255, p.alive ? 180 : 80));
        ball.setOutlineThickness(1.5f);
        target.draw(ball);

        // Shield glow
        if (p.hasShield && p.alive) {
            sf::CircleShape shield(radiusPx + 4.0f);
            shield.setOrigin(radiusPx + 4.0f, radiusPx + 4.0f);
            shield.setPosition(screenPos);
            shield.setFillColor(sf::Color::Transparent);
            sf::Uint8 pulse = static_cast<sf::Uint8>(100 + 80 * std::abs(std::sin(p.shieldTimer * 4.0f)));
            shield.setOutlineColor(sf::Color(100, 200, 255, pulse));
            shield.setOutlineThickness(2.5f);
            target.draw(shield);
        }

        // Draw label text on ball
        if (m_fontLoaded) {
            sf::Text labelText;
            labelText.setFont(m_font);
            labelText.setString(p.label);
            labelText.setCharacterSize(fs(static_cast<int>(radiusPx * 0.9f)));
            labelText.setFillColor(sf::Color::White);
            labelText.setOutlineColor(sf::Color(0, 0, 0, 200));
            labelText.setOutlineThickness(1.0f);
            sf::FloatRect lb = labelText.getLocalBounds();
            labelText.setOrigin(lb.left + lb.width / 2.0f, lb.top + lb.height / 2.0f);
            labelText.setPosition(screenPos);
            target.draw(labelText);
        }

        // Player name below ball (only if alive)
        if (m_fontLoaded && p.alive) {
            sf::Text nameText;
            nameText.setFont(m_font);
            nameText.setString(p.displayName);
            nameText.setCharacterSize(fs(10));
            nameText.setFillColor(sf::Color(220, 220, 220, 180));
            sf::FloatRect nb = nameText.getLocalBounds();
            nameText.setOrigin(nb.left + nb.width / 2.0f, 0.0f);
            nameText.setPosition(screenPos.x, screenPos.y + radiusPx + 2.0f);
            target.draw(nameText);
        }
    }
}

// ── Eliminated Pile (not a separate method, handled in renderPlayers above) ──

void CountryElimination::renderEliminatedPile(sf::RenderTarget& /*target*/) {
    // Eliminated balls are rendered by renderPlayers (faded); this is a no-op stub.
}

// ── UI Rendering ─────────────────────────────────────────────────────────────

void CountryElimination::renderUI(sf::RenderTarget& target) {
    if (!m_fontLoaded) return;

    // Title
    {
        auto r = resolve("title", SCREEN_W, SCREEN_H);
        if (r.visible) {
            sf::Text text;
            text.setFont(m_font);
            text.setString("COUNTRY ELIMINATION");
            sf::Color col(255, 255, 255, 220);
            parseHexColor(r.color, col);
            text.setFillColor(col);
            text.setOutlineColor(sf::Color(0, 0, 0, 200));
            text.setOutlineThickness(1.0f);
            applyTextLayout(text, r);
            target.draw(text);
        }
    }

    // Phase indicator
    {
        auto r = resolve("phase", SCREEN_W, SCREEN_H);
        if (r.visible) {
            std::string phaseStr;
            switch (m_phase) {
            case GamePhase::Lobby:     phaseStr = "Waiting for players..."; break;
            case GamePhase::Countdown: phaseStr = "Get ready!"; break;
            case GamePhase::Battle:    phaseStr = "Round " + std::to_string(m_roundNumber); break;
            case GamePhase::RoundEnd:  phaseStr = "Round Over!"; break;
            }
            sf::Text text;
            text.setFont(m_font);
            text.setString(phaseStr);
            sf::Color col(200, 200, 200, 200);
            parseHexColor(r.color, col);
            text.setFillColor(col);
            text.setOutlineColor(sf::Color(0, 0, 0, 160));
            text.setOutlineThickness(1.0f);
            applyTextLayout(text, r);
            target.draw(text);
        }
    }

    // Player count
    {
        auto r = resolve("player_count", SCREEN_W, SCREEN_H);
        if (r.visible) {
            int alive = 0, total = 0;
            for (const auto& [_, p] : m_players) {
                total++;
                if (p.alive) alive++;
            }
            sf::Text text;
            text.setFont(m_font);
            text.setString(std::to_string(alive) + " / " + std::to_string(total) + " alive");
            sf::Color col(200, 200, 200, 170);
            parseHexColor(r.color, col);
            text.setFillColor(col);
            text.setOutlineColor(sf::Color(0, 0, 0, 120));
            text.setOutlineThickness(1.0f);
            applyTextLayout(text, r);
            target.draw(text);
        }
    }

    // Join hint
    {
        auto r = resolve("join_hint", SCREEN_W, SCREEN_H);
        if (r.visible) {
            sf::Text text;
            text.setFont(m_font);
            text.setString("Type join <country> to play!");
            sf::Color col(170, 170, 170, 150);
            parseHexColor(r.color, col);
            text.setFillColor(col);
            text.setOutlineColor(sf::Color(0, 0, 0, 100));
            text.setOutlineThickness(1.0f);
            applyTextLayout(text, r);
            target.draw(text);
        }
    }

    // Sub reward info
    {
        auto r = resolve("sub_info", SCREEN_W, SCREEN_H);
        if (r.visible) {
            sf::Text text;
            text.setFont(m_font);
            text.setString("SUB = Shield 15s + 300pts | BITS/SC > 100 = Bigger Ball + Shield + 500pts");
            sf::Color col(170, 170, 204, 153);
            parseHexColor(r.color, col);
            text.setFillColor(col);
            text.setOutlineColor(sf::Color(0, 0, 0, 100));
            text.setOutlineThickness(1.0f);
            applyTextLayout(text, r);
            target.draw(text);
        }
    }

    // Winner overlay during RoundEnd
    if (m_phase == GamePhase::RoundEnd && !m_winnerId.empty() && m_players.count(m_winnerId)) {
        const Player& winner = m_players.at(m_winnerId);

        {
            auto r = resolve("winner_text", SCREEN_W, SCREEN_H);
            sf::Text text;
            text.setFont(m_font);
            text.setString("WINNER!");
            sf::Color col(255, 215, 0);
            parseHexColor(r.color, col);
            text.setFillColor(col);
            text.setOutlineColor(sf::Color(0, 0, 0, 230));
            text.setOutlineThickness(2.0f);
            applyTextLayout(text, r);
            target.draw(text);
        }
        {
            auto r = resolve("winner_label", SCREEN_W, SCREEN_H);
            sf::Text text;
            text.setFont(m_font);
            text.setString(winner.displayName + " [" + winner.label + "]");
            sf::Color col(255, 255, 255, 220);
            parseHexColor(r.color, col);
            text.setFillColor(col);
            text.setOutlineColor(sf::Color(0, 0, 0, 200));
            text.setOutlineThickness(1.5f);
            applyTextLayout(text, r);
            target.draw(text);
        }
    }
}

void CountryElimination::renderCountdown(sf::RenderTarget& target) {
    if (!m_fontLoaded) return;

    auto r = resolve("countdown", SCREEN_W, SCREEN_H);
    if (!r.visible) return;

    int num = static_cast<int>(std::ceil(m_countdownTimer));
    if (num <= 0) return;

    sf::Text text;
    text.setFont(m_font);
    text.setString(std::to_string(num));
    text.setCharacterSize(r.fontSize);
    text.setFillColor(sf::Color(255, 255, 255, 240));
    text.setOutlineColor(sf::Color(0, 0, 0, 220));
    text.setOutlineThickness(2.0f);

    sf::FloatRect lb = text.getLocalBounds();
    text.setOrigin(lb.left + lb.width / 2.0f, lb.top + lb.height / 2.0f);
    text.setPosition(r.px, r.py);

    // Pulse effect
    float frac = static_cast<float>(m_countdownTimer) - std::floor(static_cast<float>(m_countdownTimer));
    float scale = 1.0f + frac * 0.3f;
    text.setScale(scale, scale);

    target.draw(text);
}

void CountryElimination::renderEliminationFeed(sf::RenderTarget& target) {
    if (!m_fontLoaded || m_eliminationFeed.empty()) return;

    auto r = resolve("elim_feed", SCREEN_W, SCREEN_H);
    if (!r.visible) return;

    float y = r.py;
    for (const auto& e : m_eliminationFeed) {
        float alpha = std::min(1.0f, static_cast<float>(e.timeRemaining));
        sf::Uint8 a = static_cast<sf::Uint8>(alpha * 200);

        sf::Text text;
        text.setFont(m_font);
        text.setString("X " + e.displayName + " [" + e.label + "]");
        text.setCharacterSize(r.fontSize);
        sf::Color col(255, 100, 100, a);
        parseHexColor(r.color, col);
        col.a = a;
        text.setFillColor(col);
        text.setOutlineColor(sf::Color(0, 0, 0, a));
        text.setOutlineThickness(1.0f);

        sf::FloatRect lb = text.getLocalBounds();
        if (r.align == TextAlign::Right) {
            text.setOrigin(lb.left + lb.width, 0.0f);
        } else {
            text.setOrigin(0.0f, 0.0f);
        }
        text.setPosition(r.px, y);
        target.draw(text);

        y += r.fontSize + 4;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// ██ COLOR GENERATION █████████████████████████████████████████████████████████
// ═════════════════════════════════════════════════════════════════════════════

sf::Color CountryElimination::generateColor() {
    std::uniform_int_distribution<int> hue(0, 359);
    int h = hue(m_rng);
    // HSV -> RGB with S=0.7, V=0.9
    float s = 0.7f, v = 0.9f;
    float c = v * s;
    float x = c * (1.0f - std::abs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    float r1, g1, b1;
    if      (h < 60)  { r1 = c; g1 = x; b1 = 0; }
    else if (h < 120) { r1 = x; g1 = c; b1 = 0; }
    else if (h < 180) { r1 = 0; g1 = c; b1 = x; }
    else if (h < 240) { r1 = 0; g1 = x; b1 = c; }
    else if (h < 300) { r1 = x; g1 = 0; b1 = c; }
    else              { r1 = c; g1 = 0; b1 = x; }
    return sf::Color(
        static_cast<sf::Uint8>((r1 + m) * 255),
        static_cast<sf::Uint8>((g1 + m) * 255),
        static_cast<sf::Uint8>((b1 + m) * 255));
}

// ═════════════════════════════════════════════════════════════════════════════
// ██ INTERFACE METHODS ████████████████████████████████████████████████████████
// ═════════════════════════════════════════════════════════════════════════════

bool CountryElimination::isRoundComplete() const {
    return m_phase == GamePhase::RoundEnd;
}

bool CountryElimination::isGameOver() const {
    return false; // Loops forever
}

nlohmann::json CountryElimination::getState() const {
    nlohmann::json state;

    std::string phaseStr;
    switch (m_phase) {
    case GamePhase::Lobby:     phaseStr = "lobby"; break;
    case GamePhase::Countdown: phaseStr = "countdown"; break;
    case GamePhase::Battle:    phaseStr = "battle"; break;
    case GamePhase::RoundEnd:  phaseStr = "round_end"; break;
    }
    state["phase"] = phaseStr;
    state["round"] = m_roundNumber;
    state["roundTimer"] = m_roundTimer;

    int alive = 0;
    nlohmann::json players = nlohmann::json::array();
    for (const auto& [id, p] : m_players) {
        nlohmann::json pj;
        pj["id"] = id;
        pj["name"] = p.displayName;
        pj["label"] = p.label;
        pj["alive"] = p.alive;
        pj["score"] = p.score;
        pj["shield"] = p.hasShield;
        players.push_back(pj);
        if (p.alive) alive++;
    }
    state["players"] = players;
    state["playerCount"] = static_cast<int>(m_players.size());
    state["aliveCount"] = alive;

    return state;
}

nlohmann::json CountryElimination::getCommands() const {
    return nlohmann::json::array({
        {{"command", "join"}, {"description", "Join with a country label"}, {"aliases", nlohmann::json::array({"play"})}},
    });
}

std::vector<std::pair<std::string, int>> CountryElimination::getLeaderboard() const {
    std::vector<std::pair<std::string, int>> result;
    for (const auto& [id, p] : m_players) {
        result.emplace_back(p.displayName, p.score);
    }
    std::sort(result.begin(), result.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    return result;
}

void CountryElimination::configure(const nlohmann::json& settings) {
    if (settings.contains("arena_speed") && settings["arena_speed"].is_number()) {
        m_arenaAngularVel = std::max(0.05f, settings["arena_speed"].get<float>());
    }
    if (settings.contains("arena_speed_increase") && settings["arena_speed_increase"].is_number()) {
        m_arenaSpeedIncrease = std::max(0.0f, settings["arena_speed_increase"].get<float>());
    }
    if (settings.contains("initial_speed") && settings["initial_speed"].is_number()) {
        m_initialSpeed = std::max(0.5f, settings["initial_speed"].get<float>());
    }
    if (settings.contains("restitution") && settings["restitution"].is_number()) {
        m_restitution = std::clamp(settings["restitution"].get<float>(), 0.0f, 1.0f);
    }
    if (settings.contains("min_players") && settings["min_players"].is_number_integer()) {
        m_minPlayers = std::max(2, settings["min_players"].get<int>());
    }
    if (settings.contains("lobby_duration") && settings["lobby_duration"].is_number()) {
        m_lobbyDuration = std::max(1.0, settings["lobby_duration"].get<double>());
    }
    if (settings.contains("round_end_duration") && settings["round_end_duration"].is_number()) {
        m_roundEndDuration = std::max(1.0, settings["round_end_duration"].get<double>());
    }
    if (settings.contains("text_elements") && settings["text_elements"].is_array()) {
        applyTextOverrides(settings["text_elements"]);
    }
}

nlohmann::json CountryElimination::getSettings() const {
    return {
        {"arena_speed", m_arenaAngularVel},
        {"arena_speed_increase", m_arenaSpeedIncrease},
        {"initial_speed", m_initialSpeed},
        {"restitution", m_restitution},
        {"min_players", m_minPlayers},
        {"lobby_duration", m_lobbyDuration},
        {"round_end_duration", m_roundEndDuration},
        {"text_elements", textElementsJson()},
    };
}

} // namespace is::games::country_elimination
