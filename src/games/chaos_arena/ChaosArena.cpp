#include "games/chaos_arena/ChaosArena.h"
#include "games/GameRegistry.h"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <sstream>

namespace is::games::chaos_arena {

// Screen center offsets for world→screen conversion (1080x1920, 9:16 vertical)
static constexpr float SCREEN_CX = 540.0f;   // 1080 / 2
static constexpr float SCREEN_CY = 960.0f;   // 1920 / 2

// Auto-register this game
REGISTER_GAME(ChaosArena);

ChaosArena::ChaosArena()
    : m_rng(std::random_device{}())
{
}

ChaosArena::~ChaosArena() = default;

void ChaosArena::initialize() {
    spdlog::info("[ChaosArena] Initializing...");

    m_physics = std::make_unique<PhysicsWorld>(0.0f, 15.0f);
    m_arena   = std::make_unique<Arena>();
    m_particles = std::make_unique<ParticleSystem>(15000);

    m_arena->generate(m_physics->world(), ARENA_WIDTH, ARENA_HEIGHT);

    // Initialize background (stars, nebulae) — 9:16 vertical
    m_background.initialize(1080, 1920);

    // Initialize post-processing — 9:16 vertical
    m_postProcessing.initialize(1080, 1920);

    // Load font for text rendering
    if (m_font.loadFromFile("assets/fonts/JetBrainsMono-Regular.ttf")) {
        m_fontLoaded = true;
        spdlog::info("[ChaosArena] Font loaded successfully.");
    } else {
        spdlog::warn("[ChaosArena] Font not found, text rendering disabled.");
    }

    // Set up collision callback for visual effects
    m_physics->setContactCallback([this](const ContactInfo& info) {
        sf::Vector2f pos(info.contactPoint.x * PIXELS_PER_METER + SCREEN_CX,
                         info.contactPoint.y * PIXELS_PER_METER + SCREEN_CY);
        if (info.impulse > 3.0f) {
            m_particles->emitDust(pos);
        }
    });

    m_phase = GamePhase::Lobby;
    m_lobbyTimer = m_lobbyDuration;
    m_roundNumber = 0;

    spdlog::info("[ChaosArena] Initialized. Waiting in lobby.");
}

void ChaosArena::shutdown() {
    spdlog::info("[ChaosArena] Shutting down...");

    // Clean up physics bodies
    for (auto& [id, player] : m_players) {
        if (player.body) {
            m_physics->destroyBody(player.body);
            player.body = nullptr;
        }
    }
    for (auto& proj : m_projectiles) {
        if (proj.body) {
            m_physics->destroyBody(proj.body);
            proj.body = nullptr;
        }
    }
    for (auto& pu : m_powerUps) {
        if (pu.body) {
            m_physics->destroyBody(pu.body);
            pu.body = nullptr;
        }
    }

    m_players.clear();
    m_projectiles.clear();
    m_powerUps.clear();
    m_particles->clear();
}

// ─── Chat Command Processing ─────────────────────────────────────────────────

void ChaosArena::onChatMessage(const platform::ChatMessage& msg) {
    if (msg.text.empty() || msg.text[0] != '!') return;

    // Parse command
    std::istringstream iss(msg.text);
    std::string cmd;
    iss >> cmd;

    // Convert to lowercase
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);

    if (cmd == "!join" || cmd == "!play") {
        cmdJoin(msg.userId, msg.displayName);
    } else if (cmd == "!left" || cmd == "!l" || cmd == "!a") {
        cmdLeft(msg.userId);
    } else if (cmd == "!right" || cmd == "!r" || cmd == "!d") {
        cmdRight(msg.userId);
    } else if (cmd == "!jump" || cmd == "!j" || cmd == "!w" || cmd == "!up") {
        cmdJump(msg.userId);
    } else if (cmd == "!jumpleft" || cmd == "!jl") {
        cmdJumpLeft(msg.userId);
    } else if (cmd == "!jumpright" || cmd == "!jr") {
        cmdJumpRight(msg.userId);
    } else if (cmd == "!attack" || cmd == "!hit" || cmd == "!atk") {
        cmdAttack(msg.userId);
    } else if (cmd == "!special" || cmd == "!sp" || cmd == "!ult") {
        cmdSpecial(msg.userId);
    } else if (cmd == "!dash" || cmd == "!dodge") {
        cmdDash(msg.userId);
    } else if (cmd == "!block" || cmd == "!shield" || cmd == "!def") {
        cmdBlock(msg.userId);
    } else if (cmd == "!emote") {
        std::string emote;
        iss >> emote;
        cmdEmote(msg.userId, emote);
    }
}

void ChaosArena::cmdJoin(const std::string& userId, const std::string& displayName) {
    if (m_players.count(userId)) return;  // Already joined
    if (static_cast<int>(m_players.size()) >= m_maxPlayers) return;

    // Create player
    Player player;
    player.userId = userId;
    player.displayName = displayName;
    player.color = generatePlayerColor();

    // Spawn at random position above the arena
    std::uniform_real_distribution<float> xDist(-ARENA_WIDTH / 2 + 3, ARENA_WIDTH / 2 - 3);
    float spawnX = xDist(m_rng);
    float spawnY = -ARENA_HEIGHT / 2 + 3;

    player.body = m_physics->createPlayerBody(spawnX, spawnY,
        Player::HALF_WIDTH, Player::HALF_HEIGHT);
    player.body->GetUserData().pointer = reinterpret_cast<uintptr_t>(&player);
    player.invulnTimer = 3.0f;  // Spawn protection

    m_players[userId] = std::move(player);
    // Fix the user data pointer after move
    m_players[userId].body->GetUserData().pointer =
        reinterpret_cast<uintptr_t>(&m_players[userId]);

    // Spawn particle effect
    sf::Vector2f screenPos(SCREEN_CX + spawnX * PIXELS_PER_METER, SCREEN_CY + spawnY * PIXELS_PER_METER);
    m_particles->emitExplosion(screenPos, m_players[userId].color, 30);

    // Initialize leaderboard entry
    if (!m_leaderboard.count(userId)) {
        m_leaderboard[userId] = {displayName, 0, 0, 0};
    }

    spdlog::info("[ChaosArena] Player '{}' joined! ({} players)", displayName, m_players.size());
}

void ChaosArena::cmdLeft(const std::string& userId) {
    auto it = m_players.find(userId);
    if (it == m_players.end() || !it->second.alive) return;
    if (m_phase != GamePhase::Battle) return;

    auto& p = it->second;
    p.facingDir = -1;
    b2Vec2 vel = p.body->GetLinearVelocity();
    vel.x = -p.moveSpeed;
    p.body->SetLinearVelocity(vel);
}

