#include "games/country_elimination/CountryElimination.h"
#include "games/country_elimination/CountryAliases.h"
#include "games/GameRegistry.h"
#include "core/Application.h"
#include "core/PlayerDatabase.h"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <sstream>

namespace is::games::country_elimination {

REGISTER_GAME(CountryElimination, "country_elimination");

static constexpr float PI  = 3.14159265358979323846f;
static constexpr float TAU = 2.0f * PI;

static inline bool isBotId(const std::string& id) {
    return id.size() >= 6 && id.rfind("__bot_", 0) == 0;
}

static const char* BOT_NAMES[] = {
    "America", "Britain", "France", "Germany", "Japan",
    "Brazil", "India", "China", "Russia", "Korea",
    "Italy", "Spain", "Mexico", "Canada", "Australia",
    "Portugal", "Holland", "Sweden", "Poland", "Turkey",
    "Argentina", "Colombia", "Chile", "Peru", "Norway",
    "Denmark", "Finland", "Belgium", "Austria", "Swiss",
    "Greece", "Ireland", "NewZealand", "SouthAfrica", "Egypt",
    "Thailand", "Vietnam", "Philippines", "Indonesia", "Malaysia",
};
static const char* BOT_LABELS[] = {
    "US", "GB", "FR", "DE", "JP",
    "BR", "IN", "CN", "RU", "KR",
    "IT", "ES", "MX", "CA", "AU",
    "PT", "NL", "SE", "PL", "TR",
    "AR", "CO", "CL", "PE", "NO",
    "DK", "FI", "BE", "AT", "CH",
    "GR", "IE", "NZ", "ZA", "EG",
    "TH", "VN", "PH", "ID", "MY",
};
static constexpr int NUM_BOT_NAMES = sizeof(BOT_NAMES) / sizeof(BOT_NAMES[0]);

// ── Sprite-sheet flag order (matches rows in assets/img/flagSprite60.png) ─────

static const char* SPRITE_ORDER[] = {
    // Africa (59)
    "DZ","AO","BJ","BW","BF","BI","CM","CV","CF","TD",
    "CD","DJ","EG","GQ","ER","ET","GA","GM","GH","GN",
    "GW","CI","KE","LS","LR","LY","MG","MW","ML","MR",
    "MU","YT","MA","MZ","NA","NE","NG","CG","RE","RW",
    "SH","ST","SN","SC","SL","SO","ZA","SS","SD","SR",
    "SZ","TG","TN","UG","TZ","EH","YE","ZM","ZW",
    // Americas (54)
    "AI","AG","AR","AW","BS","BB","BQ","BZ","BM","BO",
    "VG","BR","CA","KY","CL","CO","KM","CR","CU","CW",
    "DM","DO","EC","SV","FK","GF","GL","GD","GP","GT",
    "GY","HT","HN","JM","MQ","MX","MS","NI","PA","PY",
    "PE","PR","BL","KN","LC","PM","VC","SX","TT","TC",
    "US","VI","UY","VE",
    // Asia (35)
    "AB","AF","AZ","BD","BT","BN","KH","CN","GE","HK",
    "IN","ID","JP","KZ","LA","MO","MY","MV","MN","MM",
    "NP","KP","MP","PW","PG","PH","SG","KR","LK","TW",
    "TJ","TH","TL","TM","VN",
    // Europe (54)
    "AX","AL","AD","AM","AT","BY","BE","BA","BG","HR",
    "CY","CZ","DK","EE","FO","FI","FR","DE","GI","GR",
    "GG","HU","IS","IE","IM","IT","JE","XK","LV","LI",
    "LT","LU","MT","MD","MC","ME","NL","MK","NO","PL",
    "PT","RO","RU","SM","RS","SK","SI","ES","SE","CH",
    "TR","UA","GB","VA",
    // Middle East (16)
    "BH","IR","IQ","IL","KW","JO","KG","LB","OM","PK",
    "PS","QA","SA","SY","AE","UZ",
    // Oceania (23)
    "AS","AU","CX","CC","CK","FJ","PF","GU","KI","MH",
    "FM","NC","NZ","NR","NU","NF","WS","SB","TK","TO",
    "TV","VU","WF",
    // Special (5)
    "AQ","EU","JR","OLY","UN",
};
static constexpr int NUM_SPRITE_FLAGS = sizeof(SPRITE_ORDER) / sizeof(SPRITE_ORDER[0]);

// ═════════════════════════════════════════════════════════════════════════════
// Construction
// ═════════════════════════════════════════════════════════════════════════════

CountryElimination::CountryElimination()
    : m_rng(std::random_device{}())
{
}

CountryElimination::~CountryElimination() {
    shutdown();
}

// ═════════════════════════════════════════════════════════════════════════════
// Flag Texture Generation
// ═════════════════════════════════════════════════════════════════════════════

void CountryElimination::generateFlagTextures() {
    sf::Image spriteSheet;
    if (!spriteSheet.loadFromFile("assets/img/flagSprite60.png")) {
        spdlog::warn("[CountryElimination] Could not load flag sprite sheet");
        return;
    }

    unsigned int sheetW = spriteSheet.getSize().x;
    unsigned int sheetH = spriteSheet.getSize().y;
    unsigned int flagH  = sheetH / NUM_SPRITE_FLAGS;

    for (int i = 0; i < NUM_SPRITE_FLAGS; ++i) {
        sf::IntRect area(0, i * flagH, sheetW, flagH);
        m_flagTextures[SPRITE_ORDER[i]].loadFromImage(spriteSheet, area);
        m_flagTextures[SPRITE_ORDER[i]].setSmooth(true);
    }

    spdlog::info("[CountryElimination] Loaded {} flag textures ({}x{}, {}px each)",
                 NUM_SPRITE_FLAGS, sheetW, sheetH, flagH);
}

// ═════════════════════════════════════════════════════════════════════════════
// Lifecycle
// ═════════════════════════════════════════════════════════════════════════════

void CountryElimination::initialize() {
    spdlog::info("[CountryElimination] Initializing...");

    if (m_textElements.empty()) {
        registerTextElement("title",         "Game Title",       50.0f,  2.0f, 32, TextAlign::Center, true, "#FFFFFFEE");
        registerTextElement("phase",         "Phase Indicator",  50.0f,  5.0f, 22, TextAlign::Center, true, "#CCCCCCCC");
        registerTextElement("join_hint",     "Join Hint",        50.0f, 95.0f, 20, TextAlign::Center, true, "#FFFFFFAA");
        registerTextElement("countdown",     "Countdown Number", 50.0f, 50.0f, 90, TextAlign::Center, false);
        registerTextElement("winner_text",   "Winner Text",      50.0f, 30.0f, 52, TextAlign::Center, false, "#FFD700FF");
        registerTextElement("winner_label",  "Winner Country",   50.0f, 37.0f, 36, TextAlign::Center, false, "#FFFFFFEE");
        registerTextElement("elim_feed",     "Elimination Feed", 50.0f, 75.0f, 16, TextAlign::Center, true,  "#FF6666CC");
        registerTextElement("player_count",  "Player Count",     50.0f,  7.5f, 20, TextAlign::Center, true, "#FFFFFFBB");
        registerTextElement("sub_info",      "Sub Reward Info",  50.0f, 97.5f,  9, TextAlign::Center, true, "#AAAACC99");
    }

    m_world = new b2World(b2Vec2(0.0f, 15.0f));

    // Static floor
    {
        b2BodyDef fd;
        fd.type = b2_staticBody;
        fd.position.Set(WORLD_CX, FLOOR_Y);
        m_floorBody = m_world->CreateBody(&fd);

        b2PolygonShape box;
        box.SetAsBox(WORLD_W, 0.5f);
        b2FixtureDef fix;
        fix.shape = &box;
        fix.restitution = 0.3f;
        fix.friction = 0.8f;
        m_floorBody->CreateFixture(&fix);
    }

    createBoundaryWalls();

    // Lobby starts with closed ring (no gap)
    m_currentGapAngle = 0.0f;
    createArenaBody();

    // Arena always rotates
    if (m_arenaBody) m_arenaBody->SetAngularVelocity(m_arenaAngularVel);

    m_postProcessing.initialize(1080, 1920);
    m_background.initialize(1080, 1920, 100);

    if (m_font.loadFromFile("assets/fonts/JetBrainsMono-Regular.ttf")) {
        m_fontLoaded = true;
    } else {
        spdlog::warn("[CountryElimination] Font not found.");
    }

    m_phase = GamePhase::Lobby;
    m_lobbyTimer = 0.0;
    m_roundNumber = 0;
    m_gameWon = false;
    m_championId.clear();
    m_roundWinners.clear();
    m_currentBallSpeed = m_initialSpeed;

    spawnBots();

    spdlog::info("[CountryElimination] Initialized.");
}

void CountryElimination::shutdown() {
    for (auto& [id, p] : m_players) {
        if (p.body && m_world) { m_world->DestroyBody(p.body); p.body = nullptr; }
    }
    m_players.clear();
    m_eliminationFeed.clear();
    m_eliminatedQueue.clear();
    m_particles.clear();
    m_botRespawnTimers.clear();
    destroyArenaBody();
    if (m_leftWall && m_world) { m_world->DestroyBody(m_leftWall); m_leftWall = nullptr; }
    if (m_rightWall && m_world) { m_world->DestroyBody(m_rightWall); m_rightWall = nullptr; }
    if (m_floorBody && m_world) { m_world->DestroyBody(m_floorBody); m_floorBody = nullptr; }
    delete m_world;
    m_world = nullptr;
}

// ═════════════════════════════════════════════════════════════════════════════
// Screen Layout
// ═════════════════════════════════════════════════════════════════════════════

ScreenLayout CountryElimination::computeLayout(const sf::RenderTarget& target) const {
    ScreenLayout L{};
    L.W = static_cast<float>(target.getSize().x);
    L.H = static_cast<float>(target.getSize().y);

    // 9:16 safe zone centered
    float aspect = L.W / L.H;
    float mobileAspect = 9.0f / 16.0f;

    if (aspect <= mobileAspect + 0.02f) {
        // Portrait or square-ish → full width is safe
        L.safeW = L.W;
        L.safeLeft  = 0.0f;
        L.safeRight = L.W;
        L.isDesktop = false;
    } else {
        // Landscape → center column
        L.safeW = L.H * mobileAspect;
        L.safeLeft  = (L.W - L.safeW) / 2.0f;
        L.safeRight = L.safeLeft + L.safeW;
        L.isDesktop = true;
    }

    // Side panel geometry (only meaningful in desktop mode)
    float panelMargin = 8.0f;
    L.leftPanelX = panelMargin;
    L.leftPanelW = L.safeLeft - panelMargin * 2.0f;
    L.rightPanelX = L.safeRight + panelMargin;
    L.rightPanelW = L.W - L.safeRight - panelMargin * 2.0f;

    // Arena sizing: fit within safe zone
    float maxDiamW = L.safeW * 0.88f;
    float maxDiamH = L.H * 0.44f;
    float diameter = std::min(maxDiamW, maxDiamH);
    L.arenaRadiusPx = diameter / 2.0f;

    // Arena center: in the safe column, ~38% from top
    L.arenaCX = (L.safeLeft + L.safeRight) / 2.0f;
    L.arenaCY = L.H * 0.36f;

    L.ppm = L.arenaRadiusPx / ARENA_RADIUS;

    return L;
}

// ═════════════════════════════════════════════════════════════════════════════
// Arena Physics
// ═════════════════════════════════════════════════════════════════════════════

void CountryElimination::createBoundaryWalls() {
    if (!m_world) return;

    // Left wall
    {
        b2BodyDef bd;
        bd.type = b2_staticBody;
        bd.position.Set(0.5f, (WORLD_CY + FLOOR_Y) / 2.0f);
        m_leftWall = m_world->CreateBody(&bd);
        b2PolygonShape box;
        box.SetAsBox(0.5f, (FLOOR_Y - WORLD_CY + ARENA_RADIUS) / 2.0f);
        b2FixtureDef fix;
        fix.shape = &box;
        fix.restitution = 0.4f;
        fix.friction = 0.5f;
        m_leftWall->CreateFixture(&fix);
    }

    // Right wall
    {
        b2BodyDef bd;
        bd.type = b2_staticBody;
        bd.position.Set(WORLD_W - 0.5f, (WORLD_CY + FLOOR_Y) / 2.0f);
        m_rightWall = m_world->CreateBody(&bd);
        b2PolygonShape box;
        box.SetAsBox(0.5f, (FLOOR_Y - WORLD_CY + ARENA_RADIUS) / 2.0f);
        b2FixtureDef fix;
        fix.shape = &box;
        fix.restitution = 0.4f;
        fix.friction = 0.5f;
        m_rightWall->CreateFixture(&fix);
    }
}

void CountryElimination::createArenaBody() {
    if (!m_world) return;
    destroyArenaBody();

    b2BodyDef bd;
    bd.type = b2_kinematicBody;
    bd.position.Set(WORLD_CX, WORLD_CY);
    bd.angle = 0.0f;
    m_arenaBody = m_world->CreateBody(&bd);

    const float angleStep = TAU / WALL_SEGMENTS;
    const float segLen = ARENA_RADIUS * angleStep;

    for (int i = 0; i < WALL_SEGMENTS; ++i) {
        float angle = i * angleStep;

        // Skip gap segments (only if gap > 0)
        if (m_currentGapAngle > 0.001f) {
            float norm = angle;
            if (norm > PI) norm -= TAU;
            if (std::abs(norm) < m_currentGapAngle) continue;
        }

        float mx = ARENA_RADIUS * std::cos(angle);
        float my = ARENA_RADIUS * std::sin(angle);

        b2PolygonShape seg;
        seg.SetAsBox(segLen * 0.55f, WALL_THICKNESS * 0.5f,
                     b2Vec2(mx, my), angle + PI / 2.0f);

        b2FixtureDef fix;
        fix.shape = &seg;
        fix.restitution = 1.0f;
        fix.friction = 0.0f;
        m_arenaBody->CreateFixture(&fix);
    }

    m_arenaAngle = 0.0f;
}

void CountryElimination::recreateArena() {
    float savedAngle = 0.0f;
    float savedAngVel = m_arenaAngularVel;
    if (m_arenaBody) {
        savedAngle = m_arenaBody->GetAngle();
        savedAngVel = m_arenaBody->GetAngularVelocity();
    }
    destroyArenaBody();
    createArenaBody();
    if (m_arenaBody) {
        m_arenaBody->SetTransform(m_arenaBody->GetPosition(), savedAngle);
        m_arenaBody->SetAngularVelocity(savedAngVel);
        m_arenaAngle = savedAngle;
    }
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
    bd.bullet = true;
    bd.linearDamping = 0.0f;
    bd.gravityScale = 0.0f;

    b2Body* body = m_world->CreateBody(&bd);
    b2CircleShape circle;
    circle.m_radius = radius;

    b2FixtureDef fix;
    fix.shape = &circle;
    fix.density = 1.0f;
    fix.restitution = m_restitution;
    fix.friction = 0.0f;
    body->CreateFixture(&fix);

    // Always give initial velocity
    std::uniform_real_distribution<float> aDist(0.0f, TAU);
    float a = aDist(m_rng);
    body->SetLinearVelocity(b2Vec2(m_currentBallSpeed * std::cos(a),
                                    m_currentBallSpeed * std::sin(a)));

    return body;
}

// ═════════════════════════════════════════════════════════════════════════════
// Chat Handling
// ═════════════════════════════════════════════════════════════════════════════

void CountryElimination::onChatMessage(const platform::ChatMessage& msg) {
    handleStreamEvent(msg);
    if (msg.text.empty()) return;

    std::istringstream iss(msg.text);
    std::string cmd;
    iss >> cmd;
    if (!cmd.empty() && cmd[0] == '!') cmd = cmd.substr(1);
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);

