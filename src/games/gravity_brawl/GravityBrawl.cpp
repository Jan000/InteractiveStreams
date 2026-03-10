#include "games/gravity_brawl/GravityBrawl.h"
#include "games/GameRegistry.h"
#include "core/Application.h"
#include "core/AudioManager.h"
#include "core/PlayerDatabase.h"

#include <spdlog/spdlog.h>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <filesystem>

namespace is::games::gravity_brawl {

// Helper: check if a userId represents a bot
static inline bool isBot(const std::string& userId) {
    return userId.size() >= 6 && userId.substr(0, 6) == "__bot_";
}

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
    // Center of screen maps to WORLD_CENTER, scaled by dynamic camera zoom
    float ppm = PIXELS_PER_METER * m_cameraZoom;
    float sx = SCREEN_W / 2.0f + (wx - WORLD_CENTER_X) * ppm;
    float sy = SCREEN_H / 2.0f + (wy - WORLD_CENTER_Y) * ppm;
    return {sx, sy};
}

sf::Vector2f GravityBrawl::worldToScreen(b2Vec2 pos) const {
    return worldToScreen(pos.x, pos.y);
}

// ── Initialize / Shutdown ────────────────────────────────────────────────────

void GravityBrawl::initialize() {
    spdlog::info("[GravityBrawl] Initializing...");

    // Register configurable text elements (id, label, x%, y%, fontSize, align, visible)
    if (m_textElements.empty()) {
        //                          id                  label                        x%     y%    fs   align
        registerTextElement("title",           "Game Title",                  50.f,  0.8f, 22, TextAlign::Center);
        registerTextElement("info_text",       "Info / Status",               50.f,  2.3f, 13, TextAlign::Center);
        registerTextElement("join_hint",       "Join Hint (bottom)",          50.f, 97.9f, 11, TextAlign::Center);
        registerTextElement("event_warning",   "Black Hole Warning Bar",      50.f,  3.6f, 12, TextAlign::Center);
        registerTextElement("cosmic_warning",  "Cosmic Event Warning",        50.f,  3.9f, 16, TextAlign::Center);
        registerTextElement("cosmic_timer",    "Cosmic Timer (near BH)",      50.f, 25.0f, 24, TextAlign::Center);
        registerTextElement("countdown",       "Countdown Number",            50.f, 30.0f, 80, TextAlign::Center);
        registerTextElement("kill_feed",       "Kill Feed",                   98.6f, 5.2f, 11, TextAlign::Right);
        registerTextElement("player_name",     "Player Name Label",           50.f, 50.f,  11, TextAlign::Center);
        registerTextElement("player_score",    "Player Score Label",          50.f, 50.f,   9, TextAlign::Center);
        registerTextElement("floating_text",   "Floating Score Text",         50.f, 50.f,  16, TextAlign::Center);
    }

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

    // Load sound effects (gracefully skips missing files)
    loadSfx();

    m_phase = GamePhase::Lobby;
    m_lobbyTimer = m_lobbyDuration;
    m_gameTimer = 0.0;
    m_cosmicEventTimer = m_cosmicEventCooldown;
    m_cosmicEventActive = 0.0;
    m_blackHoleConsumedGravityBonus = 0.0f;
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
    m_botRespawnTimers.clear();
    m_contactListener.reset();
    m_world.reset();
}

// ── Sound Effects ────────────────────────────────────────────────────────────

void GravityBrawl::loadSfx() {
    if (m_sfxLoaded) return;
    m_sfxLoaded = true;

    const std::string sfxDir = "assets/audio/sfx/gravity_brawl/";
    const std::vector<std::string> names = {
        "gb_join", "gb_smash", "gb_supernova", "gb_hit",
        "gb_death", "gb_kill", "gb_cosmic_event", "gb_cosmic_end",
        "gb_countdown", "gb_battle_start", "gb_game_over", "gb_bounty"
    };
    const std::vector<std::string> extensions = {".wav", ".ogg", ".mp3"};

    int loaded = 0;
    auto& audio = is::core::Application::instance().audioManager();

    for (const auto& name : names) {
        // Check for a subdirectory with multiple variants first
        std::string variantDir = sfxDir + name;
        if (std::filesystem::is_directory(variantDir)) {
            if (audio.loadSfxFromDirectory(name, variantDir)) {
                loaded++;
            }
            continue;
        }
        // Fall back to single file
        bool found = false;
        for (const auto& ext : extensions) {
            std::string path = sfxDir + name + ext;
            if (std::filesystem::exists(path)) {
                if (audio.loadSfx(name, path)) {
                    loaded++;
                    found = true;
                }
                break; // Only try one matching extension
            }
        }
        if (!found) {
            spdlog::debug("[GravityBrawl] SFX '{}' not found in {} (skipped)", name, sfxDir);
        }
    }

    spdlog::info("[GravityBrawl] Loaded {}/{} sound effects from {}", loaded, names.size(), sfxDir);
}

void GravityBrawl::playSfx(const std::string& name, float volumeScale) {
    if (!m_sfxEnabled) return;
    try {
        float vol = m_sfxVolume * volumeScale;
        is::core::Application::instance().audioManager().playSfx(name, vol);
    } catch (...) {}
}

// ── Configuration ────────────────────────────────────────────────────────────