void ChaosArena::cmdRight(const std::string& userId) {
    auto it = m_players.find(userId);
    if (it == m_players.end() || !it->second.alive) return;
    if (m_phase != GamePhase::Battle) return;

    auto& p = it->second;
    p.facingDir = 1;
    b2Vec2 vel = p.body->GetLinearVelocity();
    vel.x = p.moveSpeed;
    p.body->SetLinearVelocity(vel);
}

void ChaosArena::cmdJump(const std::string& userId) {
    auto it = m_players.find(userId);
    if (it == m_players.end() || !it->second.alive) return;
    if (m_phase != GamePhase::Battle) return;

    auto& p = it->second;
    bool onGround = m_physics->isOnGround(p.body);
    if (onGround) {
        p.jumpsLeft = 2;
    }

    if (p.jumpsLeft > 0) {
        b2Vec2 vel = p.body->GetLinearVelocity();
        vel.y = -p.jumpForce;
        p.body->SetLinearVelocity(vel);
        p.jumpsLeft--;

        // Jump dust particles
        auto pos = p.body->GetPosition();
        sf::Vector2f screenPos(SCREEN_CX + pos.x * PIXELS_PER_METER,
                               SCREEN_CY + (pos.y + Player::HALF_HEIGHT) * PIXELS_PER_METER);
        m_particles->emitDust(screenPos);
    }
}

void ChaosArena::cmdJumpLeft(const std::string& userId) {
    auto it = m_players.find(userId);
    if (it == m_players.end() || !it->second.alive) return;
    if (m_phase != GamePhase::Battle) return;

    auto& p = it->second;
    bool onGround = m_physics->isOnGround(p.body);
    if (onGround) {
        p.jumpsLeft = 2;
    }

    if (p.jumpsLeft > 0) {
        p.facingDir = -1;
        b2Vec2 vel;
        vel.x = -p.moveSpeed;
        vel.y = -p.jumpForce;
        p.body->SetLinearVelocity(vel);
        p.jumpsLeft--;

        auto pos = p.body->GetPosition();
        sf::Vector2f screenPos(SCREEN_CX + pos.x * PIXELS_PER_METER,
                               SCREEN_CY + (pos.y + Player::HALF_HEIGHT) * PIXELS_PER_METER);
        m_particles->emitDust(screenPos);
    }
}

void ChaosArena::cmdJumpRight(const std::string& userId) {
    auto it = m_players.find(userId);
    if (it == m_players.end() || !it->second.alive) return;
    if (m_phase != GamePhase::Battle) return;

    auto& p = it->second;
    bool onGround = m_physics->isOnGround(p.body);
    if (onGround) {
        p.jumpsLeft = 2;
    }

    if (p.jumpsLeft > 0) {
        p.facingDir = 1;
        b2Vec2 vel;
        vel.x = p.moveSpeed;
        vel.y = -p.jumpForce;
        p.body->SetLinearVelocity(vel);
        p.jumpsLeft--;

        auto pos = p.body->GetPosition();
        sf::Vector2f screenPos(SCREEN_CX + pos.x * PIXELS_PER_METER,
                               SCREEN_CY + (pos.y + Player::HALF_HEIGHT) * PIXELS_PER_METER);
        m_particles->emitDust(screenPos);
    }
}

void ChaosArena::cmdAttack(const std::string& userId) {
    auto it = m_players.find(userId);
    if (it == m_players.end() || !it->second.alive) return;
    if (m_phase != GamePhase::Battle) return;

    auto& p = it->second;
    if (p.attackCooldown > 0) return;

    p.attackCooldown = Player::ATTACK_CD;

    // Melee attack – check for nearby players
    auto pos = p.body->GetPosition();
    float attackRange = 1.8f;  // meters

    for (auto& [otherId, other] : m_players) {
        if (otherId == userId || !other.alive) continue;

        auto otherPos = other.body->GetPosition();
        float dx = otherPos.x - pos.x;
        float dy = otherPos.y - pos.y;
        float dist = std::sqrt(dx * dx + dy * dy);

        // Check if in range and in facing direction
        bool inDirection = (p.facingDir > 0 && dx > 0) || (p.facingDir < 0 && dx < 0);
        if (dist < attackRange && (inDirection || dist < 1.0f)) {
            applyDamage(p, other, p.getEffectiveDamage(), "melee");

            // Knockback
            float kbForce = 8.0f * p.knockbackMul;
            float angle = std::atan2(dy, dx);
            b2Vec2 knockback(std::cos(angle) * kbForce, -4.0f);
            other.body->ApplyLinearImpulseToCenter(knockback, true);

            // Hit particles
            sf::Vector2f hitPos(SCREEN_CX + (pos.x + dx * 0.5f) * PIXELS_PER_METER,
                                SCREEN_CY + (pos.y + dy * 0.5f) * PIXELS_PER_METER);
            sf::Vector2f hitDir(dx, dy);
            m_particles->emitHitSpark(hitPos, hitDir, p.color);
        }
    }

    // Attack visual (swing effect)
    sf::Vector2f swingPos(SCREEN_CX + (pos.x + p.facingDir * 1.0f) * PIXELS_PER_METER,
                          SCREEN_CY + pos.y * PIXELS_PER_METER);
    ParticleEmitterConfig cfg;
    cfg.position = swingPos;
    cfg.count = 8;
    cfg.speed = 60.0f;
    cfg.angle = p.facingDir > 0 ? 0.0f : 180.0f;
    cfg.spread = 90.0f;
    cfg.life = 0.2f;
    cfg.size = 3.0f;
    cfg.color = p.color;
    cfg.gravity = 0.0f;
    m_particles->emit(cfg);
}

void ChaosArena::cmdSpecial(const std::string& userId) {
    auto it = m_players.find(userId);
    if (it == m_players.end() || !it->second.alive) return;
    if (m_phase != GamePhase::Battle) return;

    auto& p = it->second;
    if (p.specialCooldown > 0) return;

    p.specialCooldown = Player::SPECIAL_CD;

    // Fire a projectile
    auto pos = p.body->GetPosition();
    float projSpeed = 18.0f;
    float vx = p.facingDir * projSpeed;
    float vy = -2.0f;  // Slight upward arc

    auto* projBody = m_physics->createProjectileBody(
        pos.x + p.facingDir * 1.0f, pos.y - 0.2f, 0.2f, vx, vy);

    Projectile proj;
    proj.body = projBody;
    proj.ownerId = userId;
    proj.color = p.color;
    proj.damage = 25.0f;
    proj.radius = 0.2f;
    proj.life = 3.0f;
    m_projectiles.push_back(proj);

    // Muzzle flash particles
    sf::Vector2f muzzlePos(SCREEN_CX + (pos.x + p.facingDir * 1.0f) * PIXELS_PER_METER,
                           SCREEN_CY + (pos.y - 0.2f) * PIXELS_PER_METER);
    m_particles->emitExplosion(muzzlePos, p.color, 15);

    spdlog::debug("[ChaosArena] {} fired special attack!", p.displayName);
}