    if (cmd == "join" || cmd == "play") {
        std::string label;
        std::getline(iss >> std::ws, label);
        while (!label.empty() && (label.back() == '\r' || label.back() == '\n' || label.back() == ' '))
            label.pop_back();
        // Resolve country name/code to 2-letter ISO code
        std::string code = resolveCountryCode(label);
        if (code.empty()) code = randomCountryCode(m_rng);
        cmdJoin(msg.userId, msg.displayName, code);
    }
}

void CountryElimination::cmdJoin(const std::string& userId, const std::string& displayName,
                                  const std::string& label) {
    if (m_phase != GamePhase::Lobby && m_phase != GamePhase::Battle) return;
    if (m_players.count(userId)) return;

    std::uniform_real_distribution<float> aDist(0.0f, TAU);
    std::uniform_real_distribution<float> rDist(0.0f, ARENA_RADIUS * 0.55f);
    float a = aDist(m_rng);
    float r = rDist(m_rng);
    float sx = WORLD_CX + r * std::cos(a);
    float sy = WORLD_CY + r * std::sin(a);

    Player p;
    p.userId = userId;
    p.displayName = displayName;
    p.label = label;
    p.color = generateColor();
    p.radiusM = BALL_RADIUS;
    p.alive = true;
    p.eliminated = false;
    p.body = createPlayerBody(sx, sy, BALL_RADIUS);
    p.prevPos = p.body->GetPosition();
    p.currPos = p.prevPos;
    m_players[userId] = std::move(p);

    if (!isBotId(userId)) {
        const auto& names = getCountryDisplayNames();
        auto nameIt = names.find(label);
        std::string countryName = (nameIt != names.end()) ? nameIt->second : label;
        sendChatFeedback(displayName + " [" + countryName + "] joined!");
    }
}