void GravityBrawl::configure(const nlohmann::json& settings) {
    if (settings.contains("bot_fill") && settings["bot_fill"].is_number_integer()) {
        m_botFillTarget = settings["bot_fill"].get<int>();
        spdlog::info("[GravityBrawl] Bot fill target set to {}", m_botFillTarget);
    }
    if (settings.contains("game_duration") && settings["game_duration"].is_number()) {
        m_gameDuration = settings["game_duration"].get<double>();
    }
    if (settings.contains("lobby_duration") && settings["lobby_duration"].is_number()) {
        m_lobbyDuration = settings["lobby_duration"].get<double>();
    }
    if (settings.contains("min_players") && settings["min_players"].is_number_integer()) {
        m_minPlayers = settings["min_players"].get<int>();
    }
    if (settings.contains("cosmic_event_cooldown") && settings["cosmic_event_cooldown"].is_number()) {
        m_cosmicEventCooldown = settings["cosmic_event_cooldown"].get<double>();
    }
    if (settings.contains("spawn_radius_factor") && settings["spawn_radius_factor"].is_number()) {
        m_spawnRadiusFactor = std::max(0.1f, settings["spawn_radius_factor"].get<float>());
    }
    if (settings.contains("spawn_orbit_speed") && settings["spawn_orbit_speed"].is_number()) {
        m_spawnOrbitSpeed = std::max(0.0f, settings["spawn_orbit_speed"].get<float>());
    }
    if (settings.contains("safe_orbit_radius_factor") && settings["safe_orbit_radius_factor"].is_number()) {
        m_safeOrbitRadiusFactor = std::max(0.1f, settings["safe_orbit_radius_factor"].get<float>());
    }
    if (settings.contains("orbital_gravity_strength") && settings["orbital_gravity_strength"].is_number()) {
        m_orbitalGravityStrength = std::max(0.0f, settings["orbital_gravity_strength"].get<float>());
    }
    if (settings.contains("orbital_outer_pull_multiplier") && settings["orbital_outer_pull_multiplier"].is_number()) {
        m_orbitalOuterPullMultiplier = std::max(0.0f, settings["orbital_outer_pull_multiplier"].get<float>());
    }
    if (settings.contains("orbital_safe_zone_pull_multiplier") && settings["orbital_safe_zone_pull_multiplier"].is_number()) {
        m_orbitalSafeZonePullMultiplier = std::max(0.0f, settings["orbital_safe_zone_pull_multiplier"].get<float>());
    }
    if (settings.contains("orbital_tangential_strength") && settings["orbital_tangential_strength"].is_number()) {
        m_orbitalTangentialStrength = std::max(0.0f, settings["orbital_tangential_strength"].get<float>());
    }
    if (settings.contains("black_hole_gravity_strength") && settings["black_hole_gravity_strength"].is_number()) {
        m_blackHoleBaseGravity = std::max(0.0f, settings["black_hole_gravity_strength"].get<float>());
    }
    if (settings.contains("black_hole_time_growth_factor") && settings["black_hole_time_growth_factor"].is_number()) {
        m_blackHoleTimeGrowthFactor = std::max(0.0f, settings["black_hole_time_growth_factor"].get<float>());
    }
    if (settings.contains("black_hole_consume_size_factor") && settings["black_hole_consume_size_factor"].is_number()) {
        m_blackHoleConsumeSizeFactor = std::max(0.0f, settings["black_hole_consume_size_factor"].get<float>());
    }
    if (settings.contains("black_hole_gravity_cap") && settings["black_hole_gravity_cap"].is_number()) {
        m_blackHoleGravityCap = std::max(0.0f, settings["black_hole_gravity_cap"].get<float>());
    }
    if (settings.contains("black_hole_kill_radius_multiplier") && settings["black_hole_kill_radius_multiplier"].is_number()) {
        m_blackHoleKillRadiusMultiplier = std::max(0.1f, settings["black_hole_kill_radius_multiplier"].get<float>());
    }
    if (settings.contains("event_gravity_multiplier") && settings["event_gravity_multiplier"].is_number()) {
        m_eventGravityMul = std::max(0.0f, settings["event_gravity_multiplier"].get<float>());
    }
    // Sound effect settings
    if (settings.contains("sfx_enabled") && settings["sfx_enabled"].is_boolean()) {
        m_sfxEnabled = settings["sfx_enabled"].get<bool>();
    }
    if (settings.contains("sfx_volume") && settings["sfx_volume"].is_number()) {
        m_sfxVolume = std::clamp(settings["sfx_volume"].get<float>(), 0.0f, 100.0f);
    }
    // Camera zoom settings
    if (settings.contains("camera_zoom_enabled") && settings["camera_zoom_enabled"].is_boolean()) {
        m_cameraZoomEnabled = settings["camera_zoom_enabled"].get<bool>();
    }
    if (settings.contains("camera_zoom_speed") && settings["camera_zoom_speed"].is_number()) {
        m_cameraZoomSpeed = std::max(0.1f, settings["camera_zoom_speed"].get<float>());
    }
    if (settings.contains("camera_buffer_meters") && settings["camera_buffer_meters"].is_number()) {
        m_cameraBufferMeters = std::max(0.0f, settings["camera_buffer_meters"].get<float>());
    }
    if (settings.contains("camera_min_zoom") && settings["camera_min_zoom"].is_number()) {
        m_cameraMinZoom = std::clamp(settings["camera_min_zoom"].get<float>(), 0.1f, 1.0f);
    }
    if (settings.contains("camera_max_zoom") && settings["camera_max_zoom"].is_number()) {
        m_cameraMaxZoom = std::clamp(settings["camera_max_zoom"].get<float>(), 1.0f, 5.0f);
    }
    // Bot behavior settings
    if (settings.contains("bot_kill_feed") && settings["bot_kill_feed"].is_boolean()) {
        m_botKillFeed = settings["bot_kill_feed"].get<bool>();
    }
    if (settings.contains("bot_respawn") && settings["bot_respawn"].is_boolean()) {
        m_botRespawn = settings["bot_respawn"].get<bool>();
    }
    if (settings.contains("bot_respawn_delay") && settings["bot_respawn_delay"].is_number()) {
        m_botRespawnDelay = std::max(0.0f, settings["bot_respawn_delay"].get<float>());
    }
    if (settings.contains("bot_action_interval") && settings["bot_action_interval"].is_number()) {
        m_botActionInterval = std::max(0.05f, settings["bot_action_interval"].get<float>());
    }
    if (settings.contains("bot_smash_chance") && settings["bot_smash_chance"].is_number()) {
        m_botSmashChance = std::clamp(settings["bot_smash_chance"].get<float>(), 0.0f, 1.0f);
    }
    if (settings.contains("bot_danger_smash_chance") && settings["bot_danger_smash_chance"].is_number()) {
        m_botDangerSmashChance = std::clamp(settings["bot_danger_smash_chance"].get<float>(), 0.0f, 1.0f);
    }
    if (settings.contains("bot_event_smash_chance") && settings["bot_event_smash_chance"].is_number()) {
        m_botEventSmashChance = std::clamp(settings["bot_event_smash_chance"].get<float>(), 0.0f, 1.0f);
    }
    // Text element overrides
    if (settings.contains("text_elements") && settings["text_elements"].is_array()) {
        applyTextOverrides(settings["text_elements"]);
    }
}

nlohmann::json GravityBrawl::getSettings() const {
    return {
        {"bot_fill", m_botFillTarget},
        {"game_duration", m_gameDuration},
        {"lobby_duration", m_lobbyDuration},
        {"min_players", m_minPlayers},
        {"cosmic_event_cooldown", m_cosmicEventCooldown},
        {"spawn_radius_factor", m_spawnRadiusFactor},
        {"spawn_orbit_speed", m_spawnOrbitSpeed},
        {"safe_orbit_radius_factor", m_safeOrbitRadiusFactor},
        {"orbital_gravity_strength", m_orbitalGravityStrength},
        {"orbital_outer_pull_multiplier", m_orbitalOuterPullMultiplier},
        {"orbital_safe_zone_pull_multiplier", m_orbitalSafeZonePullMultiplier},
        {"orbital_tangential_strength", m_orbitalTangentialStrength},
        {"black_hole_gravity_strength", m_blackHoleBaseGravity},
        {"black_hole_time_growth_factor", m_blackHoleTimeGrowthFactor},
        {"black_hole_consume_size_factor", m_blackHoleConsumeSizeFactor},
        {"black_hole_gravity_cap", m_blackHoleGravityCap},
        {"black_hole_kill_radius_multiplier", m_blackHoleKillRadiusMultiplier},
        {"event_gravity_multiplier", m_eventGravityMul},
        {"sfx_enabled", m_sfxEnabled},
        {"sfx_volume", m_sfxVolume},
        {"camera_zoom_enabled", m_cameraZoomEnabled},
        {"camera_zoom_speed", m_cameraZoomSpeed},
        {"camera_buffer_meters", m_cameraBufferMeters},
        {"camera_min_zoom", m_cameraMinZoom},
        {"camera_max_zoom", m_cameraMaxZoom},
        {"bot_kill_feed", m_botKillFeed},
        {"bot_respawn", m_botRespawn},
        {"bot_respawn_delay", m_botRespawnDelay},
        {"bot_action_interval", m_botActionInterval},
        {"bot_smash_chance", m_botSmashChance},
        {"bot_danger_smash_chance", m_botDangerSmashChance},
        {"bot_event_smash_chance", m_botEventSmashChance},
        {"text_elements", textElementsJson()}
    };
}