void ChaosArena::cmdDash(const std::string& userId) {
    auto it = m_players.find(userId);
    if (it == m_players.end() || !it->second.alive) return;
    if (m_phase != GamePhase::Battle) return;

    auto& p = it->second;
    if (p.dashCooldown > 0) return;

    p.dashCooldown = Player::DASH_CD;
    p.invulnTimer = 0.2f;  // Brief i-frames during dash

    float dashSpeed = 20.0f;
    b2Vec2 vel = p.body->GetLinearVelocity();
    vel.x = p.facingDir * dashSpeed;
    p.body->SetLinearVelocity(vel);

    // Trail particles
    auto pos = p.body->GetPosition();
    sf::Vector2f screenPos(SCREEN_CX + pos.x * PIXELS_PER_METER, SCREEN_CY + pos.y * PIXELS_PER_METER);
    for (int i = 0; i < 5; ++i) {
        m_particles->emitTrail(screenPos, p.color);
    }
}

void ChaosArena::cmdBlock(const std::string& userId) {
    auto it = m_players.find(userId);
    if (it == m_players.end() || !it->second.alive) return;
    if (m_phase != GamePhase::Battle) return;

    auto& p = it->second;
    if (p.blockTimer > 0) return;

    p.blockTimer = Player::BLOCK_DUR;
    p.blocking = true;

    // Shield visual
    auto pos = p.body->GetPosition();
    sf::Vector2f screenPos(SCREEN_CX + pos.x * PIXELS_PER_METER, SCREEN_CY + pos.y * PIXELS_PER_METER);
    ParticleEmitterConfig cfg;
    cfg.position = screenPos;
    cfg.count = 20;
    cfg.speed = 40.0f;
    cfg.spread = 360.0f;
    cfg.life = 0.4f;
    cfg.size = 4.0f;
    cfg.color = sf::Color(100, 200, 255, 200);
    cfg.gravity = 0.0f;
    m_particles->emit(cfg);
}

void ChaosArena::cmdEmote(const std::string& userId, const std::string& emote) {
    // Cosmetic only – could display emote above player
    (void)userId;
    (void)emote;
}

// ─── Game Logic ──────────────────────────────────────────────────────────────

void ChaosArena::update(double dt) {
    float fdt = static_cast<float>(dt);

    // Update background animations (stars twinkling, nebula pulsing)
    m_background.update(fdt);

    switch (m_phase) {
        case GamePhase::Lobby:
            m_lobbyTimer -= dt;
            if (static_cast<int>(m_players.size()) >= m_minPlayers && m_lobbyTimer <= 0) {
                startCountdown();
            }
            break;

        case GamePhase::Countdown:
            m_countdownTimer -= dt;
            if (m_countdownTimer <= 0) {
                startBattle();
            }
            break;

        case GamePhase::Battle: {
            // Update physics
            m_physics->step(fdt);

            // Update players
            for (auto& [id, player] : m_players) {
                if (!player.alive) continue;

                player.updateTimers(fdt);
                player.onGround = m_physics->isOnGround(player.body);

                // Store previous position for interpolation
                auto pos = player.body->GetPosition();
                player.prevPosition = player.renderPosition;
                player.renderPosition = {pos.x, pos.y};

                // Trail particles for fast-moving players
                auto vel = player.body->GetLinearVelocity();
                float speed = vel.Length();
                if (speed > 10.0f) {
                    sf::Vector2f screenPos(SCREEN_CX + pos.x * PIXELS_PER_METER,
                                           SCREEN_CY + pos.y * PIXELS_PER_METER);
                    m_particles->emitTrail(screenPos, player.color);
                }

                // Death zone check
                if (m_arena->isInDeathZone(pos)) {
                    eliminatePlayer(player, "Arena", "fell");
                }
            }

            // Update projectiles
            for (auto& proj : m_projectiles) {
                if (!proj.alive) continue;
                proj.update(fdt);

                if (!proj.body) { proj.alive = false; continue; }
                auto pos = proj.body->GetPosition();

                // Check collision with players
                for (auto& [pid, player] : m_players) {
                    if (pid == proj.ownerId || !player.alive) continue;
                    auto pPos = player.body->GetPosition();
                    float dx = pPos.x - pos.x;
                    float dy = pPos.y - pos.y;
                    float dist = std::sqrt(dx * dx + dy * dy);

                    if (dist < proj.radius + Player::HALF_WIDTH) {
                        // Find the attacker
                        auto attackerIt = m_players.find(proj.ownerId);
                        if (attackerIt != m_players.end()) {
                            applyDamage(attackerIt->second, player, proj.damage, "projectile");

                            // Knockback
                            float angle = std::atan2(dy, dx);
                            b2Vec2 kb(std::cos(angle) * 10.0f, -3.0f);
                            player.body->ApplyLinearImpulseToCenter(kb, true);
                        }

                        // Impact particles
                        sf::Vector2f impactPos(SCREEN_CX + pos.x * PIXELS_PER_METER,
                                               SCREEN_CY + pos.y * PIXELS_PER_METER);
                        m_particles->emitExplosion(impactPos, proj.color, 20);
                        proj.alive = false;
                        break;
                    }
                }

                // Trail particle for projectile
                if (proj.alive) {
                    sf::Vector2f screenPos(SCREEN_CX + pos.x * PIXELS_PER_METER,
                                           SCREEN_CY + pos.y * PIXELS_PER_METER);
                    m_particles->emitTrail(screenPos, proj.color);
                }
            }

            // Clean up dead projectiles
            for (auto& proj : m_projectiles) {
                if (!proj.alive && proj.body) {
                    m_physics->destroyBody(proj.body);
                    proj.body = nullptr;
                }
            }
            m_projectiles.erase(
                std::remove_if(m_projectiles.begin(), m_projectiles.end(),
                    [](const Projectile& p) { return !p.alive; }),
                m_projectiles.end());

            // Update power-ups
            for (auto& pu : m_powerUps) {
                if (!pu.alive) continue;
                pu.update(fdt);

                // Check pickup by players
                for (auto& [pid, player] : m_players) {
                    if (!player.alive) continue;
                    auto pPos = player.body->GetPosition();
                    float dx = pPos.x - pu.position.x;
                    float dy = pPos.y - pu.position.y;
                    float dist = std::sqrt(dx * dx + dy * dy);

                    if (dist < pu.radius + Player::HALF_WIDTH) {
                        // Apply power-up
                        switch (pu.type) {
                            case PowerUpType::Health:
                                player.health = std::min(player.maxHealth, player.health + 40.0f);
                                break;
                            case PowerUpType::SpeedBoost:
                                player.moveSpeed = 12.0f;
                                player.speedBoostTimer = 8.0f;
                                break;
                            case PowerUpType::DamageBoost:
                                player.damage = 18.0f;
                                player.damageBoostTimer = 8.0f;
                                break;
                            case PowerUpType::Shield:
                                player.shieldTimer = 5.0f;
                                break;
                            case PowerUpType::DoubleJump:
                                player.jumpsLeft = 2;
                                break;
                        }

                        sf::Vector2f pickupPos(SCREEN_CX + pu.position.x * PIXELS_PER_METER,
                                               SCREEN_CY + pu.position.y * PIXELS_PER_METER);
                        m_particles->emitPowerUpPickup(pickupPos, pu.getColor());
                        pu.alive = false;
                        break;
                    }
                }
            }
            m_powerUps.erase(
                std::remove_if(m_powerUps.begin(), m_powerUps.end(),
                    [](const PowerUp& p) { return !p.alive; }),
                m_powerUps.end());

            // Spawn power-ups periodically
            m_powerUpSpawnTimer -= dt;
            if (m_powerUpSpawnTimer <= 0) {
                spawnPowerUp();
                std::uniform_real_distribution<double> spawnDist(8.0, 15.0);
                m_powerUpSpawnTimer = spawnDist(m_rng);
            }

            // Round timer
            m_roundTimer -= dt;
            checkRoundEnd();
            break;
        }

        case GamePhase::RoundEnd:
            m_countdownTimer -= dt;
            if (m_countdownTimer <= 0) {
                if (m_roundNumber >= m_maxRounds) {
                    m_phase = GamePhase::GameOver;
                    m_countdownTimer = 15.0;
                } else {
                    // Reset for next round
                    m_phase = GamePhase::Lobby;
                    m_lobbyTimer = 10.0;  // Shorter lobby between rounds
                    // Respawn all players
                    for (auto& [id, player] : m_players) {
                        respawnPlayer(player);
                    }
                }
            }
            break;

        case GamePhase::GameOver:
            m_countdownTimer -= dt;
            if (m_countdownTimer <= 0) {
                // Reset everything for a new game
                m_roundNumber = 0;
                m_leaderboard.clear();
                m_phase = GamePhase::Lobby;
                m_lobbyTimer = m_lobbyDuration;
            }
            break;
    }

    // Update particles
    m_particles->update(fdt);

    // Update kill feed
    for (auto& entry : m_killFeed) {
        entry.timeRemaining -= dt;
    }
    while (!m_killFeed.empty() && m_killFeed.front().timeRemaining <= 0) {
        m_killFeed.pop_front();
    }

    // Update arena
    m_arena->update(dt);
}