void CountryElimination::handleStreamEvent(const platform::ChatMessage& msg) {
    if (msg.eventType.empty()) return;
    if (m_phase != GamePhase::Battle && m_phase != GamePhase::Lobby) return;

    if (!m_players.count(msg.userId)) {
        cmdJoin(msg.userId, msg.displayName, randomCountryCode(m_rng));
        if (!m_players.count(msg.userId)) return;
    }

    Player& p = m_players[msg.userId];

    if (msg.eventType == "yt_subscribe" || msg.eventType == "twitch_sub") {
        p.hasShield = true;
        p.shieldTimer = 15.0f;
        p.score += 300;
        sendChatFeedback("🛡️ " + p.displayName + " got a shield! +300");
    } else if ((msg.eventType == "yt_superchat" || msg.eventType == "twitch_bits") && msg.amount > 100) {
        p.hasShield = true;
        p.shieldTimer = 20.0f;
        p.score += 500;
        if (p.body) {
            float nr = BALL_RADIUS * 1.5f;
            p.radiusM = nr;
            b2Fixture* fix = p.body->GetFixtureList();
            if (fix) {
                p.body->DestroyFixture(fix);
                b2CircleShape c;
                c.m_radius = nr;
                b2FixtureDef fd;
                fd.shape = &c;
                fd.density = 1.5f;
                fd.restitution = m_restitution;
                fd.friction = 0.0f;
                p.body->CreateFixture(&fd);
            }
        }
        sendChatFeedback("⭐ " + p.displayName + " powered up! +500");
    } else if (msg.eventType == "twitch_channel_points") {
        p.hasShield = true;
        p.shieldTimer = 10.0f;
        p.score += 100;
        sendChatFeedback("✨ " + p.displayName + " got a shield! +100");
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Game Logic
// ═════════════════════════════════════════════════════════════════════════════

void CountryElimination::startCountdown() {
    if (m_phase != GamePhase::Lobby) return;
    int cnt = 0;
    for (const auto& [_, p] : m_players) if (p.alive) cnt++;
    if (cnt < m_minPlayers) return;
    m_phase = GamePhase::Countdown;
    m_countdownTimer = 3.0;
}

void CountryElimination::startBattle() {
    m_phase = GamePhase::Battle;
    m_roundTimer = 0.0;
    m_roundNumber++;

    // Open the gap and rebuild arena
    m_currentGapAngle = GAP_INITIAL;
    m_currentBallSpeed = m_initialSpeed;
    recreateArena();

    // Arena rotation continues (already set)
    if (m_arenaBody) m_arenaBody->SetAngularVelocity(m_arenaAngularVel);

    // Re-normalize all ball velocities to current speed
    for (auto& [id, p] : m_players) {
        if (!p.alive || !p.body) continue;
        b2Vec2 vel = p.body->GetLinearVelocity();
        float spd = vel.Length();
        if (spd > 0.01f) {
            float scale = m_currentBallSpeed / spd;
            p.body->SetLinearVelocity(b2Vec2(vel.x * scale, vel.y * scale));
        } else {
            std::uniform_real_distribution<float> aDist(0.0f, TAU);
            float a = aDist(m_rng);
            p.body->SetLinearVelocity(b2Vec2(m_currentBallSpeed * std::cos(a),
                                              m_currentBallSpeed * std::sin(a)));
        }
    }
}

void CountryElimination::enforceConstantVelocity() {
    for (auto& [id, p] : m_players) {
        if (!p.alive || !p.body) continue;
        b2Vec2 vel = p.body->GetLinearVelocity();
        float spd = vel.Length();
        if (spd < 0.1f) {
            // Stuck ball — give random velocity
            std::uniform_real_distribution<float> aDist(0.0f, TAU);
            float a = aDist(m_rng);
            p.body->SetLinearVelocity(b2Vec2(m_currentBallSpeed * std::cos(a),
                                              m_currentBallSpeed * std::sin(a)));
        } else if (std::abs(spd - m_currentBallSpeed) > 0.1f) {
            float scale = m_currentBallSpeed / spd;
            p.body->SetLinearVelocity(b2Vec2(vel.x * scale, vel.y * scale));
        }
    }
}

void CountryElimination::checkEliminations() {
    float limit = ARENA_RADIUS + WALL_THICKNESS + 0.5f;
    float limit2 = limit * limit;

    for (auto& [id, p] : m_players) {
        if (!p.alive || !p.body) continue;

        b2Vec2 pos = p.body->GetPosition();
        float dx = pos.x - WORLD_CX;
        float dy = pos.y - WORLD_CY;
        float d2 = dx * dx + dy * dy;

        if (d2 > limit2) {
            if (p.hasShield) {
                p.hasShield = false;
                p.shieldTimer = 0.0f;
                b2Vec2 dir(-dx, -dy);
                float len = dir.Length();
                if (len > 0.01f) dir *= (m_currentBallSpeed * 2.0f / len);
                p.body->SetLinearVelocity(dir);
                float pullback = ARENA_RADIUS * 0.75f / std::sqrt(d2);
                p.body->SetTransform(
                    b2Vec2(WORLD_CX + dx * pullback, WORLD_CY + dy * pullback),
                    p.body->GetAngle());
                if (!p.isBot())
                    sendChatFeedback("🛡️ " + p.displayName + "'s shield saved them!");
                continue;
            }

            p.alive = false;
            p.body->SetGravityScale(1.0f);
            p.body->SetLinearDamping(0.5f);
            b2Vec2 vel = p.body->GetLinearVelocity();
            p.body->SetLinearVelocity(b2Vec2(vel.x * 0.4f, 2.0f));

            // Track in eliminated FIFO queue
            m_eliminatedQueue.push_back({id, 0.0f, false, 0.0f});

            m_eliminationFeed.push_front({p.displayName, p.label, p.color, 4.0});
            if (m_eliminationFeed.size() > 8) m_eliminationFeed.pop_back();

            if (!p.isBot()) {
                sendChatFeedback("💀 " + p.displayName + " [" + p.label + "] eliminated!");
                try {
                    is::core::Application::instance().playerDatabase().recordResult(
                        p.userId, p.displayName, "country_elimination", 1, false);
                } catch (...) {}
            }
        }
    }

    // Settle eliminated balls at floor
    for (auto& [id, p] : m_players) {
        if (p.alive || p.eliminated || !p.body) continue;
        b2Vec2 pos = p.body->GetPosition();
        if (pos.y > FLOOR_Y - 1.0f) {
            p.eliminated = true;
            p.body->SetLinearVelocity(b2Vec2(0.0f, 0.0f));
            p.body->SetType(b2_staticBody);
        }
    }
}

void CountryElimination::checkRoundEnd() {
    int aliveCount = 0;
    std::string lastAlive;
    for (const auto& [id, p] : m_players) {
        if (p.alive) { aliveCount++; lastAlive = id; }
    }

    // End when 1 or 0 alive, OR round time limit exceeded
    bool timeUp = m_roundTimer >= m_roundDuration;
    if ((aliveCount <= 1 && m_players.size() >= 2) || (timeUp && aliveCount <= 2)) {
        m_winnerId = (aliveCount == 1) ? lastAlive : "";
        endRound();
    }
}

void CountryElimination::endRound() {
    m_phase = GamePhase::RoundEnd;
    m_roundEndTimer = m_roundEndDuration;

    if (!m_winnerId.empty() && m_players.count(m_winnerId)) {
        Player& w = m_players[m_winnerId];
        w.score += 100;
        recordRoundWin(w);

        if (!w.isBot()) {
            sendChatFeedback("🏆 " + w.displayName + " [" + w.label + "] wins Round " +
                              std::to_string(m_roundNumber) + "!");
            try {
                is::core::Application::instance().playerDatabase().recordResult(
                    w.userId, w.displayName, "country_elimination", 100, true);
            } catch (...) {}
        }
    } else {
        sendChatFeedback("Draw! No one wins this round.");
    }
}

void CountryElimination::recordRoundWin(const Player& winner) {
    for (auto& rw : m_roundWinners) {
        if (rw.userId == winner.userId) {
            rw.wins++;
            rw.label = winner.label;
            rw.color = winner.color;
            std::sort(m_roundWinners.begin(), m_roundWinners.end(),
                      [](const RoundWinEntry& a, const RoundWinEntry& b) { return a.wins > b.wins; });
            if (rw.wins >= m_championThreshold && !m_gameWon) {
                m_gameWon = true;
                m_championId = winner.userId;
                sendChatFeedback("👑 " + winner.displayName + " is the CHAMPION with " +
                                  std::to_string(rw.wins) + " wins!");
            }
            return;
        }
    }
    m_roundWinners.push_back({winner.userId, winner.displayName, winner.label, winner.color, 1});
    std::sort(m_roundWinners.begin(), m_roundWinners.end(),
              [](const RoundWinEntry& a, const RoundWinEntry& b) { return a.wins > b.wins; });
}

void CountryElimination::resetForNextRound() {
    for (auto& [id, p] : m_players) {
        if (p.body && m_world) { m_world->DestroyBody(p.body); p.body = nullptr; }
    }
    m_players.clear();
    m_eliminationFeed.clear();
    m_eliminatedQueue.clear();
    m_winnerId.clear();
    m_particles.clear();
    m_botRespawnTimers.clear();

    // Reset to closed ring for lobby
    m_currentGapAngle = 0.0f;
    m_currentBallSpeed = m_initialSpeed;
    m_arenaAngularVel = 0.3f;
    createArenaBody();
    if (m_arenaBody) m_arenaBody->SetAngularVelocity(m_arenaAngularVel);

    m_phase = GamePhase::Lobby;
    m_lobbyTimer = 0.0;

    spawnBots();
}

// ═════════════════════════════════════════════════════════════════════════════
// Bots
// ═════════════════════════════════════════════════════════════════════════════

void CountryElimination::spawnBots() {
    if (m_botFillTarget <= 0) return;

    int currentAlive = 0;
    for (const auto& [_, p] : m_players) { if (p.alive) currentAlive++; }

    int needed = m_botFillTarget - currentAlive;
    for (int i = 0; i < needed; ++i) {
        m_botCounter++;
        std::string botId = "__bot_" + std::to_string(m_botCounter);
        int idx = (m_botCounter - 1) % NUM_BOT_NAMES;

        cmdJoin(botId, BOT_NAMES[idx], BOT_LABELS[idx]);
    }
}

void CountryElimination::respawnDeadBots(float dt) {
    if (m_botFillTarget <= 0 || !m_botRespawn) return;

    int currentAlive = 0;
    for (const auto& [_, p] : m_players) { if (p.alive) currentAlive++; }

    for (auto& [id, p] : m_players) {
        if (p.alive || !p.isBot()) continue;
        // Track this dead bot
        if (m_botRespawnTimers.find(id) == m_botRespawnTimers.end())
            m_botRespawnTimers[id] = m_botRespawnDelay;
    }

    // Tick timers and respawn
    std::vector<std::string> toRespawn;
    for (auto it = m_botRespawnTimers.begin(); it != m_botRespawnTimers.end(); ) {
        it->second -= dt;
        if (it->second <= 0.0f && currentAlive < m_botFillTarget) {
            toRespawn.push_back(it->first);
            it = m_botRespawnTimers.erase(it);
        } else {
            ++it;
        }
    }

    for (const auto& botId : toRespawn) {
        // Remove old player entry and respawn fresh
        if (m_players.count(botId)) {
            auto& p = m_players[botId];
            if (p.body && m_world) m_world->DestroyBody(p.body);
            m_players.erase(botId);
        }
        m_botCounter++;
        std::string newId = "__bot_" + std::to_string(m_botCounter);
        int idx = (m_botCounter - 1) % NUM_BOT_NAMES;
        cmdJoin(newId, BOT_NAMES[idx], BOT_LABELS[idx]);
        currentAlive++;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Particles
// ═════════════════════════════════════════════════════════════════════════════

void CountryElimination::emitParticles(sf::Vector2f pos, sf::Color color,
                                        int count, float speed, float life, float size) {
    std::uniform_real_distribution<float> aDist(0.0f, TAU);
    std::uniform_real_distribution<float> sDist(speed * 0.4f, speed);
    for (int i = 0; i < count && m_particles.size() < 600; ++i) {
        float a = aDist(m_rng);
        float s = sDist(m_rng);
        m_particles.push_back({pos, {s * std::cos(a), s * std::sin(a)},
                               color, life, life, size});
    }
}

void CountryElimination::emitCelebration(sf::Vector2f pos) {
    sf::Color colors[] = {
        {255, 215, 0}, {255, 100, 100}, {100, 255, 100},
        {100, 200, 255}, {255, 150, 50}, {200, 100, 255}
    };
    for (auto& c : colors) {
        emitParticles(pos, c, 12, 250.0f, 1.5f, 4.0f);
    }
}

void CountryElimination::updateParticles(float dt) {
    for (auto& p : m_particles) {
        p.pos += p.vel * dt;
        p.vel *= 0.97f;
        p.life -= dt;
        float ratio = std::max(0.0f, p.life / p.maxLife);
        p.color.a = static_cast<sf::Uint8>(ratio * 255);
        p.size *= (1.0f - dt * 0.5f);
    }
    m_particles.erase(
        std::remove_if(m_particles.begin(), m_particles.end(),
                       [](const Particle& p) { return p.life <= 0.0f; }),
        m_particles.end());
}

// ═════════════════════════════════════════════════════════════════════════════
// Update
// ═════════════════════════════════════════════════════════════════════════════

void CountryElimination::update(double dt) {
    float fdt = static_cast<float>(dt);
    m_globalTime += fdt;
    m_arenaGlowPhase += fdt * 1.5f;
    m_background.update(fdt);
    updateParticles(fdt);

    for (auto& [id, p] : m_players) {
        if (p.hasShield) {
            p.shieldTimer -= fdt;
            if (p.shieldTimer <= 0.0f) { p.hasShield = false; p.shieldTimer = 0.0f; }
        }
    }

    for (auto& e : m_eliminationFeed) e.timeRemaining -= dt;
    while (!m_eliminationFeed.empty() && m_eliminationFeed.back().timeRemaining <= 0.0)
        m_eliminationFeed.pop_back();

    // Update eliminated ball FIFO fade tracking
    for (auto& eb : m_eliminatedQueue) eb.age += fdt;
    // Mark oldest balls for fading when over limit
    int visibleCount = static_cast<int>(m_eliminatedQueue.size());
    int overLimit = visibleCount - m_maxEliminatedVisible;
    if (overLimit > 0) {
        for (int i = 0; i < overLimit && i < visibleCount; ++i) {
            if (!m_eliminatedQueue[i].fading) {
                m_eliminatedQueue[i].fading = true;
                m_eliminatedQueue[i].fadeProgress = 0.0f;
            }
        }
    }
    // Also start fading balls that have lingered too long
    for (auto& eb : m_eliminatedQueue) {
        if (!eb.fading && eb.age >= m_elimLingerDuration) {
            eb.fading = true;
            eb.fadeProgress = 0.0f;
        }
    }
    // Advance fade and remove fully faded
    for (auto& eb : m_eliminatedQueue) {
        if (eb.fading) {
            eb.fadeProgress += fdt / std::max(0.1f, m_elimFadeDuration);
        }
    }
    while (!m_eliminatedQueue.empty() && m_eliminatedQueue.front().fadeProgress >= 1.0f) {
        auto& eb = m_eliminatedQueue.front();
        if (m_players.count(eb.playerId)) {
            auto& p = m_players[eb.playerId];
            if (p.body && m_world) m_world->DestroyBody(p.body);
            p.body = nullptr;
        }
        m_eliminatedQueue.pop_front();
    }

    switch (m_phase) {
    case GamePhase::Lobby: {
        m_lobbyTimer += dt;

        // Physics always running — balls bounce around
        for (auto& [id, p] : m_players)
            if (p.body) p.prevPos = p.currPos;
        m_world->Step(fdt, 8, 3);
        for (auto& [id, p] : m_players)
            if (p.body) p.currPos = p.body->GetPosition();

        enforceConstantVelocity();

        // Arena always rotates
        if (m_arenaBody) {
            m_arenaBody->SetAngularVelocity(m_arenaAngularVel);
            m_arenaAngle = m_arenaBody->GetAngle();
        }

        // Auto-start when enough players
        int cnt = 0;
        for (const auto& [_, p] : m_players) if (p.alive) cnt++;
        if (cnt >= m_minPlayers && m_lobbyTimer >= m_lobbyDuration)
            startCountdown();
        break;
    }
    case GamePhase::Countdown: {
        m_countdownTimer -= dt;

        // Physics still running
        for (auto& [id, p] : m_players)
            if (p.body) p.prevPos = p.currPos;
        m_world->Step(fdt, 8, 3);
        for (auto& [id, p] : m_players)
            if (p.body) p.currPos = p.body->GetPosition();

        enforceConstantVelocity();

        // Arena continues rotating
        if (m_arenaBody) {
            m_arenaAngle = m_arenaBody->GetAngle();
        }

        if (m_countdownTimer <= 0.0) startBattle();
        break;
    }
    case GamePhase::Battle: {
        m_roundTimer += dt;

        for (auto& [id, p] : m_players)
            if (p.body) p.prevPos = p.currPos;
        m_world->Step(fdt, 8, 3);
        for (auto& [id, p] : m_players)
            if (p.body) p.currPos = p.body->GetPosition();

        // Increase ball speed over time
        m_currentBallSpeed = std::min(m_maxBallSpeed,
            m_initialSpeed + m_ballSpeedIncrease * static_cast<float>(m_roundTimer));
        enforceConstantVelocity();

        // Arena acceleration
        m_arenaAngularVel += m_arenaSpeedIncrease * fdt;
        if (m_arenaBody) {
            m_arenaBody->SetAngularVelocity(m_arenaAngularVel);
            m_arenaAngle = m_arenaBody->GetAngle();
        }

        // Gap expansion
        float newGap = m_currentGapAngle + m_gapExpansionRate * fdt;
        if (newGap <= m_gapMax) {
            float segAngle = TAU / WALL_SEGMENTS;
            // Rebuild when gap crosses next segment boundary
            if (static_cast<int>(newGap / segAngle) > static_cast<int>(m_currentGapAngle / segAngle)) {
                m_currentGapAngle = newGap;
                recreateArena();
            } else {
                m_currentGapAngle = newGap;
            }
        }

        checkEliminations();
        respawnDeadBots(fdt);
        checkRoundEnd();
        break;
    }
    case GamePhase::RoundEnd: {
        m_world->Step(fdt, 6, 2);
        for (auto& [id, p] : m_players)
            if (p.body) { p.prevPos = p.currPos; p.currPos = p.body->GetPosition(); }

        // Arena still rotates (slowing down)
        if (m_arenaBody) {
            float curVel = m_arenaBody->GetAngularVelocity();
            float decel = curVel * 0.98f;
            m_arenaBody->SetAngularVelocity(decel);
            m_arenaAngle = m_arenaBody->GetAngle();
        }

        // Celebration particles for winner
        if (!m_winnerId.empty() && m_players.count(m_winnerId)) {
            const auto& w = m_players.at(m_winnerId);
            if (w.body && m_roundEndTimer > 2.0) {
                ScreenLayout dummyL;
                dummyL.arenaCX = 540.0f; dummyL.arenaCY = 691.0f;
                dummyL.ppm = 40.0f;
                auto sp = worldToScreen(dummyL, w.currPos);
                if (static_cast<int>(m_globalTime * 15) % 3 == 0)
                    emitParticles(sp, w.color, 3, 120.0f, 0.8f, 3.0f);
            }
        }

        m_roundEndTimer -= dt;
        if (m_roundEndTimer <= 0.0) resetForNextRound();
        break;
    }
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Rendering
// ═════════════════════════════════════════════════════════════════════════════

void CountryElimination::render(sf::RenderTarget& target, double alpha) {
    auto L = computeLayout(target);

    // Clear with dark background
    if (auto* rt = dynamic_cast<sf::RenderTexture*>(&target)) {
        rt->clear(sf::Color(12, 14, 28));
    }

    renderBackground(target, L);

    // Desktop: dim side areas + subtle edge lines
    if (L.isDesktop) {
        // Left dim
        sf::RectangleShape leftDim(sf::Vector2f(L.safeLeft, L.H));
        leftDim.setPosition(0.0f, 0.0f);
        leftDim.setFillColor(sf::Color(0, 0, 0, 60));
        target.draw(leftDim);
        // Right dim
        sf::RectangleShape rightDim(sf::Vector2f(L.W - L.safeRight, L.H));
        rightDim.setPosition(L.safeRight, 0.0f);
        rightDim.setFillColor(sf::Color(0, 0, 0, 60));
        target.draw(rightDim);
        // Left border line
        sf::RectangleShape leftLine(sf::Vector2f(1.5f, L.H));
        leftLine.setPosition(L.safeLeft, 0.0f);
        leftLine.setFillColor(sf::Color(80, 120, 180, 60));
        target.draw(leftLine);
        // Right border line
        sf::RectangleShape rightLine(sf::Vector2f(1.5f, L.H));
        rightLine.setPosition(L.safeRight, 0.0f);
        rightLine.setFillColor(sf::Color(80, 120, 180, 60));
        target.draw(rightLine);
    }

    renderArena(target, L);
    renderPlayers(target, L, alpha);
    renderParticles(target);
    renderEliminationFeed(target, L);
    renderUI(target, L);
    renderRoundWinners(target, L);

    if (L.isDesktop) renderSidePanels(target, L);

    if (m_phase == GamePhase::Battle)
        renderTimer(target, L);
    if (m_phase == GamePhase::Countdown)
        renderCountdown(target, L);
    if (m_phase == GamePhase::RoundEnd)
        renderWinnerOverlay(target, L);

    if (auto* rt = dynamic_cast<sf::RenderTexture*>(&target)) {
        m_postProcessing.applyVignette(*rt, 0.45f);
    }
}

// ── Background ───────────────────────────────────────────────────────────────

void CountryElimination::renderBackground(sf::RenderTarget& target, const ScreenLayout& L) {
    // Star field
    m_background.render(target, 1.0f, L.W / 2.0f, L.H / 2.0f);

    // Radial glow behind arena (subtle atmosphere)
    float glowR = L.arenaRadiusPx * 1.4f;
    int steps = 30;
    sf::VertexArray glow(sf::TriangleFan, steps + 2);
    glow[0].position = { L.arenaCX, L.arenaCY };
    glow[0].color = sf::Color(30, 50, 90, 40);
    for (int i = 0; i <= steps; ++i) {
        float a = TAU * i / steps;
        glow[i + 1].position = { L.arenaCX + glowR * std::cos(a),
                                  L.arenaCY + glowR * std::sin(a) };
        glow[i + 1].color = sf::Color(12, 14, 28, 0);
    }
    target.draw(glow);

    // Arena interior — slightly darker disc
    sf::CircleShape interior(L.arenaRadiusPx - 4.0f, 80);
    interior.setOrigin(L.arenaRadiusPx - 4.0f, L.arenaRadiusPx - 4.0f);
    interior.setPosition(L.arenaCX, L.arenaCY);
    interior.setFillColor(sf::Color(8, 10, 22, 180));
    target.draw(interior);
}

// ── Arena Ring ───────────────────────────────────────────────────────────────

void CountryElimination::renderArena(sf::RenderTarget& target, const ScreenLayout& L) {
    float r = L.arenaRadiusPx;
    float thickness = WALL_THICKNESS * L.ppm * 1.2f;
    float rOuter = r + thickness / 2.0f;
    float rInner = r - thickness / 2.0f;

    // Glow pulsing intensity
    float glowPulse = 0.6f + 0.4f * std::sin(m_arenaGlowPhase);
    bool hasGap = m_currentGapAngle > 0.001f;

    // Outer glow ring
    {
        float glowT = thickness * 2.5f;
        float grO = r + glowT / 2.0f;
        float grI = r - glowT / 2.0f;
        sf::Uint8 ga = static_cast<sf::Uint8>(20 * glowPulse);

        sf::VertexArray glowRing(sf::TriangleStrip, (RING_RESOLUTION + 1) * 2);
        int vi = 0;
        float angleStep = TAU / RING_RESOLUTION;
        for (int i = 0; i <= RING_RESOLUTION; ++i) {
            float a = m_arenaAngle + i * angleStep;
            float baseA = i * angleStep;
            float normA = baseA;
            if (normA > PI) normA -= TAU;
            bool inGap = hasGap && std::abs(normA) < m_currentGapAngle;

            sf::Uint8 alpha = inGap ? 0 : ga;

            glowRing[vi].position = { L.arenaCX + grO * std::cos(a), L.arenaCY + grO * std::sin(a) };
            glowRing[vi].color = sf::Color(80, 140, 255, 0);
            vi++;
            glowRing[vi].position = { L.arenaCX + grI * std::cos(a), L.arenaCY + grI * std::sin(a) };
            glowRing[vi].color = sf::Color(80, 140, 255, alpha);
            vi++;
        }
        target.draw(glowRing);
    }

    // Main ring
    {
        sf::VertexArray ring(sf::TriangleStrip, (RING_RESOLUTION + 1) * 2);
        int vi = 0;
        float angleStep = TAU / RING_RESOLUTION;
        sf::Color ringColor(180, 200, 240, 230);

        for (int i = 0; i <= RING_RESOLUTION; ++i) {
            float a = m_arenaAngle + i * angleStep;
            float baseA = i * angleStep;
            float normA = baseA;
            if (normA > PI) normA -= TAU;
            bool inGap = hasGap && std::abs(normA) < m_currentGapAngle;

            float gapDist = std::abs(normA) - m_currentGapAngle;
            float edgeFade = 1.0f;
            if (!hasGap) edgeFade = 1.0f;
            else if (inGap) edgeFade = 0.0f;
            else if (gapDist < 0.08f) edgeFade = gapDist / 0.08f;

            sf::Uint8 alpha = static_cast<sf::Uint8>(ringColor.a * edgeFade);
            sf::Color c(ringColor.r, ringColor.g, ringColor.b, alpha);

            ring[vi].position = { L.arenaCX + rOuter * std::cos(a), L.arenaCY + rOuter * std::sin(a) };
            ring[vi].color = c;
            vi++;
            ring[vi].position = { L.arenaCX + rInner * std::cos(a), L.arenaCY + rInner * std::sin(a) };
            ring[vi].color = c;
            vi++;
        }
        target.draw(ring);
    }

    // Gap indicators (only when gap is open)
    if (hasGap) {
        for (float sign : {-1.0f, 1.0f}) {
            float edgeAngle = m_arenaAngle + m_currentGapAngle * sign;
            float ex = L.arenaCX + r * std::cos(edgeAngle);
            float ey = L.arenaCY + r * std::sin(edgeAngle);
            sf::CircleShape dot(thickness * 0.6f);
            dot.setOrigin(thickness * 0.6f, thickness * 0.6f);
            dot.setPosition(ex, ey);
            dot.setFillColor(sf::Color(255, 120, 80, static_cast<sf::Uint8>(180 * glowPulse)));
            target.draw(dot);
        }

        // Arrow pointing into the gap
        float gapAngle = m_arenaAngle;
        float arrowDist = r + thickness + 12.0f;
        float ax = L.arenaCX + arrowDist * std::cos(gapAngle);
        float ay = L.arenaCY + arrowDist * std::sin(gapAngle);

        sf::ConvexShape arrow(3);
        float arrowSize = std::max(8.0f, thickness * 0.8f);
        float perpAngle = gapAngle + PI / 2.0f;
        arrow.setPoint(0, { ax + arrowSize * std::cos(gapAngle + PI), ay + arrowSize * std::sin(gapAngle + PI) });
        arrow.setPoint(1, { ax + arrowSize * 0.6f * std::cos(perpAngle), ay + arrowSize * 0.6f * std::sin(perpAngle) });
        arrow.setPoint(2, { ax - arrowSize * 0.6f * std::cos(perpAngle), ay - arrowSize * 0.6f * std::sin(perpAngle) });
        arrow.setFillColor(sf::Color(255, 100, 70, static_cast<sf::Uint8>(200 * glowPulse)));
        target.draw(arrow);
    }
}

// ── Players ──────────────────────────────────────────────────────────────────

void CountryElimination::renderPlayers(sf::RenderTarget& target, const ScreenLayout& L, double alpha) {
    float a = static_cast<float>(alpha);

    for (const auto& [id, p] : m_players) {
        if (!p.body) continue;

        float ix = p.prevPos.x + (p.currPos.x - p.prevPos.x) * a;
        float iy = p.prevPos.y + (p.currPos.y - p.prevPos.y) * a;
        sf::Vector2f sp = worldToScreen(L, ix, iy);
        float rpx = p.radiusM * L.ppm;

        // Don't draw if way off screen
        if (sp.x < -rpx * 4 || sp.x > L.W + rpx * 4 ||
            sp.y < -rpx * 4 || sp.y > L.H + rpx * 4) continue;

        sf::Uint8 baseAlpha = p.alive ? 255 : 120;

        // Apply fade-out for eliminated balls in the FIFO queue
        if (!p.alive) {
            for (const auto& eb : m_eliminatedQueue) {
                if (eb.playerId == id && eb.fading) {
                    float fade = 1.0f - std::min(1.0f, eb.fadeProgress);
                    baseAlpha = static_cast<sf::Uint8>(baseAlpha * fade);
                    break;
                }
            }
            if (baseAlpha == 0) continue;
        }

        // Shadow behind ball
        {
            sf::CircleShape shadow(rpx + 2.0f, 32);
            shadow.setOrigin(rpx + 2.0f, rpx + 2.0f);
            shadow.setPosition(sp.x + 2.0f, sp.y + 2.0f);
            shadow.setFillColor(sf::Color(0, 0, 0, baseAlpha / 3));
            target.draw(shadow);
        }

        // Ball body
        {
            sf::CircleShape ball(rpx, 32);
            ball.setOrigin(rpx, rpx);
            ball.setPosition(sp);

            std::string labelUp = p.label;
            for (auto& c : labelUp) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            auto flagIt = m_flagTextures.find(labelUp);
            bool hasFlag = (flagIt != m_flagTextures.end());

            if (hasFlag) {
                ball.setFillColor(sf::Color(255, 255, 255, baseAlpha));
                auto& tex = flagIt->second;
                ball.setTexture(&tex);
                // Center-square crop for rectangular flags
                auto ts = tex.getSize();
                int side = static_cast<int>(std::min(ts.x, ts.y));
                int ox = (static_cast<int>(ts.x) - side) / 2;
                int oy = (static_cast<int>(ts.y) - side) / 2;
                ball.setTextureRect(sf::IntRect(ox, oy, side, side));
            } else {
                sf::Color fill = p.color;
                fill.a = baseAlpha;
                ball.setFillColor(fill);
            }

            ball.setOutlineColor(sf::Color(255, 255, 255, static_cast<sf::Uint8>(baseAlpha * 0.7f)));
            ball.setOutlineThickness(std::max(1.5f, rpx * 0.08f));
            target.draw(ball);

            // Inner highlight (only for non-flag balls)
            if (!hasFlag) {
                float hlR = rpx * 0.35f;
                sf::CircleShape hl(hlR, 16);
                hl.setOrigin(hlR, hlR);
                hl.setPosition(sp.x - rpx * 0.25f, sp.y - rpx * 0.25f);
                hl.setFillColor(sf::Color(255, 255, 255, static_cast<sf::Uint8>(baseAlpha * 0.25f)));
                target.draw(hl);
            }
        }

        // Shield glow
        if (p.hasShield && p.alive) {
            float pulse = 0.5f + 0.5f * std::sin(m_globalTime * 5.0f);
            sf::Uint8 sAlpha = static_cast<sf::Uint8>(120 + 100 * pulse);
            sf::CircleShape shield(rpx + 5.0f, 32);
            shield.setOrigin(rpx + 5.0f, rpx + 5.0f);
            shield.setPosition(sp);
            shield.setFillColor(sf::Color::Transparent);
            shield.setOutlineColor(sf::Color(80, 200, 255, sAlpha));
            shield.setOutlineThickness(3.0f);
            target.draw(shield);
        }

        // Label text on ball
        if (m_fontLoaded) {
            sf::Text lbl;
            lbl.setFont(m_font);
            lbl.setString(p.label);
            lbl.setCharacterSize(fs(static_cast<int>(rpx * 0.85f)));
            lbl.setFillColor(sf::Color(255, 255, 255, baseAlpha));
            lbl.setOutlineColor(sf::Color(0, 0, 0, static_cast<sf::Uint8>(baseAlpha * 0.8f)));
            lbl.setOutlineThickness(1.5f);
            auto lb = lbl.getLocalBounds();
            lbl.setOrigin(lb.left + lb.width / 2.0f, lb.top + lb.height / 2.0f);
            lbl.setPosition(sp);
            target.draw(lbl);
        }

        // Name below (alive only)
        if (m_fontLoaded && p.alive) {
            sf::Text name;
            name.setFont(m_font);
            name.setString(p.displayName);
            name.setCharacterSize(fs(11));
            name.setFillColor(sf::Color(255, 255, 255, 180));
            name.setOutlineColor(sf::Color(0, 0, 0, 140));
            name.setOutlineThickness(1.0f);
            auto nb = name.getLocalBounds();
            name.setOrigin(nb.left + nb.width / 2.0f, 0.0f);
            name.setPosition(sp.x, sp.y + rpx + 4.0f);
            target.draw(name);
        }
    }
}

// ── Particles ────────────────────────────────────────────────────────────────

void CountryElimination::renderParticles(sf::RenderTarget& target) {
    if (m_particles.empty()) return;
    sf::VertexArray va(sf::Quads, m_particles.size() * 4);
    int vi = 0;
    for (const auto& p : m_particles) {
        float hs = p.size / 2.0f;
        va[vi + 0].position = { p.pos.x - hs, p.pos.y - hs };
        va[vi + 1].position = { p.pos.x + hs, p.pos.y - hs };
        va[vi + 2].position = { p.pos.x + hs, p.pos.y + hs };
        va[vi + 3].position = { p.pos.x - hs, p.pos.y + hs };
        va[vi + 0].color = va[vi + 1].color = va[vi + 2].color = va[vi + 3].color = p.color;
        vi += 4;
    }
    target.draw(va);
}

// ── Battle Timer ─────────────────────────────────────────────────────────────

void CountryElimination::renderTimer(sf::RenderTarget& target, const ScreenLayout& L) {
    if (!m_fontLoaded) return;

    int totalSec = static_cast<int>(m_roundTimer);
    int min = totalSec / 60;
    int sec = totalSec % 60;

    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d:%02d", min, sec);

    sf::Text timer;
    timer.setFont(m_font);
    timer.setString(buf);
    timer.setCharacterSize(fs(50));
    timer.setFillColor(sf::Color(255, 255, 255, 50));
    timer.setOutlineColor(sf::Color(0, 0, 0, 30));
    timer.setOutlineThickness(2.0f);
    auto lb = timer.getLocalBounds();
    timer.setOrigin(lb.left + lb.width / 2.0f, lb.top + lb.height / 2.0f);
    timer.setPosition(L.arenaCX, L.arenaCY);
    target.draw(timer);
}

// ── UI ───────────────────────────────────────────────────────────────────────

void CountryElimination::renderUI(sf::RenderTarget& target, const ScreenLayout& L) {
    if (!m_fontLoaded) return;

    float cx = (L.safeLeft + L.safeRight) / 2.0f;

    // Title
    {
        auto r = resolve("title", L.W, L.H);
        if (r.visible) {
            sf::Text t;
            t.setFont(m_font);
            t.setString("COUNTRY ELIMINATION");
            sf::Color col(255, 255, 255, 238);
            parseHexColor(r.color, col);
            t.setFillColor(col);
            t.setOutlineColor(sf::Color(0, 0, 0, 200));
            t.setOutlineThickness(2.0f);
            t.setCharacterSize(r.fontSize);
            auto lb = t.getLocalBounds();
            t.setOrigin(lb.left + lb.width / 2.0f, lb.top + lb.height / 2.0f);
            t.setPosition(cx, r.py);
            target.draw(t);
        }
    }

    // Phase
    {
        auto r = resolve("phase", L.W, L.H);
        if (r.visible) {
            std::string str;
            switch (m_phase) {
            case GamePhase::Lobby:     str = "Waiting for players..."; break;
            case GamePhase::Countdown: str = "Get ready!"; break;
            case GamePhase::Battle:    str = "Round " + std::to_string(m_roundNumber); break;
            case GamePhase::RoundEnd:  str = "Round Over!"; break;
            }
            sf::Text t;
            t.setFont(m_font);
            t.setString(str);
            sf::Color col(200, 200, 200, 200);
            parseHexColor(r.color, col);
            t.setFillColor(col);
            t.setOutlineColor(sf::Color(0, 0, 0, 160));
            t.setOutlineThickness(1.0f);
            t.setCharacterSize(r.fontSize);
            auto lb = t.getLocalBounds();
            t.setOrigin(lb.left + lb.width / 2.0f, lb.top + lb.height / 2.0f);
            t.setPosition(cx, r.py);
            target.draw(t);
        }
    }

    // Player count
    {
        auto r = resolve("player_count", L.W, L.H);
        if (r.visible) {
            int alive = 0, total = 0;
            for (const auto& [_, p] : m_players) { total++; if (p.alive) alive++; }
            sf::Text t;
            t.setFont(m_font);
            t.setString(std::to_string(alive) + " / " + std::to_string(total) + " alive");
            sf::Color col(255, 255, 255, 187);
            parseHexColor(r.color, col);
            t.setFillColor(col);
            t.setOutlineColor(sf::Color(0, 0, 0, 120));
            t.setOutlineThickness(1.0f);
            t.setCharacterSize(r.fontSize);
            auto lb = t.getLocalBounds();
            t.setOrigin(lb.left + lb.width / 2.0f, lb.top + lb.height / 2.0f);
            t.setPosition(cx, r.py);
            target.draw(t);
        }
    }

    // Join hint
    {
        auto r = resolve("join_hint", L.W, L.H);
        if (r.visible && (m_phase == GamePhase::Lobby || m_phase == GamePhase::Battle)) {
            sf::Text t;
            t.setFont(m_font);
            t.setString("Type  join <country>  to play!");
            sf::Color col(255, 255, 255, 170);
            parseHexColor(r.color, col);
            t.setFillColor(col);
            t.setOutlineColor(sf::Color(0, 0, 0, 120));
            t.setOutlineThickness(1.0f);
            t.setCharacterSize(r.fontSize);
            auto lb = t.getLocalBounds();
            t.setOrigin(lb.left + lb.width / 2.0f, lb.top + lb.height / 2.0f);
            t.setPosition(cx, r.py);
            target.draw(t);
        }
    }

    // Sub reward info
    {
        auto r = resolve("sub_info", L.W, L.H);
        if (r.visible) {
            sf::Text t;
            t.setFont(m_font);
            t.setString("SUB = Shield + 300pts  |  BITS/SC > 100 = Big Ball + Shield + 500pts");
            sf::Color col(170, 170, 204, 153);
            parseHexColor(r.color, col);
            t.setFillColor(col);
            t.setOutlineColor(sf::Color(0, 0, 0, 80));
            t.setOutlineThickness(1.0f);
            t.setCharacterSize(r.fontSize);
            auto lb = t.getLocalBounds();
            t.setOrigin(lb.left + lb.width / 2.0f, lb.top + lb.height / 2.0f);
            t.setPosition(cx, r.py);
            target.draw(t);
        }
    }
}

// ── Countdown ────────────────────────────────────────────────────────────────

void CountryElimination::renderCountdown(sf::RenderTarget& target, const ScreenLayout& L) {
    if (!m_fontLoaded) return;

    int num = static_cast<int>(std::ceil(m_countdownTimer));
    if (num <= 0) return;

    // Background dim
    sf::RectangleShape dim(sf::Vector2f(L.W, L.H));
    dim.setFillColor(sf::Color(0, 0, 0, 80));
    target.draw(dim);

    // Expanding ring effect
    float frac = static_cast<float>(m_countdownTimer) - std::floor(static_cast<float>(m_countdownTimer));
    float ringR = L.arenaRadiusPx * (0.3f + (1.0f - frac) * 0.4f);
    sf::CircleShape ring(ringR, 60);
    ring.setOrigin(ringR, ringR);
    ring.setPosition(L.arenaCX, L.arenaCY);
    ring.setFillColor(sf::Color::Transparent);
    ring.setOutlineColor(sf::Color(255, 255, 255, static_cast<sf::Uint8>(frac * 150)));
    ring.setOutlineThickness(3.0f);
    target.draw(ring);

    // Number
    sf::Text t;
    t.setFont(m_font);
    t.setString(std::to_string(num));
    t.setCharacterSize(fs(90));
    t.setFillColor(sf::Color(255, 255, 255, 240));
    t.setOutlineColor(sf::Color(0, 0, 0, 220));
    t.setOutlineThickness(3.0f);
    auto lb = t.getLocalBounds();
    t.setOrigin(lb.left + lb.width / 2.0f, lb.top + lb.height / 2.0f);
    t.setPosition(L.arenaCX, L.arenaCY);

    float scale = 1.0f + frac * 0.3f;
    t.setScale(scale, scale);
    target.draw(t);
}

// ── Round Winners Panel ──────────────────────────────────────────────────────

void CountryElimination::renderRoundWinners(sf::RenderTarget& target, const ScreenLayout& L) {
    if (!m_fontLoaded || m_roundWinners.empty()) return;

    // Panel position: top-right of safe zone
    float panelW = std::min(280.0f, L.safeW * 0.5f);
    float panelX = L.safeRight - panelW - 10.0f;
    float panelY = L.H * 0.015f;
    int maxShow = std::min(static_cast<int>(m_roundWinners.size()), 10);

    float lineH = fs(16) + 6.0f;
    float headerH = fs(14) + fs(10) + 18.0f;
    float panelH = headerH + maxShow * lineH + 10.0f;

    // Semi-transparent background
    sf::RectangleShape bg(sf::Vector2f(panelW, panelH));
    bg.setPosition(panelX, panelY);
    bg.setFillColor(sf::Color(0, 0, 0, 140));
    bg.setOutlineColor(sf::Color(255, 255, 255, 40));
    bg.setOutlineThickness(1.0f);
    target.draw(bg);

    // Header
    {
        sf::Text header;
        header.setFont(m_font);
        header.setString("Round Winners (Top 10)");
        header.setCharacterSize(fs(14));
        header.setFillColor(sf::Color(255, 215, 0, 220));
        header.setOutlineColor(sf::Color(0, 0, 0, 200));
        header.setOutlineThickness(1.0f);
        auto hb = header.getLocalBounds();
        header.setOrigin(hb.left + hb.width / 2.0f, 0.0f);
        header.setPosition(panelX + panelW / 2.0f, panelY + 6.0f);
        target.draw(header);

        sf::Text sub;
        sub.setFont(m_font);
        sub.setString("First to " + std::to_string(m_championThreshold) + " wins");
        sub.setCharacterSize(fs(10));
        sub.setFillColor(sf::Color(200, 200, 200, 160));
        auto sb = sub.getLocalBounds();
        sub.setOrigin(sb.left + sb.width / 2.0f, 0.0f);
        sub.setPosition(panelX + panelW / 2.0f, panelY + fs(14) + 10.0f);
        target.draw(sub);
    }

    // Entries
    float y = panelY + headerH;
    for (int i = 0; i < maxShow; ++i) {
        const auto& rw = m_roundWinners[i];

        // Rank
        sf::Text rank;
        rank.setFont(m_font);
        rank.setString(std::to_string(i + 1) + ".");
        rank.setCharacterSize(fs(15));
        rank.setFillColor(sf::Color(200, 200, 200, 200));
        rank.setPosition(panelX + 8.0f, y);
        target.draw(rank);

        // Color dot
        float dotR = 5.0f;
        sf::CircleShape dot(dotR);
        dot.setOrigin(dotR, dotR);
        dot.setPosition(panelX + 42.0f, y + fs(15) / 2.0f + 2.0f);
        dot.setFillColor(rw.color);
        dot.setOutlineColor(sf::Color(255, 255, 255, 100));
        dot.setOutlineThickness(1.0f);
        target.draw(dot);

        // Name + label
        sf::Text name;
        name.setFont(m_font);
        std::string display = rw.displayName;
        if (display.size() > 14) display = display.substr(0, 12) + "..";
        name.setString(display + " [" + rw.label + "]");
        name.setCharacterSize(fs(13));
        name.setFillColor(sf::Color(255, 255, 255, 200));
        name.setPosition(panelX + 55.0f, y + 1.0f);
        target.draw(name);

        // Wins count
        sf::Text wins;
        wins.setFont(m_font);
        wins.setString(std::to_string(rw.wins));
        wins.setCharacterSize(fs(15));
        wins.setFillColor(sf::Color(255, 215, 0, 220));
        auto wb = wins.getLocalBounds();
        wins.setOrigin(wb.left + wb.width, 0.0f);
        wins.setPosition(panelX + panelW - 10.0f, y);
        target.draw(wins);

        y += lineH;
    }
}

// ── Winner Overlay ───────────────────────────────────────────────────────────

void CountryElimination::renderWinnerOverlay(sf::RenderTarget& target, const ScreenLayout& L) {
    if (m_winnerId.empty() || !m_players.count(m_winnerId)) return;
    if (!m_fontLoaded) return;

    const Player& w = m_players.at(m_winnerId);

    // Semi-transparent dim overlay
    float fadeIn = std::min(1.0f, static_cast<float>(m_roundEndDuration - m_roundEndTimer) * 2.0f);
    sf::Uint8 dimAlpha = static_cast<sf::Uint8>(100 * fadeIn);
    sf::RectangleShape dim(sf::Vector2f(L.W, L.H));
    dim.setFillColor(sf::Color(0, 0, 0, dimAlpha));
    target.draw(dim);

    float cx = (L.safeLeft + L.safeRight) / 2.0f;

    // Winner ball (large)
    float bigR = L.arenaRadiusPx * 0.2f;
    float ballY = L.arenaCY - bigR * 0.5f;
    float bounce = std::sin(m_globalTime * 3.0f) * 8.0f;

    sf::CircleShape bigBall(bigR, 48);
    bigBall.setOrigin(bigR, bigR);
    bigBall.setPosition(cx, ballY + bounce);

    std::string wLabelUp = w.label;
    for (auto& c : wLabelUp) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    auto wFlagIt = m_flagTextures.find(wLabelUp);
    if (wFlagIt != m_flagTextures.end()) {
        bigBall.setFillColor(sf::Color::White);
        auto& tex = wFlagIt->second;
        bigBall.setTexture(&tex);
        auto ts = tex.getSize();
        int side = static_cast<int>(std::min(ts.x, ts.y));
        int ox = (static_cast<int>(ts.x) - side) / 2;
        int oy = (static_cast<int>(ts.y) - side) / 2;
        bigBall.setTextureRect(sf::IntRect(ox, oy, side, side));
    } else {
        bigBall.setFillColor(w.color);
    }

    bigBall.setOutlineColor(sf::Color(255, 255, 255, 200));
    bigBall.setOutlineThickness(3.0f);
    target.draw(bigBall);

    // Label on big ball
    sf::Text lbl;
    lbl.setFont(m_font);
    lbl.setString(w.label);
    lbl.setCharacterSize(fs(static_cast<int>(bigR * 0.7f)));
    lbl.setFillColor(sf::Color::White);
    lbl.setOutlineColor(sf::Color(0, 0, 0, 200));
    lbl.setOutlineThickness(2.0f);
    auto llb = lbl.getLocalBounds();
    lbl.setOrigin(llb.left + llb.width / 2.0f, llb.top + llb.height / 2.0f);
    lbl.setPosition(cx, ballY + bounce);
    target.draw(lbl);

    // "WINNER!" text above
    {
        auto r = resolve("winner_text", L.W, L.H);
        sf::Text t;
        t.setFont(m_font);
        t.setString("WINNER!");
        sf::Color col(255, 215, 0);
        parseHexColor(r.color, col);
        col.a = static_cast<sf::Uint8>(255 * fadeIn);
        t.setFillColor(col);
        t.setOutlineColor(sf::Color(0, 0, 0, static_cast<sf::Uint8>(230 * fadeIn)));
        t.setOutlineThickness(3.0f);
        t.setCharacterSize(r.fontSize);

        float pulse = 1.0f + 0.05f * std::sin(m_globalTime * 4.0f);
        t.setScale(pulse, pulse);

        auto lb = t.getLocalBounds();
        t.setOrigin(lb.left + lb.width / 2.0f, lb.top + lb.height / 2.0f);
        t.setPosition(cx, ballY - bigR - 40.0f);
        target.draw(t);
    }

    // Name + label below ball
    {
        auto r = resolve("winner_label", L.W, L.H);
        sf::Text t;
        t.setFont(m_font);
        t.setString(w.displayName);
        sf::Color col(255, 255, 255, 238);
        parseHexColor(r.color, col);
        col.a = static_cast<sf::Uint8>(238 * fadeIn);
        t.setFillColor(col);
        t.setOutlineColor(sf::Color(0, 0, 0, static_cast<sf::Uint8>(200 * fadeIn)));
        t.setOutlineThickness(2.0f);
        t.setCharacterSize(r.fontSize);
        auto lb = t.getLocalBounds();
        t.setOrigin(lb.left + lb.width / 2.0f, lb.top + lb.height / 2.0f);
        t.setPosition(cx, ballY + bigR + 35.0f);
        target.draw(t);
    }

    // Wins count
    {
        int wins = 0;
        for (const auto& rw : m_roundWinners) {
            if (rw.userId == w.userId) { wins = rw.wins; break; }
        }
        sf::Text t;
        t.setFont(m_font);
        t.setString("Wins: " + std::to_string(wins));
        t.setCharacterSize(fs(18));
        t.setFillColor(sf::Color(200, 200, 200, static_cast<sf::Uint8>(180 * fadeIn)));
        t.setOutlineColor(sf::Color(0, 0, 0, static_cast<sf::Uint8>(120 * fadeIn)));
        t.setOutlineThickness(1.0f);
        auto lb = t.getLocalBounds();
        t.setOrigin(lb.left + lb.width / 2.0f, lb.top + lb.height / 2.0f);
        t.setPosition(cx, ballY + bigR + 70.0f);
        target.draw(t);
    }

    // Next round countdown
    if (m_roundEndTimer < m_roundEndDuration) {
        sf::Text t;
        t.setFont(m_font);
        std::string nxt = "Next round in " + std::to_string(std::max(0, static_cast<int>(std::ceil(m_roundEndTimer)))) + "s";
        t.setString(nxt);
        t.setCharacterSize(fs(14));
        t.setFillColor(sf::Color(180, 180, 180, static_cast<sf::Uint8>(150 * fadeIn)));
        auto lb = t.getLocalBounds();
        t.setOrigin(lb.left + lb.width / 2.0f, lb.top + lb.height / 2.0f);
        t.setPosition(cx, ballY + bigR + 100.0f);
        target.draw(t);
    }
}

// ── Elimination Feed ─────────────────────────────────────────────────────────

void CountryElimination::renderEliminationFeed(sf::RenderTarget& target, const ScreenLayout& L) {
    if (!m_fontLoaded || m_eliminationFeed.empty()) return;

    float cx = (L.safeLeft + L.safeRight) / 2.0f;
    // Position below the arena
    float startY = L.arenaCY + L.arenaRadiusPx + 30.0f;

    for (size_t i = 0; i < m_eliminationFeed.size(); ++i) {
        const auto& e = m_eliminationFeed[i];
        float alpha = std::min(1.0f, static_cast<float>(e.timeRemaining));
        sf::Uint8 a = static_cast<sf::Uint8>(alpha * 200);

        // Background pill
        float entryW = 240.0f;
        float entryH = fs(14) + 8.0f;
        sf::RectangleShape pill(sf::Vector2f(entryW, entryH));
        pill.setOrigin(entryW / 2.0f, 0.0f);
        pill.setPosition(cx, startY);
        pill.setFillColor(sf::Color(80, 20, 20, static_cast<sf::Uint8>(a / 2)));
        target.draw(pill);

        // Color dot
        sf::CircleShape dot(4.0f);
        dot.setOrigin(4.0f, 4.0f);
        dot.setPosition(cx - entryW / 2.0f + 12.0f, startY + entryH / 2.0f);
        sf::Color dc = e.color;
        dc.a = a;
        dot.setFillColor(dc);
        target.draw(dot);

        // Text
        sf::Text t;
        t.setFont(m_font);
        t.setString("X  " + e.displayName + " [" + e.label + "]");
        t.setCharacterSize(fs(13));
        t.setFillColor(sf::Color(255, 100, 100, a));
        t.setOutlineColor(sf::Color(0, 0, 0, static_cast<sf::Uint8>(a * 0.6f)));
        t.setOutlineThickness(1.0f);
        auto lb = t.getLocalBounds();
        t.setOrigin(lb.left, lb.top + lb.height / 2.0f);
        t.setPosition(cx - entryW / 2.0f + 24.0f, startY + entryH / 2.0f);
        target.draw(t);

        startY += entryH + 3.0f;
    }
}

// ── Side Panels (desktop only) ───────────────────────────────────────────────

void CountryElimination::renderSidePanels(sf::RenderTarget& target, const ScreenLayout& L) {
    if (!m_fontLoaded) return;

    const float pad = 12.0f;
    const float cornerR = 6.0f;
    const float lineH = fs(15) + 5.0f;

    // Helper: draw a card background with subtle border
    auto drawCard = [&](float x, float y, float w, float h) {
        sf::RectangleShape bg(sf::Vector2f(w, h));
        bg.setPosition(x, y);
        bg.setFillColor(sf::Color(10, 14, 30, 180));
        bg.setOutlineColor(sf::Color(80, 120, 200, 50));
        bg.setOutlineThickness(1.0f);
        target.draw(bg);
    };

    // Helper: draw a card header
    auto drawHeader = [&](float x, float y, float w, const std::string& title, sf::Color col) {
        sf::RectangleShape hdr(sf::Vector2f(w, fs(16) + 10.0f));
        hdr.setPosition(x, y);
        hdr.setFillColor(sf::Color(col.r, col.g, col.b, 40));
        target.draw(hdr);

        sf::Text t;
        t.setFont(m_font);
        t.setString(title);
        t.setCharacterSize(fs(13));
        t.setFillColor(col);
        t.setOutlineColor(sf::Color(0, 0, 0, 180));
        t.setOutlineThickness(1.0f);
        auto lb = t.getLocalBounds();
        t.setOrigin(lb.left + lb.width / 2.0f, lb.top + lb.height / 2.0f);
        t.setPosition(x + w / 2.0f, y + (fs(16) + 10.0f) / 2.0f);
        target.draw(t);
        return y + fs(16) + 10.0f + 6.0f;
    };

    // ── LEFT PANEL: Leaderboard ──────────────────────────────────────────
    if (L.leftPanelW > 60.0f) {
        float px = L.leftPanelX;
        float pw = L.leftPanelW;
        float py = pad;

        // Card: Round Winners
        {
            int maxShow = std::min(static_cast<int>(m_roundWinners.size()), 10);
            float headerH = fs(16) + 10.0f;
            float cardH = headerH + 6.0f + std::max(1, maxShow) * lineH + pad;
            drawCard(px, py, pw, cardH);
            float cy = drawHeader(px, py, pw, "LEADERBOARD", sf::Color(255, 215, 0, 220));

            if (m_roundWinners.empty()) {
                sf::Text t;
                t.setFont(m_font);
                t.setString("No winners yet");
                t.setCharacterSize(fs(11));
                t.setFillColor(sf::Color(150, 150, 150, 140));
                auto lb = t.getLocalBounds();
                t.setOrigin(lb.left + lb.width / 2.0f, 0.0f);
                t.setPosition(px + pw / 2.0f, cy);
                target.draw(t);
            } else {
                for (int i = 0; i < maxShow; ++i) {
                    const auto& rw = m_roundWinners[i];
                    float ey = cy + i * lineH;

                    // Rank number
                    sf::Text rank;
                    rank.setFont(m_font);
                    rank.setString(std::to_string(i + 1) + ".");
                    rank.setCharacterSize(fs(13));
                    rank.setFillColor(i == 0 ? sf::Color(255, 215, 0, 220) : sf::Color(180, 180, 180, 200));
                    rank.setPosition(px + 6.0f, ey);
                    target.draw(rank);

                    // Flag circle
                    float flagR = fs(13) * 0.5f;
                    sf::CircleShape flag(flagR);
                    flag.setOrigin(flagR, flagR);
                    flag.setPosition(px + 30.0f, ey + lineH / 2.0f);
                    auto fit = m_flagTextures.find(rw.label);
                    if (fit != m_flagTextures.end()) {
                        flag.setTexture(&fit->second);
                        auto ts = fit->second.getSize();
                        int sq = std::min(ts.x, ts.y);
                        int ox = (ts.x - sq) / 2;
                        int oy = (ts.y - sq) / 2;
                        flag.setTextureRect(sf::IntRect(ox, oy, sq, sq));
                        flag.setFillColor(sf::Color::White);
                    } else {
                        flag.setFillColor(rw.color);
                    }
                    flag.setOutlineColor(sf::Color(255, 255, 255, 80));
                    flag.setOutlineThickness(1.0f);
                    target.draw(flag);

                    // Name
                    sf::Text name;
                    name.setFont(m_font);
                    std::string disp = rw.displayName;
                    int maxChars = static_cast<int>(pw - 80.0f) / std::max(1, static_cast<int>(fs(11) * 0.6f));
                    if (static_cast<int>(disp.size()) > maxChars && maxChars > 3)
                        disp = disp.substr(0, maxChars - 2) + "..";
                    name.setString(disp);
                    name.setCharacterSize(fs(11));
                    name.setFillColor(sf::Color(230, 230, 240, 210));
                    name.setPosition(px + 30.0f + flagR + 6.0f, ey + 1.0f);
                    target.draw(name);

                    // Wins
                    sf::Text wins;
                    wins.setFont(m_font);
                    wins.setString(std::to_string(rw.wins) + "W");
                    wins.setCharacterSize(fs(13));
                    wins.setFillColor(sf::Color(255, 215, 0, 200));
                    auto wb = wins.getLocalBounds();
                    wins.setOrigin(wb.left + wb.width, 0.0f);
                    wins.setPosition(px + pw - 8.0f, ey);
                    target.draw(wins);
                }
            }
            py += cardH + pad;
        }

        // Card: Recent Eliminations
        {
            int maxShow = std::min(static_cast<int>(m_eliminationFeed.size()), 8);
            float headerH = fs(16) + 10.0f;
            float cardH = headerH + 6.0f + std::max(1, maxShow) * lineH + pad;
            drawCard(px, py, pw, cardH);
            float cy = drawHeader(px, py, pw, "ELIMINATIONS", sf::Color(255, 80, 80, 220));

            if (m_eliminationFeed.empty()) {
                sf::Text t;
                t.setFont(m_font);
                t.setString("None yet");
                t.setCharacterSize(fs(11));
                t.setFillColor(sf::Color(150, 150, 150, 140));
                auto lb = t.getLocalBounds();
                t.setOrigin(lb.left + lb.width / 2.0f, 0.0f);
                t.setPosition(px + pw / 2.0f, cy);
                target.draw(t);
            } else {
                for (int i = 0; i < maxShow; ++i) {
                    const auto& e = m_eliminationFeed[i];
                    float ey = cy + i * lineH;
                    float alpha = std::min(1.0f, static_cast<float>(e.timeRemaining));
                    sf::Uint8 a = static_cast<sf::Uint8>(alpha * 200);

                    sf::CircleShape dot(4.0f);
                    dot.setOrigin(4.0f, 4.0f);
                    dot.setPosition(px + 10.0f, ey + lineH / 2.0f);
                    sf::Color dc = e.color; dc.a = a;
                    dot.setFillColor(dc);
                    target.draw(dot);

                    sf::Text t;
                    t.setFont(m_font);
                    t.setString(e.displayName + " [" + e.label + "]");
                    t.setCharacterSize(fs(11));
                    t.setFillColor(sf::Color(255, 120, 120, a));
                    t.setOutlineColor(sf::Color(0, 0, 0, static_cast<sf::Uint8>(a * 0.5f)));
                    t.setOutlineThickness(1.0f);
                    t.setPosition(px + 22.0f, ey);
                    target.draw(t);
                }
            }
            py += cardH + pad;
        }
    }

    // ── RIGHT PANEL: Stats ───────────────────────────────────────────────
    if (L.rightPanelW > 60.0f) {
        float px = L.rightPanelX;
        float pw = L.rightPanelW;
        float py = pad;

        // Card: Game Stats
        {
            int alive = 0, total = 0;
            for (const auto& [_, p] : m_players) { total++; if (p.alive) alive++; }

            struct StatLine { std::string label; std::string value; sf::Color col; };
            std::vector<StatLine> stats;
            stats.push_back({"Players", std::to_string(alive) + " / " + std::to_string(total), sf::Color(100, 200, 255)});
            stats.push_back({"Round", std::to_string(m_roundNumber), sf::Color(200, 200, 200)});

            std::string phaseStr;
            switch (m_phase) {
            case GamePhase::Lobby:     phaseStr = "Lobby"; break;
            case GamePhase::Countdown: phaseStr = "Countdown"; break;
            case GamePhase::Battle:    phaseStr = "Battle"; break;
            case GamePhase::RoundEnd:  phaseStr = "Round End"; break;
            }
            stats.push_back({"Phase", phaseStr, sf::Color(180, 200, 160)});

            if (m_phase == GamePhase::Battle) {
                int sec = static_cast<int>(m_roundDuration - m_roundTimer);
                stats.push_back({"Time Left", std::to_string(std::max(0, sec)) + "s", sf::Color(255, 180, 80)});
            }
            stats.push_back({"Champion At", std::to_string(m_championThreshold) + " wins", sf::Color(255, 215, 0)});

            float headerH = fs(16) + 10.0f;
            float cardH = headerH + 6.0f + static_cast<float>(stats.size()) * lineH + pad;
            drawCard(px, py, pw, cardH);
            float cy = drawHeader(px, py, pw, "GAME STATS", sf::Color(100, 180, 255, 220));

            for (size_t i = 0; i < stats.size(); ++i) {
                float ey = cy + i * lineH;
                sf::Text lbl;
                lbl.setFont(m_font);
                lbl.setString(stats[i].label);
                lbl.setCharacterSize(fs(11));
                lbl.setFillColor(sf::Color(160, 160, 180, 180));
                lbl.setPosition(px + 8.0f, ey);
                target.draw(lbl);

                sf::Text val;
                val.setFont(m_font);
                val.setString(stats[i].value);
                val.setCharacterSize(fs(12));
                val.setFillColor(stats[i].col);
                auto vb = val.getLocalBounds();
                val.setOrigin(vb.left + vb.width, 0.0f);
                val.setPosition(px + pw - 8.0f, ey);
                target.draw(val);
            }
            py += cardH + pad;
        }

        // Card: Arena Info
        {
            struct StatLine { std::string label; std::string value; sf::Color col; };
            std::vector<StatLine> info;

            // Arena speed
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.2f", m_arenaAngularVel);
            info.push_back({"Speed", std::string(buf) + " rad/s", sf::Color(180, 160, 255)});

            // Gap size
            std::snprintf(buf, sizeof(buf), "%.2f", m_currentGapAngle);
            info.push_back({"Gap", std::string(buf) + " rad", sf::Color(255, 140, 100)});

            // Ball speed
            std::snprintf(buf, sizeof(buf), "%.1f", m_currentBallSpeed);
            info.push_back({"Ball Speed", std::string(buf), sf::Color(100, 255, 180)});

            // Bots
            int botCount = 0;
            for (const auto& [_, p] : m_players) { if (p.isBot()) botCount++; }
            info.push_back({"Bots", std::to_string(botCount), sf::Color(180, 180, 180)});

            float headerH = fs(16) + 10.0f;
            float cardH = headerH + 6.0f + static_cast<float>(info.size()) * lineH + pad;
            drawCard(px, py, pw, cardH);
            float cy = drawHeader(px, py, pw, "ARENA INFO", sf::Color(180, 140, 255, 220));

            for (size_t i = 0; i < info.size(); ++i) {
                float ey = cy + i * lineH;
                sf::Text lbl;
                lbl.setFont(m_font);
                lbl.setString(info[i].label);
                lbl.setCharacterSize(fs(11));
                lbl.setFillColor(sf::Color(160, 160, 180, 180));
                lbl.setPosition(px + 8.0f, ey);
                target.draw(lbl);

                sf::Text val;
                val.setFont(m_font);
                val.setString(info[i].value);
                val.setCharacterSize(fs(12));
                val.setFillColor(info[i].col);
                auto vb = val.getLocalBounds();
                val.setOrigin(vb.left + vb.width, 0.0f);
                val.setPosition(px + pw - 8.0f, ey);
                target.draw(val);
            }
            py += cardH + pad;
        }

        // Card: How to Play
        {
            std::vector<std::string> lines = {
                "Type  join <country>",
                "to enter the arena!",
                "",
                "Survive the gap in",
                "the rotating ring.",
                "Last ball wins!",
                "",
                "SUB = Shield",
                "BITS > 100 = Big Ball",
            };

            float headerH = fs(16) + 10.0f;
            float smallH = fs(10) + 4.0f;
            float cardH = headerH + 6.0f + lines.size() * smallH + pad;
            drawCard(px, py, pw, cardH);
            float cy = drawHeader(px, py, pw, "HOW TO PLAY", sf::Color(100, 255, 150, 220));

            for (size_t i = 0; i < lines.size(); ++i) {
                if (lines[i].empty()) continue;
                sf::Text t;
                t.setFont(m_font);
                t.setString(lines[i]);
                t.setCharacterSize(fs(10));
                t.setFillColor(sf::Color(190, 200, 210, 180));
                auto lb = t.getLocalBounds();
                t.setOrigin(lb.left + lb.width / 2.0f, 0.0f);
                t.setPosition(px + pw / 2.0f, cy + i * smallH);
                target.draw(t);
            }
            py += cardH + pad;
        }
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Color Generation
// ═════════════════════════════════════════════════════════════════════════════

sf::Color CountryElimination::generateColor() {
    std::uniform_int_distribution<int> hue(0, 359);
    int h = hue(m_rng);
    float s = 0.75f, v = 0.95f;
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
// Interface Methods
// ═════════════════════════════════════════════════════════════════════════════

bool CountryElimination::isRoundComplete() const {
    return m_phase == GamePhase::RoundEnd;
}

bool CountryElimination::isGameOver() const {
    return m_gameWon;
}

nlohmann::json CountryElimination::getState() const {
    nlohmann::json state;
    std::string ps;
    switch (m_phase) {
    case GamePhase::Lobby:     ps = "lobby"; break;
    case GamePhase::Countdown: ps = "countdown"; break;
    case GamePhase::Battle:    ps = "battle"; break;
    case GamePhase::RoundEnd:  ps = "round_end"; break;
    }
    state["phase"] = ps;
    state["round"] = m_roundNumber;
    state["roundTimer"] = m_roundTimer;
    state["championThreshold"] = m_championThreshold;
    state["gameWon"] = m_gameWon;

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

    nlohmann::json rw = nlohmann::json::array();
    for (const auto& e : m_roundWinners) {
        rw.push_back({{"name", e.displayName}, {"label", e.label}, {"wins", e.wins}});
    }
    state["roundWinners"] = rw;

    return state;
}

nlohmann::json CountryElimination::getCommands() const {
    return nlohmann::json::array({
        {{"command", "join"}, {"description", "Join with a country label"}, {"aliases", nlohmann::json::array({"play"})}},
    });
}

std::vector<std::pair<std::string, int>> CountryElimination::getLeaderboard() const {
    std::vector<std::pair<std::string, int>> result;
    for (const auto& rw : m_roundWinners) {
        if (isBotId(rw.userId)) continue;
        result.emplace_back(rw.displayName + " [" + rw.label + "]", rw.wins);
    }
    return result;
}

void CountryElimination::configure(const nlohmann::json& settings) {
    if (settings.contains("arena_speed") && settings["arena_speed"].is_number())
        m_arenaAngularVel = std::max(0.05f, settings["arena_speed"].get<float>());
    if (settings.contains("arena_speed_increase") && settings["arena_speed_increase"].is_number())
        m_arenaSpeedIncrease = std::max(0.0f, settings["arena_speed_increase"].get<float>());
    if (settings.contains("initial_speed") && settings["initial_speed"].is_number())
        m_initialSpeed = std::max(0.5f, settings["initial_speed"].get<float>());
    if (settings.contains("ball_speed_increase") && settings["ball_speed_increase"].is_number())
        m_ballSpeedIncrease = std::max(0.0f, settings["ball_speed_increase"].get<float>());
    if (settings.contains("max_ball_speed") && settings["max_ball_speed"].is_number())
        m_maxBallSpeed = std::max(1.0f, settings["max_ball_speed"].get<float>());
    if (settings.contains("restitution") && settings["restitution"].is_number())
        m_restitution = std::clamp(settings["restitution"].get<float>(), 0.0f, 1.0f);
    if (settings.contains("min_players") && settings["min_players"].is_number_integer())
        m_minPlayers = std::max(2, settings["min_players"].get<int>());
    if (settings.contains("lobby_duration") && settings["lobby_duration"].is_number())
        m_lobbyDuration = std::max(1.0, settings["lobby_duration"].get<double>());
    if (settings.contains("round_end_duration") && settings["round_end_duration"].is_number())
        m_roundEndDuration = std::max(1.0, settings["round_end_duration"].get<double>());
    if (settings.contains("champion_threshold") && settings["champion_threshold"].is_number_integer())
        m_championThreshold = std::max(2, settings["champion_threshold"].get<int>());
    if (settings.contains("gap_expansion_rate") && settings["gap_expansion_rate"].is_number())
        m_gapExpansionRate = std::max(0.0f, settings["gap_expansion_rate"].get<float>());
    if (settings.contains("gap_max") && settings["gap_max"].is_number())
        m_gapMax = std::clamp(settings["gap_max"].get<float>(), 0.3f, 2.5f);
    if (settings.contains("round_duration") && settings["round_duration"].is_number())
        m_roundDuration = std::max(10.0, settings["round_duration"].get<double>());
    if (settings.contains("bot_fill") && settings["bot_fill"].is_number_integer())
        m_botFillTarget = std::max(0, settings["bot_fill"].get<int>());
    if (settings.contains("bot_respawn") && settings["bot_respawn"].is_boolean())
        m_botRespawn = settings["bot_respawn"].get<bool>();
    if (settings.contains("bot_respawn_delay") && settings["bot_respawn_delay"].is_number())
        m_botRespawnDelay = std::max(0.5f, settings["bot_respawn_delay"].get<float>());
    if (settings.contains("max_eliminated_visible") && settings["max_eliminated_visible"].is_number_integer())
        m_maxEliminatedVisible = std::max(0, settings["max_eliminated_visible"].get<int>());
    if (settings.contains("elim_fade_duration") && settings["elim_fade_duration"].is_number())
        m_elimFadeDuration = std::max(0.1f, settings["elim_fade_duration"].get<float>());
    if (settings.contains("elim_linger_duration") && settings["elim_linger_duration"].is_number())
        m_elimLingerDuration = std::max(0.5f, settings["elim_linger_duration"].get<float>());
    if (settings.contains("text_elements") && settings["text_elements"].is_array())
        applyTextOverrides(settings["text_elements"]);

    if (m_world) spawnBots();
}

nlohmann::json CountryElimination::getSettings() const {
    return {
        {"arena_speed", m_arenaAngularVel},
        {"arena_speed_increase", m_arenaSpeedIncrease},
        {"initial_speed", m_initialSpeed},
        {"ball_speed_increase", m_ballSpeedIncrease},
        {"max_ball_speed", m_maxBallSpeed},
        {"restitution", m_restitution},
        {"min_players", m_minPlayers},
        {"lobby_duration", m_lobbyDuration},
        {"round_end_duration", m_roundEndDuration},
        {"champion_threshold", m_championThreshold},
        {"gap_expansion_rate", m_gapExpansionRate},
        {"gap_max", m_gapMax},
        {"round_duration", m_roundDuration},
        {"bot_fill", m_botFillTarget},
        {"bot_respawn", m_botRespawn},
        {"bot_respawn_delay", m_botRespawnDelay},
        {"max_eliminated_visible", m_maxEliminatedVisible},
        {"elim_fade_duration", m_elimFadeDuration},
        {"elim_linger_duration", m_elimLingerDuration},
        {"text_elements", textElementsJson()},
    };
}

} // namespace is::games::country_elimination