// ── Bot Fill ─────────────────────────────────────────────────────────────────

static const char* BOT_NAMES[] = {
    "Nova", "Pulsar", "Quasar", "Nebula", "Comet",
    "Vortex", "Cosmo", "Astro", "Eclipse", "Zenith",
    "Photon", "Plasma", "Orion", "Sirius", "Andromeda",
    "Blazar", "Lumen", "Flux", "Ion", "Meteor"
};
static constexpr int NUM_BOT_NAMES = sizeof(BOT_NAMES) / sizeof(BOT_NAMES[0]);

void GravityBrawl::spawnBots() {
    if (m_botFillTarget <= 0) return;

    int currentAlive = 0;
    for (const auto& [_, p] : m_planets) {
        if (p.alive) currentAlive++;
    }

    int needed = m_botFillTarget - currentAlive;
    for (int i = 0; i < needed; ++i) {
        m_botCounter++;
        std::string botId = "__bot_" + std::to_string(m_botCounter);

        Planet& p = m_planets[botId];
        p.userId = botId;
        p.displayName = BOT_NAMES[(m_botCounter - 1) % NUM_BOT_NAMES];
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

        spawnPlanetBody(p);
    }

    if (needed > 0) {
        spdlog::info("[GravityBrawl] Spawned {} bots (total: {})", needed, currentAlive + needed);
    }
}

void GravityBrawl::updateBotAI(float dt) {
    if (m_botFillTarget <= 0) return;

    m_botAITimer -= dt;
    if (m_botAITimer > 0.0f) return;
    m_botAITimer = m_botActionInterval;

    std::uniform_real_distribution<float> chance(0.0f, 1.0f);

    for (auto& [id, p] : m_planets) {
        if (!p.alive || !p.body) continue;
        if (id.substr(0, 6) != "__bot_") continue;

        b2Vec2 pos = p.body->GetPosition();
        float dx = pos.x - WORLD_CENTER_X;
        float dy = pos.y - WORLD_CENTER_Y;
        float distToCenter = std::sqrt(dx * dx + dy * dy);
        float safeOrbit = ARENA_RADIUS * m_safeOrbitRadiusFactor;

        // Priority 1: During cosmic events, spam smash to escape (builds supernova too)
        if (m_cosmicEventActive > 0.0) {
            if (chance(m_rng) < m_botEventSmashChance) {
                cmdSmash(id);
            }
            continue;
        }

        // Priority 2: If dangerously close to black hole, smash to escape
        if (distToCenter < safeOrbit * 0.5f) {
            if (chance(m_rng) < m_botDangerSmashChance) {
                cmdSmash(id);
            }
            continue;
        }

        // Priority 3: If within combo window, keep pressing to reach supernova
        if (p.comboCount >= 3 && m_gameTimer < p.comboWindowEnd) {
            cmdSmash(id);
            continue;
        }

        // Normal behavior: smash with probability based on proximity
        float sc = m_botSmashChance;
        if (distToCenter < safeOrbit * 0.7f) sc = std::min(1.0f, sc * 1.75f);
        if (chance(m_rng) < sc) {
            cmdSmash(id);
        }
    }
}

void GravityBrawl::respawnDeadBots(float dt) {
    if (!m_botRespawn || m_botFillTarget <= 0) return;
    if (m_phase != GamePhase::Playing) return;

    // Collect dead bots that need respawn tracking
    for (auto& [id, p] : m_planets) {
        if (!p.isBot() || p.alive) continue;
        if (m_botRespawnTimers.find(id) == m_botRespawnTimers.end()) {
            m_botRespawnTimers[id] = m_botRespawnDelay;
        }
    }

    // Tick timers and respawn when ready
    for (auto it = m_botRespawnTimers.begin(); it != m_botRespawnTimers.end();) {
        it->second -= dt;
        if (it->second <= 0.0f) {
            auto pit = m_planets.find(it->first);
            if (pit != m_planets.end() && !pit->second.alive) {
                Planet& p = pit->second;
                p.alive = true;
                p.kills = 0;
                p.hitCount = 0;
                p.smashCooldown = 0.0f;
                p.comboCount = 0;
                p.comboWindowEnd = 0.0;
                p.lastHitBy.clear();
                p.lastHitTime = 0.0;
                p.isKing = false;
                p.hitFlashTimer = 0.0f;
                p.trailTimer = 0.0f;
                p.supernovaTimer = 0.0f;
                p.tier = PlanetTier::Asteroid;
                p.baseRadius = 0.5f;
                p.radiusMeters = 0.5f;
                spawnPlanetBody(p);
            }
            it = m_botRespawnTimers.erase(it);
        } else {
            ++it;
        }
    }
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
    // Block joins after game ends
    if (m_phase == GamePhase::GameOver) return;

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
    playSfx("gb_join");

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

float GravityBrawl::chooseSpawnAngle() {
    std::vector<float> occupiedAngles;
    occupiedAngles.reserve(m_planets.size());

    for (const auto& [_, planet] : m_planets) {
        if (!planet.alive || !planet.body) continue;

        const b2Vec2 pos = planet.body->GetPosition();
        float angle = std::atan2(pos.y - WORLD_CENTER_Y, pos.x - WORLD_CENTER_X);
        if (angle < 0.0f) {
            angle += 2.0f * b2_pi;
        }
        occupiedAngles.push_back(angle);
    }

    if (occupiedAngles.empty()) {
        std::uniform_real_distribution<float> angleDist(0.0f, 2.0f * b2_pi);
        return angleDist(m_rng);
    }

    std::sort(occupiedAngles.begin(), occupiedAngles.end());

    float bestAngle = occupiedAngles.front();
    float bestGap = 0.0f;
    for (size_t index = 0; index < occupiedAngles.size(); ++index) {
        const float current = occupiedAngles[index];
        const float next = occupiedAngles[(index + 1) % occupiedAngles.size()];
        const float gap = (index + 1 < occupiedAngles.size())
            ? (next - current)
            : ((next + 2.0f * b2_pi) - current);

        if (gap > bestGap) {
            bestGap = gap;
            bestAngle = current + gap * 0.5f;
        }
    }

    while (bestAngle >= 2.0f * b2_pi) {
        bestAngle -= 2.0f * b2_pi;
    }
    return bestAngle;
}

void GravityBrawl::spawnPlanetBody(Planet& p) {
    // Prefer the midpoint of the largest free orbital arc so many joins do not overlap.
    float angle = chooseSpawnAngle();
    float spawnRadius = ARENA_RADIUS * m_spawnRadiusFactor;

    float x = WORLD_CENTER_X + spawnRadius * std::cos(angle);
    float y = WORLD_CENTER_Y + spawnRadius * std::sin(angle);

    // Create circular body
    b2BodyDef bodyDef;
    bodyDef.type = b2_dynamicBody;
    bodyDef.position.Set(x, y);
    bodyDef.linearDamping = 0.5f;
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

    // 50% chance clockwise vs counter-clockwise orbit
    std::uniform_int_distribution<int> dirDist(0, 1);
    p.orbitDirection = dirDist(m_rng) ? 1 : -1;

    // Give initial tangential velocity for orbit
    float speed = m_spawnOrbitSpeed;
    float dir = static_cast<float>(p.orbitDirection);
    float vx = -speed * std::sin(angle) * dir;
    float vy =  speed * std::cos(angle) * dir;
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

    // Auto-aim: find the nearest alive enemy and dash toward them
    b2Vec2 myPos = p.body->GetPosition();
    float bestDist = 999999.0f;
    b2Vec2 bestDir(0.0f, 0.0f);
    bool foundTarget = false;

    for (const auto& [id, other] : m_planets) {
        if (!other.alive || !other.body || id == p.userId) continue;
        b2Vec2 otherPos = other.body->GetPosition();
        b2Vec2 diff(otherPos.x - myPos.x, otherPos.y - myPos.y);
        float dist = diff.Length();
        if (dist > 0.1f && dist < bestDist) {
            bestDist = dist;
            bestDir = b2Vec2(diff.x / dist, diff.y / dist);
            foundTarget = true;
        }
    }

    float impulse = 12.0f + p.getMassScale() * 2.0f;

    if (foundTarget) {
        // Dash toward the nearest enemy
        p.body->ApplyLinearImpulseToCenter(
            b2Vec2(bestDir.x * impulse, bestDir.y * impulse), true);
    } else {
        // No target: boost in current velocity direction or away from center
        b2Vec2 vel = p.body->GetLinearVelocity();
        float speed = vel.Length();
        if (speed < 0.1f) {
            b2Vec2 dir(myPos.x - WORLD_CENTER_X, myPos.y - WORLD_CENTER_Y);
            float len = dir.Length();
            if (len > 0.01f) dir = b2Vec2(dir.x / len, dir.y / len);
            p.body->ApplyLinearImpulseToCenter(
                b2Vec2(dir.x * 15.0f, dir.y * 15.0f), true);
        } else {
            b2Vec2 dir(vel.x / speed, vel.y / speed);
            p.body->ApplyLinearImpulseToCenter(
                b2Vec2(dir.x * impulse, dir.y * impulse), true);
        }
    }

    playSfx("gb_smash");

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

            // Credit the supernova user for potential kills
            other.lastHitBy = p.userId;
            other.lastHitTime = m_gameTimer;
        }
    }

    playSfx("gb_supernova");

    // Massive particle effect
    sf::Vector2f screenPos = worldToScreen(pos);
    emitSupernovaWave(screenPos, getTierColor(p.tier));

    addFloatingText("SUPERNOVA!", screenPos, sf::Color(255, 200, 50), 2.0f);
}