void ChaosArena::startCountdown() {
    m_phase = GamePhase::Countdown;
    m_countdownTimer = 3.0;
    spdlog::info("[ChaosArena] Round {} starting in 3...", m_roundNumber + 1);
}

void ChaosArena::startBattle() {
    m_phase = GamePhase::Battle;
    m_roundNumber++;
    m_roundTimer = m_roundDuration;
    m_powerUpSpawnTimer = 5.0;

    // Give all players spawn protection
    for (auto& [id, player] : m_players) {
        player.invulnTimer = 2.0f;
    }

    spdlog::info("[ChaosArena] Round {} – FIGHT! ({} players)", m_roundNumber, m_players.size());
}

void ChaosArena::checkRoundEnd() {
    // Count alive players
    int alive = 0;
    std::string lastAlive;
    for (const auto& [id, player] : m_players) {
        if (player.alive) {
            alive++;
            lastAlive = id;
        }
    }

    bool roundOver = false;
    if (alive <= 1 && m_players.size() > 1) {
        roundOver = true;
        if (alive == 1) {
            auto& winner = m_players[lastAlive];
            spdlog::info("[ChaosArena] Round {} winner: {}!", m_roundNumber, winner.displayName);
            winner.score += 100;
            m_leaderboard[lastAlive].wins++;
            m_leaderboard[lastAlive].totalScore += 100;
        }
    }
    if (m_roundTimer <= 0) {
        roundOver = true;
        spdlog::info("[ChaosArena] Round {} – time's up!", m_roundNumber);
    }

    if (roundOver) {
        endRound();
    }
}

void ChaosArena::endRound() {
    m_phase = GamePhase::RoundEnd;
    m_countdownTimer = 8.0;  // Show results for 8 seconds

    // Clean up projectiles and power-ups
    for (auto& proj : m_projectiles) {
        if (proj.body) m_physics->destroyBody(proj.body);
    }
    m_projectiles.clear();

    for (auto& pu : m_powerUps) {
        if (pu.body) m_physics->destroyBody(pu.body);
    }
    m_powerUps.clear();

    spdlog::info("[ChaosArena] Round {} ended.", m_roundNumber);
}

void ChaosArena::spawnPowerUp() {
    std::uniform_real_distribution<float> xDist(-ARENA_WIDTH / 2 + 3, ARENA_WIDTH / 2 - 3);
    std::uniform_real_distribution<float> yDist(-ARENA_HEIGHT / 2 + 2, ARENA_HEIGHT / 2 - 4);
    std::uniform_int_distribution<int> typeDist(0, 4);

    PowerUp pu;
    pu.position.x = xDist(m_rng);
    pu.position.y = yDist(m_rng);
    pu.type = static_cast<PowerUpType>(typeDist(m_rng));

    m_powerUps.push_back(pu);
}

void ChaosArena::applyDamage(Player& attacker, Player& victim, float damage,
                              const std::string& method) {
    if (victim.isInvulnerable()) return;
    if (victim.blocking) {
        damage *= 0.25f;  // Block reduces damage by 75%
    }

    victim.health -= damage;
    victim.hitFlashTimer = 0.15f;

    if (victim.health <= 0) {
        eliminatePlayer(victim, attacker.displayName, method);
        attacker.kills++;
        attacker.score += 25;
        m_leaderboard[attacker.userId].kills++;
        m_leaderboard[attacker.userId].totalScore += 25;
    }
}

void ChaosArena::eliminatePlayer(Player& player, const std::string& killerName,
                                   const std::string& method) {
    player.alive = false;
    player.deaths++;

    // Death particles
    auto pos = player.body->GetPosition();
    sf::Vector2f screenPos(SCREEN_CX + pos.x * PIXELS_PER_METER, SCREEN_CY + pos.y * PIXELS_PER_METER);
    m_particles->emitDeath(screenPos, player.color);

    // Kill feed
    m_killFeed.push_back({killerName, player.displayName, method, 5.0});
    if (m_killFeed.size() > 8) m_killFeed.pop_front();

    spdlog::info("[ChaosArena] {} eliminated {} via {}!", killerName, player.displayName, method);
}

void ChaosArena::respawnPlayer(Player& player) {
    player.alive = true;
    player.health = player.maxHealth;
    player.invulnTimer = 3.0f;
    player.attackCooldown = 0;
    player.specialCooldown = 0;
    player.dashCooldown = 0;
    player.jumpsLeft = 2;

    if (player.body) {
        std::uniform_real_distribution<float> xDist(-ARENA_WIDTH / 2 + 3, ARENA_WIDTH / 2 - 3);
        float spawnX = xDist(m_rng);
        player.body->SetTransform(b2Vec2(spawnX, -ARENA_HEIGHT / 2 + 3), 0);
        player.body->SetLinearVelocity(b2Vec2(0, 0));
    }
}

sf::Color ChaosArena::generatePlayerColor() {
    std::uniform_int_distribution<int> hue(0, 360);
    // HSV to RGB with high saturation and value
    float h = static_cast<float>(hue(m_rng));
    float s = 0.8f, v = 0.9f;

    float c = v * s;
    float x = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;

    float r, g, b;
    if      (h < 60)  { r = c; g = x; b = 0; }
    else if (h < 120) { r = x; g = c; b = 0; }
    else if (h < 180) { r = 0; g = c; b = x; }
    else if (h < 240) { r = 0; g = x; b = c; }
    else if (h < 300) { r = x; g = 0; b = c; }
    else              { r = c; g = 0; b = x; }

    return sf::Color(
        static_cast<sf::Uint8>((r + m) * 255),
        static_cast<sf::Uint8>((g + m) * 255),
        static_cast<sf::Uint8>((b + m) * 255)
    );
}

// ─── Rendering ───────────────────────────────────────────────────────────────

void ChaosArena::render(sf::RenderTarget& target, double alpha) {
    // Background: stars, nebulae
    m_background.render(target);

    // Arena: grid, platforms, blocks
    renderArena(target);
    renderPowerUps(target);
    renderProjectiles(target);
    renderPlayers(target, alpha);
    renderParticles(target);
    renderUI(target);

    // Post-processing: vignette, scanlines
    m_postProcessing.applyVignette(target, 0.5f);
    m_postProcessing.applyScanlines(target, 0.03f);
}

void ChaosArena::renderArena(sf::RenderTarget& target) {
    m_arena->render(target, PIXELS_PER_METER);
}

void ChaosArena::renderPlayers(sf::RenderTarget& target, double alpha) {
    float cx = static_cast<float>(target.getSize().x) / 2.0f;
    float cy = static_cast<float>(target.getSize().y) / 2.0f;

    for (const auto& [id, player] : m_players) {
        if (!player.alive || !player.body) continue;

        auto pos = player.body->GetPosition();

        // Interpolated position
        float x = cx + pos.x * PIXELS_PER_METER;
        float y = cy + pos.y * PIXELS_PER_METER;

        float pw = Player::HALF_WIDTH * 2 * PIXELS_PER_METER;
        float ph = Player::HALF_HEIGHT * 2 * PIXELS_PER_METER;

        // Player body
        sf::RectangleShape body(sf::Vector2f(pw, ph));
        body.setOrigin(pw / 2, ph / 2);
        body.setPosition(x, y);

        sf::Color bodyColor = player.color;
        if (player.hitFlashTimer > 0) {
            bodyColor = sf::Color::White;
        }
        if (player.isInvulnerable()) {
            bodyColor.a = static_cast<sf::Uint8>(128 + 127 * std::sin(player.animTimer * 10));
        }
        body.setFillColor(bodyColor);

        // Outline glow
        body.setOutlineThickness(2.0f);
        sf::Color outlineColor = player.color;
        outlineColor.a = 100;
        body.setOutlineColor(outlineColor);

        target.draw(body);

        // Eyes (facing direction)
        float eyeOffsetX = player.facingDir * pw * 0.15f;
        sf::CircleShape eye(3.0f);
        eye.setOrigin(3, 3);
        eye.setFillColor(sf::Color::White);
        eye.setPosition(x + eyeOffsetX - 4, y - ph * 0.15f);
        target.draw(eye);
        eye.setPosition(x + eyeOffsetX + 4, y - ph * 0.15f);
        target.draw(eye);

        // Pupils
        sf::CircleShape pupil(1.5f);
        pupil.setOrigin(1.5f, 1.5f);
        pupil.setFillColor(sf::Color::Black);
        pupil.setPosition(x + eyeOffsetX - 4 + player.facingDir * 1.5f, y - ph * 0.15f);
        target.draw(pupil);
        pupil.setPosition(x + eyeOffsetX + 4 + player.facingDir * 1.5f, y - ph * 0.15f);
        target.draw(pupil);

        // Blocking shield visual
        if (player.blocking) {
            sf::CircleShape shield(pw * 0.8f);
            shield.setOrigin(pw * 0.8f, pw * 0.8f);
            shield.setPosition(x, y);
            shield.setFillColor(sf::Color(100, 200, 255, 40));
            shield.setOutlineThickness(2.0f);
            shield.setOutlineColor(sf::Color(100, 200, 255, 150));
            target.draw(shield);
        }

        // Shield timer visual
        if (player.shieldTimer > 0) {
            sf::CircleShape shieldBubble(pw * 0.9f);
            shieldBubble.setOrigin(pw * 0.9f, pw * 0.9f);
            shieldBubble.setPosition(x, y);
            float shieldAlpha = std::min(1.0f, player.shieldTimer);
            shieldBubble.setFillColor(sf::Color(255, 220, 50, static_cast<sf::Uint8>(30 * shieldAlpha)));
            shieldBubble.setOutlineThickness(2.0f);
            shieldBubble.setOutlineColor(sf::Color(255, 220, 50, static_cast<sf::Uint8>(180 * shieldAlpha)));
            target.draw(shieldBubble);
        }

        // Health bar above player
        float barWidth = pw * 1.2f;
        float barHeight = 4.0f;
        float barY = y - ph / 2 - 12;

        sf::RectangleShape barBg(sf::Vector2f(barWidth, barHeight));
        barBg.setOrigin(barWidth / 2, 0);
        barBg.setPosition(x, barY);
        barBg.setFillColor(sf::Color(40, 40, 40, 200));
        target.draw(barBg);

        float healthRatio = player.health / player.maxHealth;
        sf::Color healthColor = healthRatio > 0.5f
            ? sf::Color(50, 220, 50)
            : (healthRatio > 0.25f ? sf::Color(220, 200, 50) : sf::Color(220, 50, 50));
        sf::RectangleShape barFill(sf::Vector2f(barWidth * healthRatio, barHeight));
        barFill.setOrigin(barWidth / 2, 0);
        barFill.setPosition(x, barY);
        barFill.setFillColor(healthColor);
        target.draw(barFill);

        // Name tag above health bar
        if (m_fontLoaded) {
            sf::Text nameTag;
            nameTag.setFont(m_font);
            std::string name = player.displayName;
            if (name.length() > 12) name = name.substr(0, 10) + "..";
            nameTag.setString(name);
            nameTag.setCharacterSize(10);
            nameTag.setFillColor(sf::Color(255, 255, 255, 200));
            auto nBounds = nameTag.getLocalBounds();
            nameTag.setOrigin(nBounds.left + nBounds.width / 2, nBounds.top + nBounds.height);
            nameTag.setPosition(x, barY - 4);
            target.draw(nameTag);
        }
    }
}