// ── Collision Handler ────────────────────────────────────────────────────────

void GravityBrawl::onPlanetCollision(Planet& a, Planet& b, float impulse) {
    if (impulse < 2.0f) return; // Ignore gentle touches
    if (m_phase != GamePhase::Playing) return; // No scoring outside active play

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
        if (!isBot(a.userId))
            db.recordResult(a.userId, a.displayName, "gravity_brawl", 2, false);
        if (!isBot(b.userId))
            db.recordResult(b.userId, b.displayName, "gravity_brawl", 2, false);
    } catch (...) {}

    playSfx("gb_hit", 0.6f);

    // Floating text
    addFloatingText("+2", worldToScreen(a.body->GetPosition()), sf::Color(100, 255, 100), 0.8f);
    addFloatingText("+2", worldToScreen(b.body->GetPosition()), sf::Color(100, 255, 100), 0.8f);
}

// ── Phase Management ─────────────────────────────────────────────────────────

void GravityBrawl::startCountdown() {
    // Fill up with bots before the round starts
    spawnBots();

    m_phase = GamePhase::Countdown;
    m_countdownTimer = 3.0;
    playSfx("gb_countdown");
    spdlog::info("[GravityBrawl] Countdown started.");
}

void GravityBrawl::startPlaying() {
    m_phase = GamePhase::Playing;
    m_gameTimer = 0.0;
    m_cosmicEventTimer = m_cosmicEventCooldown;
    m_cosmicEventActive = 0.0;
    m_blackHoleConsumedGravityBonus = 0.0f;
    m_survivalAccum = 0.0;
    playSfx("gb_battle_start");
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
    updateCameraZoom(fdt);

    // Update trail emission rate limiter
    m_trailEmitTimer -= fdt;
    if (m_trailEmitTimer < 0.0f) m_trailEmitTimer = 0.0f;

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

                // Trail particles for higher tiers (rate-limited)
                if (p.tier >= PlanetTier::IcePlanet && m_trailEmitTimer <= 0.0f) {
                    sf::Vector2f screenPos = worldToScreen(pos);
                    emitTrail(screenPos, getTierColor(p.tier));
                }

                // Star tier ambient particles (rate-limited, every ~0.3s)
                if (p.tier == PlanetTier::Star && m_trailEmitTimer <= 0.0f) {
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

                // Update fixture radius if grown (threshold 0.05 to avoid frequent recreation)
                float newRadius = p.getVisualRadius();
                if (std::abs(newRadius - p.radiusMeters) > 0.05f) {
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

        // Reset trail emit timer after processing all planets
        if (m_trailEmitTimer <= 0.0f) {
            m_trailEmitTimer = TRAIL_EMIT_INTERVAL;
        }

        // Check deaths (black hole)
        checkBlackHoleDeaths();

        // Bot AI
        updateBotAI(fdt);
        respawnDeadBots(fdt);

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
            playSfx("gb_game_over");
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
                if (!isBot(winnerId)) {
                    try {
                        is::core::Application::instance().playerDatabase().recordResult(
                            winnerId, winner.displayName, "gravity_brawl", 100, true);
                    } catch (...) {}
                    sendChatFeedback("🏆 " + winner.displayName + " wins Gravity Brawl with " +
                                     std::to_string(winner.score) + " points!");
                }

                // Celebration particles for the winner
                if (winner.body) {
                    sf::Vector2f winPos = worldToScreen(winner.body->GetPosition());
                    emitExplosion(winPos, sf::Color(255, 215, 0), 200);
                    emitExplosion(winPos, sf::Color(255, 255, 255), 100);
                }
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
    (void)dt;
    // During cosmic events, boost orbital restoring forces to compensate for
    // the increased black hole gravity — keeps the game challenging but fair
    float eventBoost = (m_cosmicEventActive > 0.0) ? (1.0f + (m_eventGravityMul - 1.0f) * 0.5f) : 1.0f;

    for (auto& [id, p] : m_planets) {
        if (!p.alive || !p.body) continue;

        b2Vec2 pos = p.body->GetPosition();
        b2Vec2 toCenter(WORLD_CENTER_X - pos.x, WORLD_CENTER_Y - pos.y);
        float dist = toCenter.Length();

        if (dist < 0.1f) continue;

        b2Vec2 dir(toCenter.x / dist, toCenter.y / dist);

        // Restoring radial force toward safe orbit band
        float targetDist = ARENA_RADIUS * m_safeOrbitRadiusFactor;
        float pullStrength = m_orbitalGravityStrength * eventBoost;

        if (dist > targetDist * 1.2f) {
            // Too far out → stronger inward pull
            pullStrength *= m_orbitalOuterPullMultiplier;
        } else if (dist > targetDist * 0.8f && dist < targetDist * 1.2f) {
            // In safe band → gentle inward pull
            pullStrength *= m_orbitalSafeZonePullMultiplier;
        } else {
            // Inside safe orbit → push OUTWARD (negative = reverse direction)
            float penetration = (targetDist * 0.8f - dist) / (targetDist * 0.8f);
            pullStrength *= -(0.8f + 1.5f * penetration);
        }

        // Tangential force for orbit (perpendicular to radial direction)
        // Direction depends on the planet's assigned orbit direction
        float orbDir = static_cast<float>(p.orbitDirection);
        b2Vec2 tangent(-dir.y * orbDir, dir.x * orbDir);
        float orbitalSpeed = m_orbitalTangentialStrength;

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
    (void)dt;
    float gravMul = (m_cosmicEventActive > 0.0) ? m_eventGravityMul : 1.0f;
    float effectiveGravity = currentBlackHoleGravity() * gravMul;

    for (auto& [id, p] : m_planets) {
        if (!p.alive || !p.body) continue;

        b2Vec2 pos = p.body->GetPosition();
        b2Vec2 toCenter(WORLD_CENTER_X - pos.x, WORLD_CENTER_Y - pos.y);
        float dist = toCenter.Length();

        if (dist < 0.5f) continue;

        float distanceFromKillZone = std::max(1.0f, dist - BLACK_HOLE_RADIUS * m_blackHoleKillRadiusMultiplier);
        float pullForce = effectiveGravity / distanceFromKillZone;
        pullForce = std::min(pullForce, m_blackHoleGravityCap);

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

        // Kill zone is 20% larger than the visual radius
        if (dist < BLACK_HOLE_RADIUS * m_blackHoleKillRadiusMultiplier) {
            eliminatePlanet(p);
        }
    }
}

void GravityBrawl::eliminatePlanet(Planet& p) {
    if (!p.alive) return;
    p.alive = false;
    p.deaths++;
    playSfx("gb_death");

    bool victimIsBot = isBot(p.userId);
    std::string victimName = victimIsBot ? "a rogue planet" : p.displayName;

    // Explosion particles
    sf::Vector2f screenPos = worldToScreen(p.body->GetPosition());
    emitExplosion(screenPos, getTierColor(p.tier), 80);

    if (m_phase == GamePhase::Playing) {
        float gravityGain = std::max(0.0f, p.radiusMeters * m_blackHoleConsumeSizeFactor);
        m_blackHoleConsumedGravityBonus += gravityGain;
        spdlog::info("[GravityBrawl] Black hole gravity increased by {:.2f} (total bonus {:.2f}) after consuming '{}'",
                     gravityGain, m_blackHoleConsumedGravityBonus, victimName);
    }

    // Kill attribution
    std::string killerId = p.lastHitBy;
    double timeSinceHit = m_gameTimer - p.lastHitTime;
    bool attributed = !killerId.empty() && timeSinceHit < 5.0;

    bool wasBounty = p.isKing;

    if (attributed) {
        auto kit = m_planets.find(killerId);
        if (kit != m_planets.end() && kit->second.alive) {
            Planet& killer = kit->second;
            bool killerIsBot = isBot(killerId);

            // Kill points
            int killPoints = 50;
            killer.kills++;
            killer.score += killPoints;
            playSfx("gb_kill");

            // Floating text
            sf::Vector2f killerScreen = worldToScreen(killer.body->GetPosition());
            addFloatingText("+50", killerScreen, sf::Color(255, 215, 0), 1.5f);

            // Bounty bonus
            if (wasBounty) {
                int bountyBonus = 150;
                killer.score += bountyBonus;
                addFloatingText("+150 BOUNTY!", killerScreen, sf::Color(255, 50, 50), 2.0f);
                playSfx("gb_bounty");

                // Massive particle rain
                emitExplosion(killerScreen, sf::Color(255, 215, 0), 150);

                if (!killerIsBot) {
                    try {
                        is::core::Application::instance().playerDatabase().recordResult(
                            killerId, killer.displayName, "gravity_brawl", killPoints + bountyBonus, false);
                    } catch (...) {}
                }

                if (!killerIsBot && !victimIsBot) {
                    sendChatFeedback("👑💀 " + killer.displayName + " destroyed the King " +
                                     victimName + " for +" + std::to_string(bountyBonus) + " bounty!");
                }
            } else {
                if (!killerIsBot) {
                    try {
                        is::core::Application::instance().playerDatabase().recordResult(
                            killerId, killer.displayName, "gravity_brawl", killPoints, false);
                    } catch (...) {}
                }
            }

            // Kill feed (skip if bots are involved unless configured)
            if (m_botKillFeed || (!killerIsBot && !victimIsBot)) {
                std::string killerName = killerIsBot ? "Bot" : killer.displayName;
                m_killFeed.push_front({killerName, victimName, wasBounty, 6.0});
                if (m_killFeed.size() > 8) m_killFeed.pop_back();
            }

            if (!killerIsBot) {
                sendChatFeedback("🕳️ " + killer.displayName + " smashed " +
                                 victimName + " into the void!");
            }
        }
    } else {
        // Died by gravity (no attribution)
        if (m_botKillFeed || !victimIsBot) {
            m_killFeed.push_front({"The Void", victimName, wasBounty, 6.0});
            if (m_killFeed.size() > 8) m_killFeed.pop_back();
        }
        if (!victimIsBot) {
            sendChatFeedback("🕳️ " + victimName + " was consumed by the void!");
        }
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

    // Pick a random event flavor
    std::uniform_int_distribution<int> eventDist(0, 2);
    int eventType = eventDist(m_rng);

    std::string message;
    switch (eventType) {
    case 0:
        message = "🕳️ THE BLACK HOLE IS HUNGRY! Spam !s to escape!";
        break;
    case 1:
        message = "⚡ GRAVITATIONAL SURGE! The void pulls harder!";
        break;
    case 2:
        message = "🌀 SPACETIME RIFT! Orbit destabilizing!";
        break;
    }

    spdlog::info("[GravityBrawl] COSMIC EVENT triggered (type {})", eventType);
    sendChatFeedback(message);
    playSfx("gb_cosmic_event");
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
                    if (!isBot(id)) {
                        try {
                            is::core::Application::instance().playerDatabase().recordResult(
                                id, p.displayName, "gravity_brawl", survivorBonus, false);
                        } catch (...) {}
                    }
                    sf::Vector2f screenPos = worldToScreen(p.body->GetPosition());
                    addFloatingText("+25 SURVIVED!", screenPos, sf::Color(100, 255, 100), 1.5f);
                }
            }
            sendChatFeedback("✅ The black hole calms down. Survivors get +" +
                             std::to_string(survivorBonus) + " points!");
            playSfx("gb_cosmic_end");
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
                if (!isBot(id)) {
                    try {
                        is::core::Application::instance().playerDatabase().recordResult(
                            id, p.displayName, "gravity_brawl", 1, false);
                    } catch (...) {}
                }
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
    int reduced = std::max(1, count / 2); // Halve for performance
    emitParticles(pos, color, reduced, 200.f, 360.f, 1.2f);
    // Add some bright white particles
    emitParticles(pos, sf::Color(255, 255, 255, 200), reduced / 3, 150.f, 360.f, 0.6f);
}

void GravityBrawl::emitTrail(sf::Vector2f pos, sf::Color color) {
    color.a = 120;
    emitParticles(pos, color, 2, 15.f, 360.f, 0.4f);
}

void GravityBrawl::emitSupernovaWave(sf::Vector2f pos, sf::Color color) {
    // Ring of particles expanding outward (reduced counts for performance)
    emitParticles(pos, color, 50, 300.f, 360.f, 1.0f);
    emitParticles(pos, sf::Color(255, 255, 255, 200), 25, 250.f, 360.f, 0.8f);
    emitParticles(pos, sf::Color(255, 200, 50, 180), 15, 200.f, 360.f, 1.2f);
}

void GravityBrawl::updateParticles(float dt) {
    // Swap-and-pop pattern: O(1) removal instead of O(n) vector::erase
    for (size_t i = 0; i < m_particles.size();) {
        auto& p = m_particles[i];
        p.life -= dt;
        if (p.life <= 0.0f) {
            m_particles[i] = m_particles.back();
            m_particles.pop_back();
            continue;
        }
        p.velocity.x *= p.drag;
        p.velocity.y *= p.drag;
        p.position.x += p.velocity.x * dt;
        p.position.y += p.velocity.y * dt;

        // Fade alpha
        if (p.fadeAlpha) {
            float frac = p.life / p.maxLife;
            p.color.a = static_cast<sf::Uint8>(frac * 255);
        }
        // Shrink
        if (p.shrink) {
            float frac = p.life / p.maxLife;
            p.size = std::max(0.5f, p.size * (0.5f + 0.5f * frac));
        }
        ++i;
    }
}

// ── Floating Texts ───────────────────────────────────────────────────────────

void GravityBrawl::addFloatingText(const std::string& text, sf::Vector2f pos,
                                    sf::Color color, float duration) {
    m_floatingTexts.push_back({text, pos, color, duration, duration});
}

// ── Dynamic Camera Zoom ──────────────────────────────────────────────────────

void GravityBrawl::updateCameraZoom(float dt) {
    if (!m_cameraZoomEnabled) {
        m_cameraZoom = 1.0f;
        m_cameraTargetZoom = 1.0f;
        return;
    }

    // Find the maximum distance from WORLD_CENTER among alive players
    float maxDist = 0.0f;
    for (const auto& [_, p] : m_planets) {
        if (!p.alive || !p.body) continue;
        b2Vec2 pos = p.body->GetPosition();
        float dx = pos.x - WORLD_CENTER_X;
        float dy = pos.y - WORLD_CENTER_Y;
        float dist = std::sqrt(dx * dx + dy * dy) + p.getVisualRadius();
        maxDist = std::max(maxDist, dist);
    }

    // The default view fits SCREEN_H / 2 / PIXELS_PER_METER meters vertically
    // from center (portrait 9:16).  Horizontal is narrower: SCREEN_W / 2 / PPM.
    float defaultRadiusV = SCREEN_H / 2.0f / PIXELS_PER_METER;
    float defaultRadiusH = SCREEN_W / 2.0f / PIXELS_PER_METER;
    float defaultRadius  = std::min(defaultRadiusV, defaultRadiusH);

    float neededRadius = maxDist + m_cameraBufferMeters;

    if (neededRadius > defaultRadius) {
        m_cameraTargetZoom = std::max(m_cameraMinZoom, defaultRadius / neededRadius);
    } else if (neededRadius > 0.01f) {
        // Zoom in when all players are clustered near the center
        m_cameraTargetZoom = std::min(m_cameraMaxZoom, defaultRadius / neededRadius);
    } else {
        m_cameraTargetZoom = 1.0f;
    }

    // Smooth interpolation toward target
    float t = 1.0f - std::exp(-m_cameraZoomSpeed * dt);
    m_cameraZoom += (m_cameraTargetZoom - m_cameraZoom) * t;
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

    // Cosmic event warning (before UI so text remains readable)
    if (m_cosmicEventActive > 0.0) {
        renderCosmicEventWarning(target);
    }

    // UI overlay (on top of cosmic warning)
    renderUI(target);
    renderKillFeed(target);
    // In-game leaderboard removed; global scoreboard overlay handles it

    // GameOver scoreboard
    if (m_phase == GamePhase::GameOver) {
        renderGameOverScreen(target);
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
    float baseRadius = BLACK_HOLE_RADIUS * PIXELS_PER_METER * m_cameraZoom;

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

    // Particle drain toward center (rate-limited: every 4th tick)
    if (static_cast<int>(m_blackHolePulse * 5) % 4 == 0) {
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
    float orbitR = ARENA_RADIUS * m_safeOrbitRadiusFactor * PIXELS_PER_METER * m_cameraZoom;

    // Faint orbit circle
    sf::CircleShape orbit(orbitR);
    orbit.setOrigin(orbitR, orbitR);
    orbit.setPosition(center);
    orbit.setFillColor(sf::Color::Transparent);
    orbit.setOutlineColor(sf::Color(80, 80, 120, 30));
    orbit.setOutlineThickness(1.5f);
    target.draw(orbit);

    // Danger zone ring (inner)
    float dangerR = BLACK_HOLE_RADIUS * m_blackHoleKillRadiusMultiplier * PIXELS_PER_METER;
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
    float pixelRadius = p.getVisualRadius() * PIXELS_PER_METER * m_cameraZoom;
    sf::Color baseColor = getTierColor(p.tier);
    sf::Color glowColor = getTierGlowColor(p.tier);

    // Hit flash
    if (p.hitFlashTimer > 0.0f) {
        baseColor = sf::Color::White;
    }

    // Supernova shockwave ring
    if (p.supernovaTimer > 0.0f) {
        float progress = 1.0f - p.supernovaTimer / 0.5f;
        float ringR = pixelRadius + progress * 200.0f * m_cameraZoom;
        sf::CircleShape ring(ringR);
        ring.setOrigin(ringR, ringR);
        ring.setPosition(screenPos);
        sf::Uint8 ringA = static_cast<sf::Uint8>((1.0f - progress) * 200);
        ring.setFillColor(sf::Color::Transparent);
        ring.setOutlineColor(sf::Color(255, 255, 200, ringA));
        ring.setOutlineThickness((3.0f + (1.0f - progress) * 5.0f) * m_cameraZoom);
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
    float outlineThickness = 1.5f * m_cameraZoom;
    sf::Color outlineColor = baseColor;
    outlineColor.r = std::min(255, outlineColor.r + 40);
    outlineColor.g = std::min(255, outlineColor.g + 40);
    outlineColor.b = std::min(255, outlineColor.b + 40);

    if (p.isKing) {
        outlineColor = sf::Color(255, 215, 0);
        outlineThickness = 3.0f * m_cameraZoom;
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
            sf::RectangleShape band(sf::Vector2f(halfW * 1.6f, 2.0f * m_cameraZoom));
            band.setOrigin(halfW * 0.8f, 1.0f * m_cameraZoom);
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

    // Name & score labels — skip bots so real players stand out
    if (m_fontLoaded && !p.displayName.empty() && !p.isBot()) {
        auto rName = resolve("player_name", SCREEN_W, SCREEN_H);
        if (rName.visible) {
            sf::Text nameText;
            nameText.setFont(m_font);
            nameText.setString(p.displayName);
            // Keep text size constant regardless of camera zoom
            nameText.setCharacterSize(rName.fontSize);
            nameText.setFillColor(sf::Color(255, 255, 255, 220));
            nameText.setOutlineColor(sf::Color(0, 0, 0, 180));
            nameText.setOutlineThickness(1.0f);
            sf::FloatRect bounds = nameText.getLocalBounds();
            nameText.setOrigin(bounds.left + bounds.width / 2.0f, bounds.top);
            nameText.setPosition(screenPos.x, screenPos.y + pixelRadius + 6.0f);
            target.draw(nameText);

            // Score below name
            auto rScore = resolve("player_score", SCREEN_W, SCREEN_H);
            if (rScore.visible) {
                sf::Text scoreText;
                scoreText.setFont(m_font);
                scoreText.setString(std::to_string(p.score));
                scoreText.setCharacterSize(rScore.fontSize);
                scoreText.setFillColor(sf::Color(200, 200, 100, 180));
                scoreText.setOutlineColor(sf::Color(0, 0, 0, 150));
                scoreText.setOutlineThickness(1.0f);
                sf::FloatRect sBounds = scoreText.getLocalBounds();
                scoreText.setOrigin(sBounds.left + sBounds.width / 2.0f, sBounds.top);
                scoreText.setPosition(screenPos.x, screenPos.y + pixelRadius + 6.0f + rName.fontSize + 2.0f);
                target.draw(scoreText);
            }
        }
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
    // Batch all particles into a single vertex array (quads) for a single draw call
    m_particleVerts.setPrimitiveType(sf::Quads);
    m_particleVerts.resize(m_particles.size() * 4);

    for (size_t i = 0; i < m_particles.size(); ++i) {
        const auto& p = m_particles[i];
        float s = p.size;
        sf::Vector2f pos = p.position;
        size_t vi = i * 4;

        m_particleVerts[vi + 0].position = {pos.x - s, pos.y - s};
        m_particleVerts[vi + 1].position = {pos.x + s, pos.y - s};
        m_particleVerts[vi + 2].position = {pos.x + s, pos.y + s};
        m_particleVerts[vi + 3].position = {pos.x - s, pos.y + s};

        m_particleVerts[vi + 0].color = p.color;
        m_particleVerts[vi + 1].color = p.color;
        m_particleVerts[vi + 2].color = p.color;
        m_particleVerts[vi + 3].color = p.color;
    }

    target.draw(m_particleVerts);
}

// ── Kill Feed ────────────────────────────────────────────────────────────────

void GravityBrawl::renderKillFeed(sf::RenderTarget& target) {
    if (!m_fontLoaded || m_killFeed.empty()) return;
    auto r = resolve("kill_feed", SCREEN_W, SCREEN_H);
    if (!r.visible) return;

    float y = r.py;
    for (const auto& entry : m_killFeed) {
        float alpha = std::min(1.0f, static_cast<float>(entry.timeRemaining / 1.0));
        sf::Uint8 a = static_cast<sf::Uint8>(alpha * 220);

        std::string text = entry.killer + " > " + entry.victim;
        if (entry.wasBounty) text += " [BOUNTY]";

        sf::Text feedText;
        feedText.setFont(m_font);
        feedText.setString(text);
        feedText.setCharacterSize(r.fontSize);
        feedText.setFillColor(entry.wasBounty ? sf::Color(255, 215, 0, a) : sf::Color(255, 200, 200, a));
        feedText.setOutlineColor(sf::Color(0, 0, 0, a));
        feedText.setOutlineThickness(1.0f);

        sf::FloatRect bounds = feedText.getLocalBounds();
        // Right-align from the configured x position
        feedText.setPosition(r.px - bounds.width, y);
        target.draw(feedText);

        y += static_cast<float>(r.fontSize) * 1.3f;
    }
}



// ── Floating Texts ───────────────────────────────────────────────────────────

void GravityBrawl::renderFloatingTexts(sf::RenderTarget& target) {
    if (!m_fontLoaded) return;
    auto r = resolve("floating_text", SCREEN_W, SCREEN_H);
    if (!r.visible) return;

    for (const auto& ft : m_floatingTexts) {
        float alpha = std::min(1.0f, ft.timer / (ft.maxTime * 0.3f));
        sf::Uint8 a = static_cast<sf::Uint8>(alpha * 255);

        sf::Text text;
        text.setFont(m_font);
        text.setString(ft.text);
        text.setCharacterSize(r.fontSize);
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
    {
        auto r = resolve("title", SCREEN_W, SCREEN_H);
        if (r.visible) {
            sf::Text titleText;
            titleText.setFont(m_font);
            titleText.setString("GRAVITY BRAWL");
            titleText.setFillColor(sf::Color(200, 180, 255, 200));
            titleText.setOutlineColor(sf::Color(0, 0, 0, 200));
            titleText.setOutlineThickness(2.0f);
            applyTextLayout(titleText, r);
            target.draw(titleText);
        }
    }

    // Player count and timer
    {
        auto r = resolve("info_text", SCREEN_W, SCREEN_H);
        if (r.visible) {
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
            infoText.setFillColor(sf::Color(180, 180, 200, 200));
            infoText.setOutlineColor(sf::Color(0, 0, 0, 180));
            infoText.setOutlineThickness(1.0f);
            applyTextLayout(infoText, r);
            target.draw(infoText);
        }
    }

    // Join hint
    if (m_phase == GamePhase::Lobby || m_phase == GamePhase::Playing) {
        auto r = resolve("join_hint", SCREEN_W, SCREEN_H);
        if (r.visible) {
            sf::Text joinHint;
            joinHint.setFont(m_font);
            joinHint.setString("Type !join to play | !s to smash");
            joinHint.setFillColor(sf::Color(150, 150, 180, 160));
            joinHint.setOutlineColor(sf::Color(0, 0, 0, 120));
            joinHint.setOutlineThickness(1.0f);
            applyTextLayout(joinHint, r);
            target.draw(joinHint);
        }
    }

    // Cosmic event warning bar
    if (m_cosmicEventActive <= 0.0 && m_cosmicEventTimer < 10.0 && m_phase == GamePhase::Playing) {
        auto r = resolve("event_warning", SCREEN_W, SCREEN_H);
        if (r.visible) {
            sf::Text warningText;
            warningText.setFont(m_font);
            warningText.setString("BLACK HOLE HUNGERS IN " +
                                  std::to_string(static_cast<int>(m_cosmicEventTimer)) + "s");
            warningText.setFillColor(sf::Color(255, 100, 100, 200));
            warningText.setOutlineColor(sf::Color(0, 0, 0, 200));
            warningText.setOutlineThickness(1.0f);
            applyTextLayout(warningText, r);
            target.draw(warningText);
        }
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
    {
        auto r = resolve("cosmic_warning", SCREEN_W, SCREEN_H);
        if (r.visible) {
            sf::Text warning;
            warning.setFont(m_font);
            warning.setString("BLACK HOLE SURGE! SPAM !s TO ESCAPE!");
            warning.setCharacterSize(r.fontSize);
            warning.setFillColor(sf::Color(255, 50, 50, static_cast<sf::Uint8>(180 + pulse * 75)));
            warning.setOutlineColor(sf::Color(0, 0, 0, 220));
            warning.setOutlineThickness(2.0f);
            sf::FloatRect wb = warning.getLocalBounds();
            warning.setOrigin(wb.left + wb.width / 2.0f, wb.top + wb.height / 2.0f);
            warning.setPosition(r.px, r.py);

            float scale = 1.0f + pulse * 0.1f;
            warning.setScale(scale, scale);
            target.draw(warning);
        }
    }

    // Timer near black hole
    {
        auto r = resolve("cosmic_timer", SCREEN_W, SCREEN_H);
        if (r.visible) {
            int remaining = static_cast<int>(m_cosmicEventActive);
            sf::Text timerText;
            timerText.setFont(m_font);
            timerText.setString(std::to_string(remaining) + "s");
            timerText.setCharacterSize(r.fontSize);
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
    }
}

// ── Countdown ────────────────────────────────────────────────────────────────

void GravityBrawl::renderCountdown(sf::RenderTarget& target) {
    if (!m_fontLoaded) return;
    auto r = resolve("countdown", SCREEN_W, SCREEN_H);
    if (!r.visible) return;

    int count = static_cast<int>(std::ceil(m_countdownTimer));
    if (count <= 0) return;

    sf::Text text;
    text.setFont(m_font);
    text.setString(std::to_string(count));
    text.setCharacterSize(r.fontSize);
    text.setFillColor(sf::Color(255, 255, 255, 230));
    text.setOutlineColor(sf::Color(100, 50, 200, 200));
    text.setOutlineThickness(4.0f);
    sf::FloatRect bounds = text.getLocalBounds();
    text.setOrigin(bounds.left + bounds.width / 2.0f, bounds.top + bounds.height / 2.0f);
    text.setPosition(r.px, r.py);

    float frac = static_cast<float>(m_countdownTimer - static_cast<int>(m_countdownTimer));
    float scale = 1.0f + frac * 0.5f;
    text.setScale(scale, scale);
    target.draw(text);
}

// ── Game Over Screen ─────────────────────────────────────────────────────────

void GravityBrawl::renderGameOverScreen(sf::RenderTarget& target) {
    // Semi-transparent dark overlay
    sf::RectangleShape overlay(sf::Vector2f(SCREEN_W, SCREEN_H));
    overlay.setFillColor(sf::Color(0, 0, 0, 120));
    target.draw(overlay);

    if (!m_fontLoaded) return;

    // Sort players by score descending
    std::vector<const Planet*> ranked;
    ranked.reserve(m_planets.size());
    for (const auto& [id, p] : m_planets) {
        if (isBot(id)) continue; // Skip bots from scoreboard
        ranked.push_back(&p);
    }
    std::sort(ranked.begin(), ranked.end(),
              [](const Planet* a, const Planet* b) { return a->score > b->score; });

    float centerX = SCREEN_W / 2.0f;
    float startY = SCREEN_H * 0.25f;

    // Winner announcement
    if (!ranked.empty()) {
        sf::Text winnerText;
        winnerText.setFont(m_font);
        winnerText.setString(ranked[0]->displayName + " WINS!");
        winnerText.setCharacterSize(fs(42));
        winnerText.setFillColor(sf::Color(255, 215, 0));
        winnerText.setOutlineColor(sf::Color(0, 0, 0, 220));
        winnerText.setOutlineThickness(3.0f);
        sf::FloatRect wb = winnerText.getLocalBounds();
        winnerText.setOrigin(wb.left + wb.width / 2.0f, wb.top + wb.height / 2.0f);
        winnerText.setPosition(centerX, startY);
        target.draw(winnerText);
        startY += fs(42) + 20.0f;
    }

    // Score header
    {
        sf::Text headerText;
        headerText.setFont(m_font);
        headerText.setString("FINAL STANDINGS");
        headerText.setCharacterSize(fs(24));
        headerText.setFillColor(sf::Color(200, 180, 255, 220));
        headerText.setOutlineColor(sf::Color(0, 0, 0, 180));
        headerText.setOutlineThickness(1.0f);
        sf::FloatRect hb = headerText.getLocalBounds();
        headerText.setOrigin(hb.left + hb.width / 2.0f, hb.top + hb.height / 2.0f);
        headerText.setPosition(centerX, startY);
        target.draw(headerText);
        startY += fs(24) + 15.0f;
    }

    // Player rankings (top 8)
    int maxDisplay = std::min(static_cast<int>(ranked.size()), 8);
    for (int i = 0; i < maxDisplay; ++i) {
        const Planet* p = ranked[i];

        // Rank colors: gold, silver, bronze, then white
        sf::Color rankColor;
        if (i == 0)      rankColor = sf::Color(255, 215, 0);
        else if (i == 1) rankColor = sf::Color(192, 192, 192);
        else if (i == 2) rankColor = sf::Color(205, 127, 50);
        else             rankColor = sf::Color(180, 180, 200);

        std::string entry = "#" + std::to_string(i + 1) + "  " + p->displayName +
                            "   " + std::to_string(p->score) + " pts  (" +
                            std::to_string(p->kills) + " kills)";

        sf::Text entryText;
        entryText.setFont(m_font);
        entryText.setString(entry);
        entryText.setCharacterSize(fs(18));
        entryText.setFillColor(rankColor);
        entryText.setOutlineColor(sf::Color(0, 0, 0, 180));
        entryText.setOutlineThickness(1.0f);
        sf::FloatRect eb = entryText.getLocalBounds();
        entryText.setOrigin(eb.left + eb.width / 2.0f, eb.top + eb.height / 2.0f);
        entryText.setPosition(centerX, startY);
        target.draw(entryText);

        startY += fs(18) + 8.0f;
    }
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
    state["blackHoleGravity"] = currentBlackHoleGravity();
    state["blackHoleConsumedGravityBonus"] = m_blackHoleConsumedGravityBonus;

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

float GravityBrawl::currentBlackHoleGravity() const {
    float base = m_blackHoleBaseGravity + static_cast<float>(m_gameTimer) * m_blackHoleTimeGrowthFactor;
    float cappedBonus = std::min(m_blackHoleConsumedGravityBonus, m_blackHoleGravityCap * 0.5f);
    return base + cappedBonus;
}

std::vector<std::pair<std::string, int>> GravityBrawl::getLeaderboard() const {
    std::vector<std::pair<std::string, int>> result;
    for (const auto& [id, p] : m_planets) {
        if (isBot(id)) continue; // Skip bots
        result.emplace_back(p.displayName, p.score);
    }
    std::sort(result.begin(), result.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    return result;
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