void ChaosArena::renderProjectiles(sf::RenderTarget& target) {
    float cx = static_cast<float>(target.getSize().x) / 2.0f;
    float cy = static_cast<float>(target.getSize().y) / 2.0f;

    for (const auto& proj : m_projectiles) {
        if (!proj.alive || !proj.body) continue;

        auto pos = proj.body->GetPosition();
        float x = cx + pos.x * PIXELS_PER_METER;
        float y = cy + pos.y * PIXELS_PER_METER;
        float r = proj.radius * PIXELS_PER_METER;

        // Glow
        sf::CircleShape glow(r * 2.5f);
        glow.setOrigin(r * 2.5f, r * 2.5f);
        glow.setPosition(x, y);
        sf::Color glowColor = proj.color;
        glowColor.a = 60;
        glow.setFillColor(glowColor);
        target.draw(glow);

        // Core
        sf::CircleShape core(r);
        core.setOrigin(r, r);
        core.setPosition(x, y);
        core.setFillColor(sf::Color::White);
        core.setOutlineThickness(2.0f);
        core.setOutlineColor(proj.color);
        target.draw(core);
    }
}

void ChaosArena::renderPowerUps(sf::RenderTarget& target) {
    float cx = static_cast<float>(target.getSize().x) / 2.0f;
    float cy = static_cast<float>(target.getSize().y) / 2.0f;

    for (const auto& pu : m_powerUps) {
        if (!pu.alive) continue;

        float x = cx + pu.position.x * PIXELS_PER_METER;
        float y = cy + pu.position.y * PIXELS_PER_METER + std::sin(pu.bobTimer) * 5.0f;
        float r = pu.radius * PIXELS_PER_METER;

        // Pulsing glow
        float glowSize = r * 2.0f + std::sin(pu.glowTimer) * r * 0.5f;
        sf::CircleShape glow(glowSize);
        glow.setOrigin(glowSize, glowSize);
        glow.setPosition(x, y);
        sf::Color glowColor = pu.getColor();
        glowColor.a = 40;
        glow.setFillColor(glowColor);
        target.draw(glow);

        // Diamond shape (rotated square)
        sf::RectangleShape diamond(sf::Vector2f(r * 1.4f, r * 1.4f));
        diamond.setOrigin(r * 0.7f, r * 0.7f);
        diamond.setPosition(x, y);
        diamond.setRotation(45.0f + pu.bobTimer * 30.0f);
        diamond.setFillColor(pu.getColor());
        diamond.setOutlineThickness(2.0f);
        diamond.setOutlineColor(sf::Color::White);
        target.draw(diamond);
    }
}

void ChaosArena::renderParticles(sf::RenderTarget& target) {
    m_particles->render(target);
}

void ChaosArena::renderUI(sf::RenderTarget& target) {
    renderKillFeed(target);
    renderLeaderboard(target);

    if (m_phase == GamePhase::Countdown) {
        renderCountdown(target);
    }

    // Phase-specific UI
    auto size = target.getSize();

    // Round info bar at top
    sf::RectangleShape topBar(sf::Vector2f(static_cast<float>(size.x), 40.0f));
    topBar.setFillColor(sf::Color(0, 0, 0, 160));
    target.draw(topBar);

    // Top bar text
    if (m_fontLoaded) {
        // Game title
        sf::Text titleText;
        titleText.setFont(m_font);
        titleText.setString("CHAOS ARENA");
        titleText.setCharacterSize(18);
        titleText.setStyle(sf::Text::Bold);
        titleText.setFillColor(sf::Color(100, 200, 255));
        titleText.setPosition(15, 8);
        target.draw(titleText);

        // Phase indicator
        std::string phaseStr;
        switch (m_phase) {
            case GamePhase::Lobby:     phaseStr = "LOBBY"; break;
            case GamePhase::Countdown: phaseStr = "GET READY"; break;
            case GamePhase::Battle:    phaseStr = "BATTLE"; break;
            case GamePhase::RoundEnd:  phaseStr = "ROUND END"; break;
            case GamePhase::GameOver:  phaseStr = "GAME OVER"; break;
        }
        sf::Text phaseText;
        phaseText.setFont(m_font);
        phaseText.setString(phaseStr);
        phaseText.setCharacterSize(16);
        phaseText.setFillColor(sf::Color(255, 220, 100));
        auto pBounds = phaseText.getLocalBounds();
        phaseText.setPosition(size.x / 2.0f - pBounds.width / 2, 10);
        target.draw(phaseText);

        // Round / Player count
        std::string infoStr = "Round " + std::to_string(m_roundNumber) + "/" +
            std::to_string(m_maxRounds) + "  |  Players: " +
            std::to_string(m_players.size());
        sf::Text infoText;
        infoText.setFont(m_font);
        infoText.setString(infoStr);
        infoText.setCharacterSize(14);
        infoText.setFillColor(sf::Color(180, 180, 200));
        auto iBounds = infoText.getLocalBounds();
        infoText.setPosition(size.x - iBounds.width - 15, 12);
        target.draw(infoText);
    }

    // Bottom status bar
    sf::RectangleShape bottomBar(sf::Vector2f(static_cast<float>(size.x), 30.0f));
    bottomBar.setPosition(0, static_cast<float>(size.y) - 30.0f);
    bottomBar.setFillColor(sf::Color(0, 0, 0, 160));
    target.draw(bottomBar);

    if (m_fontLoaded) {
        sf::Text cmdHint;
        cmdHint.setFont(m_font);
        cmdHint.setString("!join  !left  !right  !jump  !attack  !special  !dash  !block");
        cmdHint.setCharacterSize(12);
        cmdHint.setFillColor(sf::Color(140, 140, 160));
        auto cBounds = cmdHint.getLocalBounds();
        cmdHint.setPosition(size.x / 2.0f - cBounds.width / 2, size.y - 24.0f);
        target.draw(cmdHint);
    }

    // Round timer indicator (for Battle phase)
    if (m_phase == GamePhase::Battle && m_roundDuration > 0) {
        float timerRatio = static_cast<float>(m_roundTimer / m_roundDuration);
        sf::RectangleShape timerBar(sf::Vector2f(static_cast<float>(size.x) * timerRatio, 4.0f));
        timerBar.setPosition(0, 40.0f);
        sf::Color timerColor = timerRatio > 0.3f
            ? sf::Color(50, 200, 255) : sf::Color(255, 80, 50);
        timerBar.setFillColor(timerColor);
        target.draw(timerBar);

        // Timer text
        if (m_fontLoaded) {
            int seconds = static_cast<int>(m_roundTimer);
            int min = seconds / 60;
            int sec = seconds % 60;
            std::string timeStr = std::to_string(min) + ":" +
                (sec < 10 ? "0" : "") + std::to_string(sec);
            sf::Text timerText;
            timerText.setFont(m_font);
            timerText.setString(timeStr);
            timerText.setCharacterSize(20);
            timerText.setStyle(sf::Text::Bold);
            timerText.setFillColor(timerColor);
            auto tBounds = timerText.getLocalBounds();
            timerText.setPosition(size.x / 2.0f - tBounds.width / 2, 44.0f);
            target.draw(timerText);
        }
    }

    // Lobby waiting indicator
    if (m_phase == GamePhase::Lobby) {
        // Pulsing "waiting" indicator
        float pulse = static_cast<float>(std::sin(m_lobbyTimer * 2.0) * 0.3 + 0.7);
        float boxW = std::min(500.0f, size.x * 0.85f);
        sf::RectangleShape waitBox(sf::Vector2f(boxW, 120));
        waitBox.setOrigin(boxW / 2.0f, 60);
        waitBox.setPosition(size.x / 2.0f, size.y / 2.0f);
        waitBox.setFillColor(sf::Color(0, 0, 0, static_cast<sf::Uint8>(200 * pulse)));
        waitBox.setOutlineThickness(2.0f);
        waitBox.setOutlineColor(sf::Color(100, 200, 255, static_cast<sf::Uint8>(200 * pulse)));
        target.draw(waitBox);

        if (m_fontLoaded) {
            sf::Text waitTitle;
            waitTitle.setFont(m_font);
            waitTitle.setString("WAITING FOR PLAYERS");
            waitTitle.setCharacterSize(22);
            waitTitle.setStyle(sf::Text::Bold);
            waitTitle.setFillColor(sf::Color(100, 200, 255, static_cast<sf::Uint8>(255 * pulse)));
            auto wBounds = waitTitle.getLocalBounds();
            waitTitle.setPosition(size.x / 2.0f - wBounds.width / 2, size.y / 2.0f - 45);
            target.draw(waitTitle);

            std::string joinStr = "Type !join in chat to play";
            sf::Text joinText;
            joinText.setFont(m_font);
            joinText.setString(joinStr);
            joinText.setCharacterSize(14);
            joinText.setFillColor(sf::Color(180, 180, 200, static_cast<sf::Uint8>(220 * pulse)));
            auto jBounds = joinText.getLocalBounds();
            joinText.setPosition(size.x / 2.0f - jBounds.width / 2, size.y / 2.0f - 5);
            target.draw(joinText);

            std::string countStr = std::to_string(m_players.size()) + " / " +
                std::to_string(m_minPlayers) + " players needed";
            sf::Text countText;
            countText.setFont(m_font);
            countText.setString(countStr);
            countText.setCharacterSize(14);
            countText.setFillColor(sf::Color(255, 220, 100, static_cast<sf::Uint8>(220 * pulse)));
            auto cBounds = countText.getLocalBounds();
            countText.setPosition(size.x / 2.0f - cBounds.width / 2, size.y / 2.0f + 22);
            target.draw(countText);
        }
    }
}

void ChaosArena::renderKillFeed(sf::RenderTarget& target) {
    auto size = target.getSize();
    float y = 50.0f;

    for (const auto& entry : m_killFeed) {
        float alpha = std::min(1.0f, static_cast<float>(entry.timeRemaining));
        sf::RectangleShape bg(sf::Vector2f(300, 24));
        bg.setPosition(static_cast<float>(size.x) - 310, y);
        bg.setFillColor(sf::Color(0, 0, 0, static_cast<sf::Uint8>(140 * alpha)));
        target.draw(bg);

        if (m_fontLoaded) {
            std::string feedStr = entry.killer + " [" + entry.method + "] " + entry.victim;
            sf::Text feedText;
            feedText.setFont(m_font);
            feedText.setString(feedStr);
            feedText.setCharacterSize(11);
            feedText.setFillColor(sf::Color(255, 255, 255, static_cast<sf::Uint8>(220 * alpha)));
            feedText.setPosition(static_cast<float>(size.x) - 305, y + 3);
            target.draw(feedText);
        }

        y += 28.0f;
    }
}

void ChaosArena::renderLeaderboard(sf::RenderTarget& target) {
    if (m_players.empty()) return;

    // Background panel on the left
    float panelWidth = 220.0f;
    float entryHeight = 22.0f;
    float panelHeight = std::min(static_cast<float>(m_players.size()), 10.0f) * entryHeight + 40;

    sf::RectangleShape panel(sf::Vector2f(panelWidth, panelHeight));
    panel.setPosition(10, 50);
    panel.setFillColor(sf::Color(0, 0, 0, 140));
    panel.setOutlineThickness(1.0f);
    panel.setOutlineColor(sf::Color(100, 100, 140, 100));
    target.draw(panel);

    // Header
    if (m_fontLoaded) {
        sf::Text header;
        header.setFont(m_font);
        header.setString("LEADERBOARD");
        header.setCharacterSize(11);
        header.setStyle(sf::Text::Bold);
        header.setFillColor(sf::Color(100, 200, 255));
        header.setPosition(16, 54);
        target.draw(header);
    }

    // Player entries (sorted by score)
    struct Entry { std::string name; int score; sf::Color color; bool alive; };
    std::vector<Entry> entries;
    for (const auto& [id, p] : m_players) {
        entries.push_back({p.displayName, p.score, p.color, p.alive});
    }
    std::sort(entries.begin(), entries.end(),
        [](const auto& a, const auto& b) { return a.score > b.score; });

    float y = 74.0f;
    int rank = 1;
    for (const auto& e : entries) {
        if (rank > 10) break;

        // Color indicator
        sf::RectangleShape colorBar(sf::Vector2f(4, entryHeight - 4));
        colorBar.setPosition(14, y);
        colorBar.setFillColor(e.alive ? e.color : sf::Color(80, 80, 80));
        target.draw(colorBar);

        // Player name and score text
        if (m_fontLoaded) {
            sf::Text nameText;
            nameText.setFont(m_font);
            std::string nameStr = std::to_string(rank) + ". " + e.name;
            if (nameStr.length() > 16) nameStr = nameStr.substr(0, 14) + "..";
            nameText.setString(nameStr);
            nameText.setCharacterSize(11);
            nameText.setFillColor(e.alive ? sf::Color(220, 220, 230) : sf::Color(100, 100, 100));
            nameText.setPosition(24, y + 1);
            target.draw(nameText);

            sf::Text scoreText;
            scoreText.setFont(m_font);
            scoreText.setString(std::to_string(e.score));
            scoreText.setCharacterSize(11);
            scoreText.setFillColor(sf::Color(255, 220, 100));
            auto sBounds = scoreText.getLocalBounds();
            scoreText.setPosition(panelWidth - sBounds.width - 15, y + 1);
            target.draw(scoreText);
        }

        // Alive indicator dot
        if (e.alive) {
            sf::CircleShape dot(3);
            dot.setPosition(panelWidth - 5, y + 7);
            dot.setFillColor(sf::Color(50, 220, 50));
            target.draw(dot);
        }

        y += entryHeight;
        rank++;
    }
}

void ChaosArena::renderCountdown(sf::RenderTarget& target) {
    auto size = target.getSize();

    int countdown = static_cast<int>(std::ceil(m_countdownTimer));

    // Large countdown circle
    float radius = 60.0f;
    sf::CircleShape circle(radius);
    circle.setOrigin(radius, radius);
    circle.setPosition(size.x / 2.0f, size.y / 2.0f);
    circle.setFillColor(sf::Color(0, 0, 0, 180));
    circle.setOutlineThickness(3.0f);

    float pulse = static_cast<float>(std::fmod(m_countdownTimer, 1.0));
    sf::Color ringColor(255, 200, 50, static_cast<sf::Uint8>(255 * pulse));
    circle.setOutlineColor(ringColor);
    target.draw(circle);

    // Countdown number
    if (m_fontLoaded && countdown > 0) {
        sf::Text countText;
        countText.setFont(m_font);
        countText.setString(std::to_string(countdown));
        countText.setCharacterSize(60);
        countText.setStyle(sf::Text::Bold);
        countText.setFillColor(sf::Color(255, 220, 100, static_cast<sf::Uint8>(255 * pulse)));
        auto cBounds = countText.getLocalBounds();
        countText.setOrigin(cBounds.left + cBounds.width / 2, cBounds.top + cBounds.height / 2);
        countText.setPosition(size.x / 2.0f, size.y / 2.0f);
        target.draw(countText);
    }
}

// ─── State / Commands ────────────────────────────────────────────────────────

bool ChaosArena::isRoundComplete() const {
    return m_phase == GamePhase::RoundEnd || m_phase == GamePhase::GameOver
        || m_phase == GamePhase::Lobby;
}

bool ChaosArena::isGameOver() const {
    return m_phase == GamePhase::GameOver;
}

nlohmann::json ChaosArena::getState() const {
    nlohmann::json state;
    state["phase"] = static_cast<int>(m_phase);
    state["round"] = m_roundNumber;
    state["maxRounds"] = m_maxRounds;
    state["roundTimer"] = m_roundTimer;
    state["playerCount"] = m_players.size();

    nlohmann::json players = nlohmann::json::array();
    for (const auto& [id, p] : m_players) {
        nlohmann::json pj;
        pj["id"] = id;
        pj["name"] = p.displayName;
        pj["health"] = p.health;
        pj["alive"] = p.alive;
        pj["kills"] = p.kills;
        pj["deaths"] = p.deaths;
        pj["score"] = p.score;
        players.push_back(pj);
    }
    state["players"] = players;

    nlohmann::json lb = nlohmann::json::array();
    for (const auto& [id, entry] : m_leaderboard) {
        nlohmann::json e;
        e["id"] = id;
        e["name"] = entry.displayName;
        e["kills"] = entry.kills;
        e["wins"] = entry.wins;
        e["score"] = entry.totalScore;
        lb.push_back(e);
    }
    state["leaderboard"] = lb;
    state["particles"] = m_particles->activeCount();

    return state;
}

nlohmann::json ChaosArena::getCommands() const {
    return nlohmann::json::array({
        {{"command", "!join"}, {"description", "Join the game"}, {"aliases", nlohmann::json::array({"!play"})}},
        {{"command", "!left"}, {"description", "Move left"}, {"aliases", nlohmann::json::array({"!l", "!a"})}},
        {{"command", "!right"}, {"description", "Move right"}, {"aliases", nlohmann::json::array({"!r", "!d"})}},
        {{"command", "!jump"}, {"description", "Jump straight up"}, {"aliases", nlohmann::json::array({"!j", "!w", "!up"})}},
        {{"command", "!jumpleft"}, {"description", "Jump to the left"}, {"aliases", nlohmann::json::array({"!jl"})}},
        {{"command", "!jumpright"}, {"description", "Jump to the right"}, {"aliases", nlohmann::json::array({"!jr"})}},
        {{"command", "!attack"}, {"description", "Melee attack"}, {"aliases", nlohmann::json::array({"!hit", "!atk"})}},
        {{"command", "!special"}, {"description", "Fire projectile (5s cooldown)"}, {"aliases", nlohmann::json::array({"!sp", "!ult"})}},
        {{"command", "!dash"}, {"description", "Quick dash (3s cooldown)"}, {"aliases", nlohmann::json::array({"!dodge"})}},
        {{"command", "!block"}, {"description", "Block (reduces damage 75%)"}, {"aliases", nlohmann::json::array({"!shield", "!def"})}},
    });
}

} // namespace is::games::chaos_arena
