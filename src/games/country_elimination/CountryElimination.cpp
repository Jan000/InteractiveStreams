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

    // Auto-detect flag height by finding the first fully-transparent separator row
    unsigned int flagStride = 0;
    for (unsigned int y = 1; y < std::min(sheetH, 200u); ++y) {
        bool isTransparent = true;
        for (unsigned int x = 0; x < sheetW; ++x) {
            if (spriteSheet.getPixel(x, y).a != 0) { isTransparent = false; break; }
        }
        if (isTransparent) {
            flagStride = y + 1; // flag content height + 1px separator
            break;
        }
    }
    if (flagStride == 0) {
        // Fallback: divide evenly
        flagStride = sheetH / NUM_SPRITE_FLAGS;
    }
    unsigned int flagH = flagStride - 1;
    unsigned int totalSlots = sheetH / flagStride;

    // Detect if first slot is a black header (skip it)
    unsigned int startSlot = 0;
    bool firstIsBlack = true;
    for (unsigned int x = 0; x < sheetW; ++x) {
        sf::Color c = spriteSheet.getPixel(x, 0);
        if (c.r != 0 || c.g != 0 || c.b != 0) { firstIsBlack = false; break; }
    }
    if (firstIsBlack && totalSlots > static_cast<unsigned int>(NUM_SPRITE_FLAGS))
        startSlot = 1;

    int loaded = 0;
    for (int i = 0; i < NUM_SPRITE_FLAGS; ++i) {
        unsigned int slot = startSlot + static_cast<unsigned int>(i);
        if (slot >= totalSlots) break;
        unsigned int y = slot * flagStride;
        sf::IntRect area(0, static_cast<int>(y), static_cast<int>(sheetW), static_cast<int>(flagH));
        m_flagTextures[SPRITE_ORDER[i]].loadFromImage(spriteSheet, area);
        m_flagTextures[SPRITE_ORDER[i]].setSmooth(true);
        ++loaded;
    }

    spdlog::info("[CountryElimination] Loaded {} flag textures ({}x{}, stride={}px, flagH={}px, startSlot={})",
                 loaded, sheetW, sheetH, flagStride, flagH, startSlot);
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

    m_world = new b2World(b2Vec2(0.0f, m_gravity));

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
        fix.restitution = 0.6f;
        fix.friction = 0.4f;
        fix.filter.categoryBits = CAT_BOUNDARY;
        fix.filter.maskBits     = MASK_BOUNDARY;
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

    generateFlagTextures();

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

    // Quiz init
    m_quizActive = false;
    m_quizCooldown = m_quizInterval;
    m_quizCurrentIdx = -1;
    m_quizRevealTimer = 0.0f;
    m_quizCorrectCount = 0;
    m_quizAnswers.clear();
    m_quizOrder = shuffledQuizIndices(m_rng);
    m_quizOrderPos = 0;

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
    m_pendingBotSpawns.clear();
    m_quizActive = false;
    m_quizAnswers.clear();
    m_avatarCache.clear();
    destroyArenaBody();
    if (m_leftWall && m_world) { m_world->DestroyBody(m_leftWall); m_leftWall = nullptr; }
    if (m_rightWall && m_world) { m_world->DestroyBody(m_rightWall); m_rightWall = nullptr; }
    if (m_floorBody && m_world) { m_world->DestroyBody(m_floorBody); m_floorBody = nullptr; }
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

    L.ppm = L.arenaRadiusPx / m_arenaRadius;

    return L;
}

// ═════════════════════════════════════════════════════════════════════════════
// Arena Physics
// ═════════════════════════════════════════════════════════════════════════════

void CountryElimination::createBoundaryWalls() {
    if (!m_world) return;

    // Walls span from far above the arena down to the floor, so eliminated
    // balls that fly upward can never escape the sides.
    float wallTop     = WORLD_CY - m_arenaRadius - 15.0f;
    float wallCenterY = (wallTop + FLOOR_Y) / 2.0f;
    float wallHalfH   = (FLOOR_Y - wallTop) / 2.0f;

    // Left wall — at the left visible boundary
    {
        b2BodyDef bd;
        bd.type = b2_staticBody;
        bd.position.Set(WALL_LEFT_X, wallCenterY);
        m_leftWall = m_world->CreateBody(&bd);
        b2PolygonShape box;
        box.SetAsBox(0.5f, wallHalfH);
        b2FixtureDef fix;
        fix.shape = &box;
        fix.restitution = 0.6f;
        fix.friction = 0.3f;
        fix.filter.categoryBits = CAT_BOUNDARY;
        fix.filter.maskBits     = MASK_BOUNDARY;
        m_leftWall->CreateFixture(&fix);
    }

    // Right wall — at the right visible boundary
    {
        b2BodyDef bd;
        bd.type = b2_staticBody;
        bd.position.Set(WALL_RIGHT_X, wallCenterY);
        m_rightWall = m_world->CreateBody(&bd);
        b2PolygonShape box;
        box.SetAsBox(0.5f, wallHalfH);
        b2FixtureDef fix;
        fix.shape = &box;
        fix.restitution = 0.6f;
        fix.friction = 0.3f;
        fix.filter.categoryBits = CAT_BOUNDARY;
        fix.filter.maskBits     = MASK_BOUNDARY;
        m_rightWall->CreateFixture(&fix);
    }

    // No ceiling — eliminated balls may fly upward freely
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
    const float segLen = m_arenaRadius * angleStep;

    for (int i = 0; i < WALL_SEGMENTS; ++i) {
        float angle = i * angleStep;

        // Skip gap segments (only if gap > 0)
        if (m_currentGapAngle > 0.001f) {
            float norm = angle;
            if (norm > PI) norm -= TAU;
            if (std::abs(norm) < m_currentGapAngle) continue;
        }

        float mx = m_arenaRadius * std::cos(angle);
        float my = m_arenaRadius * std::sin(angle);

        b2PolygonShape seg;
        seg.SetAsBox(segLen * 0.55f, m_wallThickness * 0.5f,
                     b2Vec2(mx, my), angle + PI / 2.0f);

        b2FixtureDef fix;
        fix.shape = &seg;
        fix.restitution = 1.0f;
        fix.friction = 0.0f;
        fix.filter.categoryBits = CAT_ARENA;
        fix.filter.maskBits     = MASK_ARENA;
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

    // Safety: reposition any alive ball that ended up inside wall segments
    // after the arena was rebuilt (e.g. during gap expansion).
    float innerWall = m_arenaRadius - m_wallThickness * 0.5f;
    float embedLimit = innerWall - m_ballRadius * 0.3f;
    float embedLimit2 = embedLimit * embedLimit;
    for (auto& [id, p] : m_players) {
        if (!p.alive || !p.body) continue;
        b2Vec2 pos = p.body->GetPosition();
        float dx = pos.x - WORLD_CX;
        float dy = pos.y - WORLD_CY;
        float d2 = dx * dx + dy * dy;
        if (d2 > embedLimit2) {
            // Skip balls at the gap opening — they're exiting normally
            bool inGap = false;
            if (m_currentGapAngle > 0.001f) {
                float ballAngle = std::atan2(dy, dx);
                float localAngle = ballAngle - m_arenaAngle;
                localAngle = std::fmod(localAngle + PI, TAU);
                if (localAngle < 0.0f) localAngle += TAU;
                localAngle -= PI;
                inGap = std::abs(localAngle) < m_currentGapAngle + 0.2f;
            }
            if (!inGap) {
                float dist = std::sqrt(d2);
                float safeR = m_arenaRadius - p.radiusM - 0.4f;
                float rx = dx / dist;
                float ry = dy / dist;
                p.body->SetTransform(
                    b2Vec2(WORLD_CX + safeR * rx, WORLD_CY + safeR * ry),
                    p.body->GetAngle());
            }
        }
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

    b2FixtureDef fix;
    fix.density = 1.0f;
    fix.restitution = m_restitution;
    fix.friction = 0.0f;
    fix.filter.categoryBits = CAT_ALIVE;
    fix.filter.maskBits     = MASK_ALIVE;

    b2CircleShape circle;
    b2PolygonShape box;
    if (m_flagShapeRect) {
        box.SetAsBox(radius, radius / FLAG_ASPECT);
        fix.shape = &box;
    } else {
        circle.m_radius = radius;
        fix.shape = &circle;
    }
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
    std::string cmdLower = cmd;
    std::transform(cmdLower.begin(), cmdLower.end(), cmdLower.begin(), ::tolower);

    // Helper: strip trailing non-alphanumeric characters (handles "de!!!!", "usa!!")
    auto stripTrailing = [](const std::string& s) -> std::string {
        std::string r = s;
        while (!r.empty()) {
            unsigned char c = static_cast<unsigned char>(r.back());
            if (std::isalnum(c) || c >= 0x80) break; // keep unicode and alnum
            r.pop_back();
        }
        return r;
    };

    bool handled = false;

    if (cmdLower == "join" || cmdLower == "play" || cmdLower == "me") {
        std::string label;
        std::getline(iss >> std::ws, label);
        while (!label.empty() && (label.back() == '\r' || label.back() == '\n' || label.back() == ' '))
            label.pop_back();
        label = stripTrailing(label);

        // "me" is always a flagless join (no country code resolution)
        if (cmdLower == "me") {
            if (m_allowFlaglessJoin) {
                cmdJoin(msg.userId, msg.displayName, "", msg.avatarUrl, true);
            } else {
                cmdJoin(msg.userId, msg.displayName, randomCountryCode(m_rng), msg.avatarUrl);
            }
        } else {
            // Resolve country name/code to 2-letter ISO code
            std::string code = resolveCountryCode(label);
            if (code.empty() && !label.empty()) {
                // Try the full raw text after cmd (flag emoji may have trailing chars)
                code = resolveCountryCode(stripTrailing(msg.text.substr(msg.text.find(cmd) + cmd.size())));
            }
            if (code.empty()) {
                if (m_allowFlaglessJoin) {
                    cmdJoin(msg.userId, msg.displayName, "", msg.avatarUrl, true);
                } else {
                    cmdJoin(msg.userId, msg.displayName, randomCountryCode(m_rng), msg.avatarUrl);
                }
            } else {
                cmdJoin(msg.userId, msg.displayName, code, msg.avatarUrl);
            }
        }
        handled = true;
    }

    // Auto-detect country from any message (e.g. "usa", "🇩🇪", "de")
    if (!handled && m_autoDetectCountry) {
        // Try the whole message stripped of trailing junk
        std::string stripped = stripTrailing(msg.text);
        while (!stripped.empty() && (stripped.front() == '!' || stripped.front() == ' '))
            stripped.erase(stripped.begin());
        stripped = stripTrailing(stripped);
        std::string code = resolveCountryCode(stripped);
        if (!code.empty()) {
            cmdJoin(msg.userId, msg.displayName, code, msg.avatarUrl);
            handled = true;
        }
    }

    // Quiz answers: 1-4 or a-d
    if (m_quizActive && cmdLower.size() == 1) {
        int ans = -1;
        if (cmdLower[0] >= '1' && cmdLower[0] <= '4') ans = cmdLower[0] - '1';
        else if (cmdLower[0] >= 'a' && cmdLower[0] <= 'd') ans = cmdLower[0] - 'a';
        if (ans >= 0)
            handleQuizAnswer(msg.userId, msg.displayName, ans);
    }
}

void CountryElimination::cmdJoin(const std::string& userId, const std::string& displayName,
                                  const std::string& label, const std::string& avatarUrl,
                                  bool flagless) {
    // Joins are allowed in ALL phases — viewers can join at any time.

    // Count alive entries for this user
    int aliveCount = 0;
    int totalCount = 0;
    for (const auto& [key, pl] : m_players) {
        if (pl.userId == userId) {
            totalCount++;
            if (pl.alive) aliveCount++;
        }
    }
    if (aliveCount >= m_maxEntriesPerPlayer) return;

    // Find next available map key
    std::string mapKey = userId;
    if (m_players.count(mapKey)) {
        for (int n = 2; ; ++n) {
            mapKey = userId + "#" + std::to_string(n);
            if (!m_players.count(mapKey)) break;
        }
    }

    std::uniform_real_distribution<float> aDist(0.0f, TAU);
    std::uniform_real_distribution<float> rDist(0.0f, m_arenaRadius * 0.55f);
    float a = aDist(m_rng);
    float r = rDist(m_rng);
    float sx = WORLD_CX + r * std::cos(a);
    float sy = WORLD_CY + r * std::sin(a);

    Player p;
    p.userId = userId;
    p.displayName = displayName;
    p.label = label;
    p.avatarUrl = avatarUrl;
    p.flagless = flagless;
    p.color = generateColor();
    p.radiusM = m_ballRadius;
    p.alive = true;
    p.eliminated = false;
    p.body = createPlayerBody(sx, sy, m_ballRadius);
    p.prevPos = p.body->GetPosition();
    p.currPos = p.prevPos;
    m_players[mapKey] = std::move(p);

    if (!avatarUrl.empty())
        m_avatarCache.request(avatarUrl);

    if (totalCount == 0 && !isBotId(userId)) {
        if (flagless) {
            sendChatFeedback(displayName + " joined!");
        } else {
            const auto& names = getCountryDisplayNames();
            auto nameIt = names.find(label);
            std::string countryName = (nameIt != names.end()) ? nameIt->second : label;
            sendChatFeedback(displayName + " [" + countryName + "] joined!");
        }
    }
}

void CountryElimination::handleStreamEvent(const platform::ChatMessage& msg) {
    if (msg.eventType.empty()) return;

    // Find first alive entry for this user (or any entry)
    Player* target = nullptr;
    bool hasAny = false;
    for (auto& [key, pl] : m_players) {
        if (pl.userId == msg.userId) {
            hasAny = true;
            if (pl.alive && !target) target = &pl;
        }
    }
    if (!hasAny) {
        cmdJoin(msg.userId, msg.displayName, randomCountryCode(m_rng), msg.avatarUrl);
        // Find the newly created entry
        for (auto& [key, pl] : m_players) {
            if (pl.userId == msg.userId && pl.alive) { target = &pl; break; }
        }
        if (!target) return;
    }
    if (!target) return;

    Player& p = *target;

    if (msg.eventType == "yt_subscribe" || msg.eventType == "twitch_sub") {
        p.hasShield = true;
        p.shieldTimer = m_shieldDurationSub;
        p.score += m_scoreSub;
        sendChatFeedback("🛡️ " + p.displayName + " got a shield! +" + std::to_string(m_scoreSub));
    } else if ((msg.eventType == "yt_superchat" || msg.eventType == "twitch_bits") && msg.amount > 100) {
        p.hasShield = true;
        p.shieldTimer = m_shieldDurationSuperchat;
        p.score += m_scoreSuperchat;
        if (p.body) {
            float nr = m_ballRadius * 1.5f;
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
        sendChatFeedback("⭐ " + p.displayName + " powered up! +" + std::to_string(m_scoreSuperchat));
    } else if (msg.eventType == "twitch_channel_points") {
        p.hasShield = true;
        p.shieldTimer = m_shieldDurationPoints;
        p.score += m_scorePoints;
        sendChatFeedback("✨ " + p.displayName + " got a shield! +" + std::to_string(m_scorePoints));
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
    m_countdownTimer = m_countdownDuration;
}

void CountryElimination::startBattle() {
    m_phase = GamePhase::Battle;
    m_roundTimer = 0.0;
    m_roundNumber++;

    // Open the gap and rebuild arena
    m_currentGapAngle = m_gapInitial;
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

        // Anti-wall-riding: if the ball is near the arena wall and its
        // velocity is not pointing inward, reflect the radial component so
        // it bounces cleanly instead of riding along with the rotating ring.
        // Skip if ball is at the gap opening — let it escape normally.
        b2Vec2 pos = p.body->GetPosition();
        float dx = pos.x - WORLD_CX;
        float dy = pos.y - WORLD_CY;
        float dist2 = dx * dx + dy * dy;
        float wallProx = m_arenaRadius - p.radiusM - 0.3f;
        if (dist2 > wallProx * wallProx) {
            // Check if the ball is at the gap opening (in arena-local frame)
            bool inGap = false;
            if (m_currentGapAngle > 0.001f) {
                float ballAngle = std::atan2(dy, dx);
                float localAngle = ballAngle - m_arenaAngle;
                // Normalize to [-PI, PI]
                localAngle = std::fmod(localAngle + PI, TAU);
                if (localAngle < 0.0f) localAngle += TAU;
                localAngle -= PI;
                inGap = std::abs(localAngle) < m_currentGapAngle + 0.15f;
            }

            if (!inGap) {
                float dist = std::sqrt(dist2);
                float rx = dx / dist;
                float ry = dy / dist;
                vel = p.body->GetLinearVelocity();
                float radialVel = vel.x * rx + vel.y * ry;
                if (radialVel >= 0.0f) {
                    // Remove outward component, reflect inward
                    vel.x -= 2.0f * radialVel * rx;
                    vel.y -= 2.0f * radialVel * ry;
                    spd = vel.Length();
                    if (spd > 0.01f) {
                        float scale = m_currentBallSpeed / spd;
                        p.body->SetLinearVelocity(b2Vec2(vel.x * scale, vel.y * scale));
                    }
                }

                // Force-reposition if ball is embedded in the arena wall.
                // Inner wall surface is at (arenaRadius - wallThickness/2).
                // If ball center is past that minus a fraction of its radius,
                // it has penetrated the wall and must be pushed back.
                float innerWall = m_arenaRadius - m_wallThickness * 0.5f;
                if (dist > innerWall - p.radiusM * 0.3f) {
                    float safeR = m_arenaRadius - p.radiusM - 0.4f;
                    p.body->SetTransform(
                        b2Vec2(WORLD_CX + safeR * rx, WORLD_CY + safeR * ry),
                        p.body->GetAngle());
                }
            }
        }
    }
}

void CountryElimination::checkEliminations() {
    float limit = m_arenaRadius + m_wallThickness + 0.5f;
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
                float pullback = m_arenaRadius * 0.75f / std::sqrt(d2);
                p.body->SetTransform(
                    b2Vec2(WORLD_CX + dx * pullback, WORLD_CY + dy * pullback),
                    p.body->GetAngle());
                if (!p.isBot())
                    sendChatFeedback("🛡️ " + p.displayName + "'s shield saved them!");
                continue;
            }

            p.alive = false;
            p.body->SetGravityScale(1.0f);
            p.body->SetLinearDamping(0.2f);
            b2Vec2 vel = p.body->GetLinearVelocity();
            p.body->SetLinearVelocity(b2Vec2(vel.x * 0.5f, 2.0f));

            // Update fixture for eliminated ball physics: bouncy, some friction
            b2Fixture* fix = p.body->GetFixtureList();
            if (fix) {
                fix->SetRestitution(0.7f);
                fix->SetFriction(0.2f);

                // Switch collision category to eliminated
                b2Filter filter = fix->GetFilterData();
                filter.categoryBits = CAT_ELIMINATED;
                filter.maskBits     = m_allowReentry ? MASK_ELIM_REENTRY : MASK_ELIM_NOREENTRY;
                fix->SetFilterData(filter);
            }

            // Track in eliminated FIFO queue
            m_eliminatedQueue.push_back({id, 0.0f, false, 0.0f});

            m_eliminationFeed.push_front({p.displayName, p.label, p.avatarUrl, p.color, 4.0});
            if (static_cast<int>(m_eliminationFeed.size()) > m_elimFeedMax) m_eliminationFeed.pop_back();

            if (!p.isBot()) {
                sendChatFeedback("💀 " + p.displayName + " [" + p.label + "] eliminated!");
                try {
                    is::core::Application::instance().playerDatabase().recordResult(
                        p.userId, p.displayName, "country_elimination", m_scoreParticipation, false);
                } catch (...) {}
            }
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
        w.score += m_scoreWin;
        recordRoundWin(w);
        recordCountryWin(w.label);

        if (!w.isBot()) {
            sendChatFeedback("🏆 " + w.displayName + " [" + w.label + "] wins Round " +
                              std::to_string(m_roundNumber) + "!");
            try {
                is::core::Application::instance().playerDatabase().recordResult(
                    w.userId, w.displayName, "country_elimination", m_scoreWin, true);
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
            rw.avatarUrl = winner.avatarUrl;
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
    m_roundWinners.push_back({winner.userId, winner.displayName, winner.label, winner.avatarUrl, winner.color, 1});
    std::sort(m_roundWinners.begin(), m_roundWinners.end(),
              [](const RoundWinEntry& a, const RoundWinEntry& b) { return a.wins > b.wins; });
}

void CountryElimination::recordCountryWin(const std::string& label) {
    std::string upper = label;
    for (auto& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    for (auto& cw : m_countryWins) {
        if (cw.label == upper) {
            cw.wins++;
            std::sort(m_countryWins.begin(), m_countryWins.end(),
                      [](const CountryWinEntry& a, const CountryWinEntry& b) { return a.wins > b.wins; });
            return;
        }
    }
    m_countryWins.push_back({upper, 1});
    std::sort(m_countryWins.begin(), m_countryWins.end(),
              [](const CountryWinEntry& a, const CountryWinEntry& b) { return a.wins > b.wins; });
}

void CountryElimination::refreshPlayerLeaderboardCache() {
    try {
        auto entries = is::core::Application::instance().playerDatabase().getTopRecent(10, 24);
        m_cachedPlayerLeaderboard.clear();
        for (const auto& e : entries) {
            // Try to find avatar URL from active players
            std::string avatarUrl;
            auto it = m_players.find(e.userId);
            if (it != m_players.end()) avatarUrl = it->second.avatarUrl;
            m_cachedPlayerLeaderboard.push_back({e.displayName, avatarUrl, e.points, e.wins});
        }
    } catch (...) {}
}

void CountryElimination::resetForNextRound() {
    // ── Survivors persist: keep alive players, remove eliminated ones ──
    for (auto it = m_players.begin(); it != m_players.end(); ) {
        if (it->second.alive) {
            // Survivor — reset power-ups but keep in the game
            Player& p = it->second;
            p.hasShield = false;
            p.shieldTimer = 0.0f;
            // Reset enlarged balls back to default
            if (std::abs(p.radiusM - m_ballRadius) > 0.01f && p.body) {
                b2Fixture* fix = p.body->GetFixtureList();
                if (fix) {
                    p.body->DestroyFixture(fix);
                    b2CircleShape c;
                    c.m_radius = m_ballRadius;
                    b2FixtureDef fd;
                    fd.shape = &c;
                    fd.density = 1.0f;
                    fd.restitution = m_restitution;
                    fd.friction = 0.0f;
                    fd.filter.categoryBits = CAT_ALIVE;
                    fd.filter.maskBits     = MASK_ALIVE;
                    p.body->CreateFixture(&fd);
                }
                p.radiusM = m_ballRadius;
            }
            ++it;
        } else {
            // Eliminated — clean up unless visual persistence is on
            if (!m_elimPersistRounds) {
                if (it->second.body && m_world) m_world->DestroyBody(it->second.body);
                it = m_players.erase(it);
            } else {
                ++it;
            }
        }
    }
    if (!m_elimPersistRounds) m_eliminatedQueue.clear();
    m_eliminationFeed.clear();
    m_winnerId.clear();
    m_particles.clear();
    m_botRespawnTimers.clear();
    m_pendingBotSpawns.clear();

    // Reset to closed ring for lobby
    m_currentGapAngle = 0.0f;
    m_currentBallSpeed = m_initialSpeed;
    m_arenaAngularVel = m_arenaSpeedDefault;
    createArenaBody();
    if (m_arenaBody) m_arenaBody->SetAngularVelocity(m_arenaAngularVel);

    // Reposition survivors safely inside the closed arena so no one gets
    // trapped inside the rebuilt wall segments.
    float safeRadius = m_arenaRadius - m_wallThickness - m_ballRadius - 0.3f;
    float safeRadius2 = safeRadius * safeRadius;
    for (auto& [id, p] : m_players) {
        if (!p.alive || !p.body) continue;
        b2Vec2 pos = p.body->GetPosition();
        float dx = pos.x - WORLD_CX;
        float dy = pos.y - WORLD_CY;
        float d2 = dx * dx + dy * dy;
        if (d2 > safeRadius2) {
            // Pull back inside
            float dist = std::sqrt(d2);
            float factor = (safeRadius * 0.85f) / dist;
            p.body->SetTransform(
                b2Vec2(WORLD_CX + dx * factor, WORLD_CY + dy * factor),
                p.body->GetAngle());
            p.prevPos = p.body->GetPosition();
            p.currPos = p.prevPos;
        }
    }

    // Normalize survivor velocities to initial speed
    for (auto& [id, p] : m_players) {
        if (!p.alive || !p.body) continue;
        b2Vec2 vel = p.body->GetLinearVelocity();
        float spd = vel.Length();
        if (spd > 0.01f) {
            float scale = m_initialSpeed / spd;
            p.body->SetLinearVelocity(b2Vec2(vel.x * scale, vel.y * scale));
        }
    }

    m_phase = GamePhase::Lobby;
    m_lobbyTimer = 0.0;

    spawnBots();
}

// ═════════════════════════════════════════════════════════════════════════════
// Bots
// ═════════════════════════════════════════════════════════════════════════════

void CountryElimination::spawnBots() {
    if (m_botFillTarget <= 0) return;

    int botCount = 0;
    for (const auto& [id, p] : m_players) { if (p.isBot() && p.alive) botCount++; }

    int needed = m_botFillTarget - botCount;
    for (int i = 0; i < needed; ++i) {
        m_botCounter++;
        std::string botId = "__bot_" + std::to_string(m_botCounter);
        int idx = (m_botCounter - 1) % NUM_BOT_NAMES;

        cmdJoin(botId, BOT_NAMES[idx], BOT_LABELS[idx]);
    }
}

void CountryElimination::scheduleBotSpawns() {
    if (m_botFillTarget <= 0) return;

    int botCount = 0;
    for (const auto& [id, p] : m_players) { if (p.isBot() && p.alive) botCount++; }
    int pending = static_cast<int>(m_pendingBotSpawns.size());

    int needed = m_botFillTarget - botCount - pending;
    for (int i = 0; i < needed; ++i) {
        int idx = (m_botCounter + i) % NUM_BOT_NAMES;
        float delay = m_botRespawnDelay * static_cast<float>(i + 1);
        m_pendingBotSpawns.push_back({delay, BOT_NAMES[idx], BOT_LABELS[idx]});
    }
}

void CountryElimination::tickBotSpawnTimers(float dt) {
    if (m_pendingBotSpawns.empty()) return;

    for (auto& pbs : m_pendingBotSpawns) {
        pbs.timer -= dt;
    }

    // Spawn bots whose timer has expired (one per tick to stagger visually)
    while (!m_pendingBotSpawns.empty() && m_pendingBotSpawns.front().timer <= 0.0f) {
        auto& pbs = m_pendingBotSpawns.front();
        m_botCounter++;
        std::string botId = "__bot_" + std::to_string(m_botCounter);
        cmdJoin(botId, pbs.name, pbs.label);
        m_pendingBotSpawns.erase(m_pendingBotSpawns.begin());
    }
}

void CountryElimination::respawnDeadBots(float dt) {
    if (m_botFillTarget <= 0 || !m_botRespawn) return;

    int aliveCount = 0;
    for (const auto& [_, p] : m_players) { if (p.alive) aliveCount++; }
    if (aliveCount >= m_botFillTarget) return;  // already full

    for (auto& [id, p] : m_players) {
        if (p.alive || !p.isBot()) continue;
        // Track this dead bot (only if not already tracked)
        if (m_botRespawnTimers.find(id) == m_botRespawnTimers.end())
            m_botRespawnTimers[id] = m_botRespawnDelay;
    }

    // Tick timers and respawn
    std::vector<std::string> toRespawn;
    for (auto& [id, timer] : m_botRespawnTimers) {
        if (timer < 0.0f) continue;  // already consumed
        timer -= dt;
        if (timer <= 0.0f && aliveCount < m_botFillTarget) {
            toRespawn.push_back(id);
            timer = -1.0f;  // sentinel: consumed
            aliveCount++;   // account for upcoming spawn
        }
    }

    // Clean up consumed timers whose players have been removed from m_players
    for (auto it = m_botRespawnTimers.begin(); it != m_botRespawnTimers.end(); ) {
        if (it->second < 0.0f && m_players.find(it->first) == m_players.end())
            it = m_botRespawnTimers.erase(it);
        else
            ++it;
    }

    for (const auto& botId : toRespawn) {
        m_botCounter++;
        std::string newId = "__bot_" + std::to_string(m_botCounter);
        int idx = (m_botCounter - 1) % NUM_BOT_NAMES;
        cmdJoin(newId, BOT_NAMES[idx], BOT_LABELS[idx]);
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
    updateQuiz(fdt);
    m_avatarCache.processPendingUploads(2);

    // Periodically refresh player leaderboard cache from PlayerDatabase
    m_leaderboardCacheTimer -= dt;
    if (m_leaderboardCacheTimer <= 0.0) {
        m_leaderboardCacheTimer = 5.0;
        refreshPlayerLeaderboardCache();
    }

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
    // Mark oldest balls for fading when over limit (0 = no limit).
    // Count-based fading always applies, even with infinite linger.
    {
        int visibleCount = static_cast<int>(m_eliminatedQueue.size());
        int overLimit = (m_maxEliminatedVisible > 0) ? visibleCount - m_maxEliminatedVisible : 0;
        if (overLimit > 0) {
            for (int i = 0; i < overLimit && i < visibleCount; ++i) {
                if (!m_eliminatedQueue[i].fading) {
                    m_eliminatedQueue[i].fading = true;
                    m_eliminatedQueue[i].fadeProgress = 0.0f;
                }
            }
        }
    }
    // Start fading balls that have lingered too long (unless infinite linger is on)
    if (!m_elimInfiniteLinger) {
        for (auto& eb : m_eliminatedQueue) {
            if (!eb.fading && eb.age >= m_elimLingerDuration) {
                eb.fading = true;
                eb.fadeProgress = 0.0f;
            }
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
            m_players.erase(eb.playerId);
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

        // Staggered bot spawning
        tickBotSpawnTimers(fdt);

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

        // Re-entry: revive eliminated players that bounce back inside the arena
        if (m_allowReentry) {
            float reentryLimit = m_arenaRadius - m_wallThickness - 0.3f;
            float reentryLimit2 = reentryLimit * reentryLimit;
            for (auto& [id, p] : m_players) {
                if (p.alive || !p.body) continue;
                b2Vec2 pos = p.body->GetPosition();
                float dx = pos.x - WORLD_CX;
                float dy = pos.y - WORLD_CY;
                if (dx * dx + dy * dy < reentryLimit2) {
                    p.alive = true;
                    p.body->SetGravityScale(0.0f);
                    p.body->SetLinearDamping(0.0f);
                    // Restore ball physics
                    b2Fixture* fix = p.body->GetFixtureList();
                    if (fix) {
                        fix->SetRestitution(m_restitution);
                        fix->SetFriction(0.0f);
                        // Restore alive collision filter
                        b2Filter filter = fix->GetFilterData();
                        filter.categoryBits = CAT_ALIVE;
                        filter.maskBits     = MASK_ALIVE;
                        fix->SetFilterData(filter);
                    }
                    // Remove from eliminated queue
                    m_eliminatedQueue.erase(
                        std::remove_if(m_eliminatedQueue.begin(), m_eliminatedQueue.end(),
                            [&id](const EliminatedBall& eb) { return eb.playerId == id; }),
                        m_eliminatedQueue.end());
                }
            }
        }

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
    renderVisualizer(target, L);
    renderPlayers(target, L, alpha);
    renderParticles(target);
    renderEliminationFeed(target, L);
    renderUI(target, L);
    renderQuizOverlay(target, L);
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
    float thickness = m_wallThickness * L.ppm * 1.2f;
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

            // Rainbow or static color
            sf::Color c;
            if (m_rainbowRing) {
                float hue = std::fmod(baseA / TAU + m_globalTime * 0.15f, 1.0f);
                // HSV→RGB (S=0.8, V=1.0)
                float h6 = hue * 6.0f;
                int hi = static_cast<int>(h6) % 6;
                float f = h6 - static_cast<float>(hi);
                float q = 1.0f - 0.8f * f;
                float t = 1.0f - 0.8f * (1.0f - f);
                float rr, gg, bb;
                switch (hi) {
                    case 0: rr=1; gg=t; bb=0.2f; break;
                    case 1: rr=q; gg=1; bb=0.2f; break;
                    case 2: rr=0.2f; gg=1; bb=t; break;
                    case 3: rr=0.2f; gg=q; bb=1; break;
                    case 4: rr=t; gg=0.2f; bb=1; break;
                    default: rr=1; gg=0.2f; bb=q; break;
                }
                c = sf::Color(
                    static_cast<sf::Uint8>(rr * 255),
                    static_cast<sf::Uint8>(gg * 255),
                    static_cast<sf::Uint8>(bb * 255),
                    static_cast<sf::Uint8>(230 * edgeFade));
            } else {
                sf::Uint8 alpha = static_cast<sf::Uint8>(ringColor.a * edgeFade);
                c = sf::Color(ringColor.r, ringColor.g, ringColor.b, alpha);
            }

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

// ── Audio Visualizer ─────────────────────────────────────────────────────────

void CountryElimination::renderVisualizer(sf::RenderTarget& target, const ScreenLayout& L) {
    if (!m_visualizerEnabled) return;

    const int numBands = std::clamp(m_visualizerBands, 8, 128);
    std::vector<float> bands(numBands, 0.0f);
    if (!getSpectrum(bands.data(), numBands)) return;

    // Ensure smoothed buffer matches
    if (static_cast<int>(m_vizSmoothed.size()) != numBands)
        m_vizSmoothed.assign(numBands, 0.0f);

    // Apply gain and exponential smoothing
    float gain = std::max(0.1f, m_visualizerGain);
    float smooth = std::clamp(m_visualizerSmoothing, 0.0f, 0.95f);
    for (int i = 0; i < numBands; ++i) {
        float val = std::min(1.0f, bands[i] * gain);
        m_vizSmoothed[i] = smooth * m_vizSmoothed[i] + (1.0f - smooth) * val;
    }

    float r = L.arenaRadiusPx;
    float thickness = m_wallThickness * L.ppm * 1.2f;
    float rOuter = r + thickness / 2.0f + 2.0f;  // just outside the ring
    float maxH = m_visualizerHeight * (L.ppm / REF_PPM); // scale with resolution
    float opacity = std::clamp(m_visualizerOpacity, 0.0f, 1.0f);

    // Mirror spectrum: left half = bands 0..N-1, right half = N-1..0
    // This ensures both halves of the circle look equally active.
    const int totalSlots = numBands * 2;
    std::vector<float> vizMirrored(totalSlots);
    for (int i = 0; i < numBands; ++i) {
        vizMirrored[i] = m_vizSmoothed[i];
        vizMirrored[totalSlots - 1 - i] = m_vizSmoothed[i];
    }

    float slotAngle = TAU / totalSlots;

    // Helper: HSV hue to RGB
    auto hueToRGB = [](float hue, float& rr, float& gg, float& bb) {
        float h6 = hue * 6.0f;
        int hi = static_cast<int>(h6) % 6;
        float f = h6 - static_cast<float>(hi);
        switch (hi) {
            case 0: rr=1; gg=f; bb=0; break;
            case 1: rr=1-f; gg=1; bb=0; break;
            case 2: rr=0; gg=1; bb=f; break;
            case 3: rr=0; gg=1-f; bb=1; break;
            case 4: rr=f; gg=0; bb=1; break;
            default: rr=1; gg=0; bb=1-f; break;
        }
    };

    if (m_visualizerStyle == 0) {
        // ── Style 0: Bars ────────────────────────────────────────────────
        for (int i = 0; i < totalSlots; ++i) {
            float centerAngle = i * slotAngle - PI * 0.5f;

            float val = std::clamp(vizMirrored[i], 0.0f, 1.0f);
            if (val < 0.01f) continue;

            float barH = val * maxH;
            float barW = slotAngle * rOuter * 0.6f;

            float rr, gg, bb;
            float hue = std::fmod(static_cast<float>(i) / totalSlots + m_globalTime * 0.1f, 1.0f);
            hueToRGB(hue, rr, gg, bb);

            sf::Uint8 alpha = static_cast<sf::Uint8>(255 * opacity * (0.5f + 0.5f * val));
            sf::Color col(static_cast<sf::Uint8>(rr * 255),
                          static_cast<sf::Uint8>(gg * 255),
                          static_cast<sf::Uint8>(bb * 255), alpha);

            float halfW = barW * 0.5f;
            float cosA = std::cos(centerAngle);
            float sinA = std::sin(centerAngle);
            float perpCos = std::cos(centerAngle + PI * 0.5f);
            float perpSin = std::sin(centerAngle + PI * 0.5f);

            float r1 = rOuter;
            float r2 = rOuter + barH;

            sf::ConvexShape bar(4);
            bar.setPoint(0, { L.arenaCX + r1 * cosA + halfW * perpCos,
                              L.arenaCY + r1 * sinA + halfW * perpSin });
            bar.setPoint(1, { L.arenaCX + r1 * cosA - halfW * perpCos,
                              L.arenaCY + r1 * sinA - halfW * perpSin });
            bar.setPoint(2, { L.arenaCX + r2 * cosA - halfW * 0.6f * perpCos,
                              L.arenaCY + r2 * sinA - halfW * 0.6f * perpSin });
            bar.setPoint(3, { L.arenaCX + r2 * cosA + halfW * 0.6f * perpCos,
                              L.arenaCY + r2 * sinA + halfW * 0.6f * perpSin });
            bar.setFillColor(col);
            target.draw(bar);

            float dotR = barW * 0.3f;
            sf::CircleShape tip(dotR, 8);
            tip.setOrigin(dotR, dotR);
            tip.setPosition(L.arenaCX + r2 * cosA, L.arenaCY + r2 * sinA);
            tip.setFillColor(sf::Color(col.r, col.g, col.b, static_cast<sf::Uint8>(alpha * 0.8f)));
            target.draw(tip);
        }
    } else if (m_visualizerStyle == 1) {
        // ── Style 1: Dots ────────────────────────────────────────────────
        for (int i = 0; i < totalSlots; ++i) {
            float centerAngle = i * slotAngle - PI * 0.5f;

            float val = std::clamp(vizMirrored[i], 0.0f, 1.0f);
            if (val < 0.02f) continue;

            float rr, gg, bb;
            float hue = std::fmod(static_cast<float>(i) / totalSlots + m_globalTime * 0.1f, 1.0f);
            hueToRGB(hue, rr, gg, bb);

            for (int layer = 0; layer < 3; ++layer) {
                float layerVal = val * (1.0f - layer * 0.25f);
                if (layerVal < 0.02f) continue;
                float dist = rOuter + (layer + 1) * maxH * 0.3f * layerVal;
                float dotSize = std::max(1.5f, slotAngle * rOuter * 0.3f * layerVal);
                sf::Uint8 alpha = static_cast<sf::Uint8>(255 * opacity * layerVal * (1.0f - layer * 0.3f));
                sf::CircleShape dot(dotSize, 10);
                dot.setOrigin(dotSize, dotSize);
                dot.setPosition(L.arenaCX + dist * std::cos(centerAngle),
                                L.arenaCY + dist * std::sin(centerAngle));
                dot.setFillColor(sf::Color(static_cast<sf::Uint8>(rr * 255),
                                           static_cast<sf::Uint8>(gg * 255),
                                           static_cast<sf::Uint8>(bb * 255), alpha));
                target.draw(dot);
            }
        }
    } else if (m_visualizerStyle == 2) {
        // ── Style 2: Pulse ───────────────────────────────────────────────
        float avgVal = 0.0f;
        for (int i = 0; i < numBands; ++i)
            avgVal += m_vizSmoothed[i];
        avgVal /= numBands;
        avgVal = std::clamp(avgVal, 0.0f, 1.0f);

        if (avgVal > 0.01f) {
            float pulseThickness = std::max(2.0f, 4.0f * avgVal);

            sf::VertexArray pulseRing(sf::TriangleStrip, (RING_RESOLUTION + 1) * 2);
            int vi = 0;
            float angleStep = TAU / RING_RESOLUTION;
            for (int i = 0; i <= RING_RESOLUTION; ++i) {
                float a = i * angleStep - PI * 0.5f;

                int slotIdx = static_cast<int>(static_cast<float>(i) / RING_RESOLUTION * totalSlots) % totalSlots;
                float localVal = vizMirrored[slotIdx];
                float localR = rOuter + localVal * maxH;
                float lpRO = localR + pulseThickness * 0.5f;
                float lpRI = localR - pulseThickness * 0.5f;

                float rr, gg, bb;
                float hue = std::fmod(static_cast<float>(i) / RING_RESOLUTION + m_globalTime * 0.1f, 1.0f);
                hueToRGB(hue, rr, gg, bb);

                sf::Uint8 alpha = static_cast<sf::Uint8>(200 * opacity * localVal);
                sf::Color col(static_cast<sf::Uint8>(rr * 255),
                              static_cast<sf::Uint8>(gg * 255),
                              static_cast<sf::Uint8>(bb * 255), alpha);

                pulseRing[vi].position = { L.arenaCX + lpRO * std::cos(a), L.arenaCY + lpRO * std::sin(a) };
                pulseRing[vi].color = sf::Color(col.r, col.g, col.b, static_cast<sf::Uint8>(alpha * 0.3f));
                vi++;
                pulseRing[vi].position = { L.arenaCX + lpRI * std::cos(a), L.arenaCY + lpRI * std::sin(a) };
                pulseRing[vi].color = col;
                vi++;
            }
            target.draw(pulseRing);
        }
    } else if (m_visualizerStyle == 3) {
        // ── Style 3: Wave ────────────────────────────────────────────────
        sf::VertexArray wave(sf::TriangleStrip, (RING_RESOLUTION + 1) * 2);
        int vi = 0;
        float angleStep = TAU / RING_RESOLUTION;
        for (int i = 0; i <= RING_RESOLUTION; ++i) {
            float a = i * angleStep - PI * 0.5f;

            // Interpolate mirrored bands for smooth waveform
            float slotPos = static_cast<float>(i) / RING_RESOLUTION * totalSlots;
            int s0 = static_cast<int>(slotPos) % totalSlots;
            int s1 = (s0 + 1) % totalSlots;
            float frac = slotPos - static_cast<float>(static_cast<int>(slotPos));
            float val = vizMirrored[s0] * (1.0f - frac) + vizMirrored[s1] * frac;
            val = std::clamp(val, 0.0f, 1.0f);

            float waveR = rOuter + val * maxH;

            float rr, gg, bb;
            float hue = std::fmod(static_cast<float>(i) / RING_RESOLUTION + m_globalTime * 0.1f, 1.0f);
            hueToRGB(hue, rr, gg, bb);

            sf::Uint8 alpha = static_cast<sf::Uint8>(220 * opacity * (0.3f + 0.7f * val));
            sf::Color col(static_cast<sf::Uint8>(rr * 255),
                          static_cast<sf::Uint8>(gg * 255),
                          static_cast<sf::Uint8>(bb * 255), alpha);

            wave[vi].position = { L.arenaCX + waveR * std::cos(a), L.arenaCY + waveR * std::sin(a) };
            wave[vi].color = col;
            vi++;
            wave[vi].position = { L.arenaCX + rOuter * std::cos(a), L.arenaCY + rOuter * std::sin(a) };
            wave[vi].color = sf::Color(col.r, col.g, col.b, static_cast<sf::Uint8>(alpha * 0.2f));
            vi++;
        }
        target.draw(wave);
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

        // Flag texture lookup
        std::string labelUp = p.label;
        for (auto& c : labelUp) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        auto flagIt = m_flagTextures.find(labelUp);
        bool hasFlag = (flagIt != m_flagTextures.end()) && !p.flagless;

        // Avatar texture (needed early for flagless ball rendering)
        const sf::Texture* avatarTex = (!p.avatarUrl.empty())
            ? m_avatarCache.getTexture(p.avatarUrl) : nullptr;
        bool avatarOnBall = p.flagless && avatarTex;

        float halfW = rpx;
        float halfH = m_flagShapeRect ? (rpx / FLAG_ASPECT) : rpx;

        // Shadow
        {
            if (m_flagShapeRect) {
                sf::RectangleShape shadow(sf::Vector2f(halfW * 2 + 4.0f, halfH * 2 + 4.0f));
                shadow.setOrigin(halfW + 2.0f, halfH + 2.0f);
                shadow.setPosition(sp.x + 2.0f, sp.y + 2.0f);
                shadow.setFillColor(sf::Color(0, 0, 0, baseAlpha / 3));
                target.draw(shadow);
            } else {
                sf::CircleShape shadow(rpx + 2.0f, 32);
                shadow.setOrigin(rpx + 2.0f, rpx + 2.0f);
                shadow.setPosition(sp.x + 2.0f, sp.y + 2.0f);
                shadow.setFillColor(sf::Color(0, 0, 0, baseAlpha / 3));
                target.draw(shadow);
            }
        }

        // Ball body
        {
            float outlineThk = m_flagOutline ? m_flagOutlineThickness : 0.0f;
            sf::Color outlineCol(255, 255, 255, static_cast<sf::Uint8>(baseAlpha * 0.7f));

            if (m_flagShapeRect) {
                sf::RectangleShape ball(sf::Vector2f(halfW * 2, halfH * 2));
                ball.setOrigin(halfW, halfH);
                ball.setPosition(sp);

                if (hasFlag) {
                    ball.setFillColor(sf::Color(255, 255, 255, baseAlpha));
                    ball.setTexture(&flagIt->second);
                } else if (avatarOnBall) {
                    ball.setFillColor(sf::Color(255, 255, 255, baseAlpha));
                    ball.setTexture(avatarTex);
                    // Center-square crop to avoid distortion on rectangular textures
                    auto ts = avatarTex->getSize();
                    int side = static_cast<int>(std::min(ts.x, ts.y));
                    int ox = (static_cast<int>(ts.x) - side) / 2;
                    int oy = (static_cast<int>(ts.y) - side) / 2;
                    ball.setTextureRect(sf::IntRect(ox, oy, side, side));
                } else {
                    sf::Color fill = p.color;
                    fill.a = baseAlpha;
                    ball.setFillColor(fill);
                }
                ball.setOutlineColor(outlineCol);
                ball.setOutlineThickness(outlineThk);
                target.draw(ball);
            } else {
                sf::CircleShape ball(rpx, 32);
                ball.setOrigin(rpx, rpx);
                ball.setPosition(sp);

                if (hasFlag) {
                    ball.setFillColor(sf::Color(255, 255, 255, baseAlpha));
                    auto& tex = flagIt->second;
                    ball.setTexture(&tex);
                    // Center-square crop for rectangular flags on circle
                    auto ts = tex.getSize();
                    int side = static_cast<int>(std::min(ts.x, ts.y));
                    int ox = (static_cast<int>(ts.x) - side) / 2;
                    int oy = (static_cast<int>(ts.y) - side) / 2;
                    ball.setTextureRect(sf::IntRect(ox, oy, side, side));
                } else if (avatarOnBall) {
                    ball.setFillColor(sf::Color(255, 255, 255, baseAlpha));
                    ball.setTexture(avatarTex);
                    auto ts = avatarTex->getSize();
                    int side = static_cast<int>(std::min(ts.x, ts.y));
                    int ox = (static_cast<int>(ts.x) - side) / 2;
                    int oy = (static_cast<int>(ts.y) - side) / 2;
                    ball.setTextureRect(sf::IntRect(ox, oy, side, side));
                } else {
                    sf::Color fill = p.color;
                    fill.a = baseAlpha;
                    ball.setFillColor(fill);
                }
                ball.setOutlineColor(outlineCol);
                ball.setOutlineThickness(outlineThk);
                target.draw(ball);

                // Inner highlight (only for plain colored circle balls)
                if (!hasFlag && !avatarOnBall) {
                    float hlR = rpx * 0.35f;
                    sf::CircleShape hl(hlR, 16);
                    hl.setOrigin(hlR, hlR);
                    hl.setPosition(sp.x - rpx * 0.25f, sp.y - rpx * 0.25f);
                    hl.setFillColor(sf::Color(255, 255, 255, static_cast<sf::Uint8>(baseAlpha * 0.25f)));
                    target.draw(hl);
                }
            }
        }

        // Shield glow
        if (p.hasShield && p.alive) {
            float pulse = 0.5f + 0.5f * std::sin(m_globalTime * 5.0f);
            sf::Uint8 sAlpha = static_cast<sf::Uint8>(120 + 100 * pulse);
            if (m_flagShapeRect) {
                sf::RectangleShape shield(sf::Vector2f(halfW * 2 + 10.0f, halfH * 2 + 10.0f));
                shield.setOrigin(halfW + 5.0f, halfH + 5.0f);
                shield.setPosition(sp);
                shield.setFillColor(sf::Color::Transparent);
                shield.setOutlineColor(sf::Color(80, 200, 255, sAlpha));
                shield.setOutlineThickness(3.0f);
                target.draw(shield);
            } else {
                sf::CircleShape shield(rpx + 5.0f, 32);
                shield.setOrigin(rpx + 5.0f, rpx + 5.0f);
                shield.setPosition(sp);
                shield.setFillColor(sf::Color::Transparent);
                shield.setOutlineColor(sf::Color(80, 200, 255, sAlpha));
                shield.setOutlineThickness(3.0f);
                target.draw(shield);
            }
        }

        // Label text on ball (only when plain colored ball)
        if (m_fontLoaded && !hasFlag && !avatarOnBall) {
            sf::Text lbl;
            lbl.setFont(m_font);
            lbl.setString(p.label);
            lbl.setCharacterSize(fs(static_cast<int>(rpx * 0.85f * m_labelTextScale)));
            lbl.setFillColor(sf::Color(255, 255, 255, baseAlpha));
            lbl.setOutlineColor(sf::Color(0, 0, 0, static_cast<sf::Uint8>(baseAlpha * 0.8f)));
            lbl.setOutlineThickness(1.5f);
            auto lb = lbl.getLocalBounds();
            lbl.setOrigin(lb.left + lb.width / 2.0f, lb.top + lb.height / 2.0f);
            lbl.setPosition(sp);
            target.draw(lbl);
        }

        // Name below (alive only, skip bots if disabled)
        if (m_fontLoaded && p.alive && (!p.isBot() || m_showBotNames)) {
            float nameY = sp.y + halfH + 4.0f;

            // Avatar circle (left of name) — skip if avatar is already on the ball
            const sf::Texture* nameAvatarTex = avatarOnBall ? nullptr : avatarTex;

            float avatarDiam = rpx * 0.9f * m_avatarScale;
            float avatarOffset = 0.0f;

            if (nameAvatarTex) {
                sf::Sprite avatar(*nameAvatarTex);
                auto ats = nameAvatarTex->getSize();
                int aSide = static_cast<int>(std::min(ats.x, ats.y));
                int aox = (static_cast<int>(ats.x) - aSide) / 2;
                int aoy = (static_cast<int>(ats.y) - aSide) / 2;
                avatar.setTextureRect(sf::IntRect(aox, aoy, aSide, aSide));
                avatar.setOrigin(aSide / 2.0f, aSide / 2.0f);
                float avatarS = avatarDiam / static_cast<float>(aSide);
                avatar.setScale(avatarS, avatarS);
                // Will position after we measure the name text
                avatarOffset = avatarDiam / 2.0f + 3.0f;

                sf::Text name;
                name.setFont(m_font);
                name.setString(p.displayName);
                name.setCharacterSize(fs(static_cast<int>(20 * m_nameTextScale)));
                auto nb = name.getLocalBounds();
                float totalW = avatarDiam + 3.0f + nb.width;
                float startX = sp.x - totalW / 2.0f;

                avatar.setPosition(startX + avatarDiam / 2.0f, nameY + avatarDiam / 2.0f);
                target.draw(avatar);

                // Circular outline around avatar
                sf::CircleShape ring(avatarDiam / 2.0f, 20);
                ring.setOrigin(avatarDiam / 2.0f, avatarDiam / 2.0f);
                ring.setPosition(avatar.getPosition());
                ring.setFillColor(sf::Color::Transparent);
                ring.setOutlineColor(sf::Color(255, 255, 255, static_cast<sf::Uint8>(baseAlpha * 0.5f)));
                ring.setOutlineThickness(m_avatarOutlineThickness);
                target.draw(ring);

                name.setFillColor(sf::Color(255, 255, 255, 180));
                name.setOutlineColor(sf::Color(0, 0, 0, 140));
                name.setOutlineThickness(1.0f);
                name.setOrigin(0.0f, 0.0f);
                name.setPosition(startX + avatarDiam + 3.0f, nameY);
                target.draw(name);
            } else {
                sf::Text name;
                name.setFont(m_font);
                name.setString(p.displayName);
                name.setCharacterSize(fs(static_cast<int>(20 * m_nameTextScale)));
                name.setFillColor(sf::Color(255, 255, 255, 180));
                name.setOutlineColor(sf::Color(0, 0, 0, 140));
                name.setOutlineThickness(1.0f);
                auto nb = name.getLocalBounds();
                name.setOrigin(nb.left + nb.width / 2.0f, 0.0f);
                name.setPosition(sp.x, nameY);
                target.draw(name);
            }
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

// ── Country Leaderboard Panel ─────────────────────────────────────────────────

void CountryElimination::renderRoundWinners(sf::RenderTarget& target, const ScreenLayout& L) {
    if (!m_fontLoaded || m_countryWins.empty()) return;

    // Panel position: top-right of safe zone
    float panelW = std::min(320.0f, L.safeW * 0.5f);
    float panelX = L.safeRight - panelW - 10.0f;
    float panelY = L.H * 0.015f;
    int maxShow = std::min(static_cast<int>(m_countryWins.size()), 10);

    float lineH = fs(24) + 10.0f;
    float headerH = fs(26) + fs(20) + 20.0f;
    float panelH = headerH + maxShow * lineH + 12.0f;

    // Semi-transparent background
    sf::RectangleShape bg(sf::Vector2f(panelW, panelH));
    bg.setPosition(panelX, panelY);
    bg.setFillColor(sf::Color(0, 0, 0, 160));
    bg.setOutlineColor(sf::Color(255, 215, 0, 60));
    bg.setOutlineThickness(1.0f);
    target.draw(bg);

    // Header
    {
        sf::Text header;
        header.setFont(m_font);
        header.setString("COUNTRIES");
        header.setCharacterSize(fs(26));
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
        sub.setCharacterSize(fs(20));
        sub.setFillColor(sf::Color(200, 200, 200, 160));
        auto sb = sub.getLocalBounds();
        sub.setOrigin(sb.left + sb.width / 2.0f, 0.0f);
        sub.setPosition(panelX + panelW / 2.0f, panelY + fs(26) + 10.0f);
        target.draw(sub);
    }

    // Entries — flag + country label + wins
    float y = panelY + headerH;
    for (int i = 0; i < maxShow; ++i) {
        const auto& cw = m_countryWins[i];

        // Rank
        sf::Text rank;
        rank.setFont(m_font);
        rank.setString(std::to_string(i + 1) + ".");
        rank.setCharacterSize(fs(24));
        rank.setFillColor(i == 0 ? sf::Color(255, 215, 0, 220) : sf::Color(200, 200, 200, 200));
        rank.setPosition(panelX + 8.0f, y);
        target.draw(rank);

        // Flag circle
        float flagR = fs(24) * 0.45f;
        sf::CircleShape flag(flagR);
        flag.setOrigin(flagR, flagR);
        flag.setPosition(panelX + 50.0f, y + lineH / 2.0f);
        auto fit = m_flagTextures.find(cw.label);
        if (fit != m_flagTextures.end()) {
            flag.setTexture(&fit->second);
            auto ts = fit->second.getSize();
            int sq = std::min(ts.x, ts.y);
            int ox = (ts.x - sq) / 2;
            int oy = (ts.y - sq) / 2;
            flag.setTextureRect(sf::IntRect(ox, oy, sq, sq));
            flag.setFillColor(sf::Color::White);
        } else {
            flag.setFillColor(sf::Color(180, 180, 180));
        }
        flag.setOutlineColor(sf::Color(255, 255, 255, 100));
        flag.setOutlineThickness(1.0f);
        target.draw(flag);

        // Country label
        sf::Text name;
        name.setFont(m_font);
        name.setString(cw.label);
        name.setCharacterSize(fs(22));
        name.setFillColor(sf::Color(255, 255, 255, 210));
        name.setPosition(panelX + 50.0f + flagR + 8.0f, y + 2.0f);
        target.draw(name);

        // Wins count
        sf::Text wins;
        wins.setFont(m_font);
        wins.setString(std::to_string(cw.wins) + "W");
        wins.setCharacterSize(fs(24));
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
    float bounce = std::sin(m_globalTime * 3.0f) * 8.0f;

    // ── "WINNER!" text ──
    float winnerTextY = L.arenaCY - L.arenaRadiusPx * 0.55f;
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
        t.setPosition(cx, winnerTextY + bounce * 0.5f);
        target.draw(t);
    }

    // ── Large avatar circle (profile picture) ──
    float avatarR = L.arenaRadiusPx * 0.22f; // large avatar radius
    float avatarY = winnerTextY + fs(52) * 0.5f + avatarR + 15.0f;
    const sf::Texture* avatarTex = (!w.avatarUrl.empty())
        ? m_avatarCache.getTexture(w.avatarUrl) : nullptr;

    if (avatarTex) {
        sf::Sprite avatarSprite(*avatarTex);
        auto ts = avatarTex->getSize();
        float scale = (avatarR * 2.0f) / static_cast<float>(std::max(ts.x, ts.y));
        avatarSprite.setOrigin(ts.x / 2.0f, ts.y / 2.0f);
        avatarSprite.setScale(scale, scale);
        avatarSprite.setPosition(cx, avatarY + bounce);
        avatarSprite.setColor(sf::Color(255, 255, 255, static_cast<sf::Uint8>(255 * fadeIn)));
        target.draw(avatarSprite);

        // Circular outline ring
        sf::CircleShape ring(avatarR, 48);
        ring.setOrigin(avatarR, avatarR);
        ring.setPosition(cx, avatarY + bounce);
        ring.setFillColor(sf::Color::Transparent);
        ring.setOutlineColor(sf::Color(255, 215, 0, static_cast<sf::Uint8>(200 * fadeIn)));
        ring.setOutlineThickness(3.0f);
        target.draw(ring);
    }

    // ── Flag ball below avatar ──
    float flagR = L.arenaRadiusPx * 0.15f;
    float flagH = m_flagShapeRect ? (flagR / FLAG_ASPECT) : flagR;
    float flagY = avatarTex ? (avatarY + avatarR + flagR + 15.0f)
                            : (winnerTextY + fs(52) * 0.5f + flagR + 15.0f);

    std::string wLabelUp = w.label;
    for (auto& c : wLabelUp) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    auto wFlagIt = m_flagTextures.find(wLabelUp);
    bool wHasFlag = (wFlagIt != m_flagTextures.end());

    if (m_flagShapeRect) {
        sf::RectangleShape bigBall(sf::Vector2f(flagR * 2, flagH * 2));
        bigBall.setOrigin(flagR, flagH);
        bigBall.setPosition(cx, flagY + bounce);
        if (wHasFlag) { bigBall.setFillColor(sf::Color::White); bigBall.setTexture(&wFlagIt->second); }
        else bigBall.setFillColor(w.color);
        bigBall.setOutlineColor(sf::Color(255, 255, 255, static_cast<sf::Uint8>(200 * fadeIn)));
        bigBall.setOutlineThickness(m_flagOutline ? 3.0f : 0.0f);
        target.draw(bigBall);
    } else {
        sf::CircleShape bigBall(flagR, 48);
        bigBall.setOrigin(flagR, flagR);
        bigBall.setPosition(cx, flagY + bounce);
        if (wHasFlag) {
            bigBall.setFillColor(sf::Color::White);
            auto& tex = wFlagIt->second;
            bigBall.setTexture(&tex);
            auto ts = tex.getSize();
            int side = static_cast<int>(std::min(ts.x, ts.y));
            int ox = (static_cast<int>(ts.x) - side) / 2;
            int oy = (static_cast<int>(ts.y) - side) / 2;
            bigBall.setTextureRect(sf::IntRect(ox, oy, side, side));
        } else bigBall.setFillColor(w.color);
        bigBall.setOutlineColor(sf::Color(255, 255, 255, static_cast<sf::Uint8>(200 * fadeIn)));
        bigBall.setOutlineThickness(m_flagOutline ? 3.0f : 0.0f);
        target.draw(bigBall);
    }

    // Label on flag ball (only when no flag texture)
    if (!wHasFlag) {
        sf::Text lbl;
        lbl.setFont(m_font);
        lbl.setString(w.label);
        lbl.setCharacterSize(fs(static_cast<int>(flagR * 0.7f)));
        lbl.setFillColor(sf::Color(255, 255, 255, static_cast<sf::Uint8>(255 * fadeIn)));
        lbl.setOutlineColor(sf::Color(0, 0, 0, static_cast<sf::Uint8>(200 * fadeIn)));
        lbl.setOutlineThickness(2.0f);
        auto llb = lbl.getLocalBounds();
        lbl.setOrigin(llb.left + llb.width / 2.0f, llb.top + llb.height / 2.0f);
        lbl.setPosition(cx, flagY + bounce);
        target.draw(lbl);
    }

    // ── Display name (large, below flag) ──
    float nameY = flagY + (m_flagShapeRect ? flagH : flagR) + 20.0f;
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
        t.setPosition(cx, nameY);
        target.draw(t);
    }

    // ── Wins count ──
    {
        int wins = 0;
        for (const auto& rw : m_roundWinners) {
            if (rw.userId == w.userId) { wins = rw.wins; break; }
        }
        sf::Text t;
        t.setFont(m_font);
        t.setString("Wins: " + std::to_string(wins));
        t.setCharacterSize(fs(28));
        t.setFillColor(sf::Color(200, 200, 200, static_cast<sf::Uint8>(180 * fadeIn)));
        t.setOutlineColor(sf::Color(0, 0, 0, static_cast<sf::Uint8>(120 * fadeIn)));
        t.setOutlineThickness(1.0f);
        auto lb = t.getLocalBounds();
        t.setOrigin(lb.left + lb.width / 2.0f, lb.top + lb.height / 2.0f);
        t.setPosition(cx, nameY + 40.0f);
        target.draw(t);
    }

    // ── Next round countdown ──
    if (m_roundEndTimer < m_roundEndDuration) {
        sf::Text t;
        t.setFont(m_font);
        std::string nxt = "Next round in " + std::to_string(std::max(0, static_cast<int>(std::ceil(m_roundEndTimer)))) + "s";
        t.setString(nxt);
        t.setCharacterSize(fs(24));
        t.setFillColor(sf::Color(180, 180, 180, static_cast<sf::Uint8>(150 * fadeIn)));
        auto lb = t.getLocalBounds();
        t.setOrigin(lb.left + lb.width / 2.0f, lb.top + lb.height / 2.0f);
        t.setPosition(cx, nameY + 80.0f);
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
        float entryW = 300.0f;
        float entryH = fs(22) + 8.0f;
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
        t.setCharacterSize(fs(22));
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
    const float lineH = fs(24) + 8.0f;

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
        float hdrH = fs(28) + 12.0f;
        sf::RectangleShape hdr(sf::Vector2f(w, hdrH));
        hdr.setPosition(x, y);
        hdr.setFillColor(sf::Color(col.r, col.g, col.b, 40));
        target.draw(hdr);

        sf::Text t;
        t.setFont(m_font);
        t.setString(title);
        t.setCharacterSize(fs(24));
        t.setFillColor(col);
        t.setOutlineColor(sf::Color(0, 0, 0, 180));
        t.setOutlineThickness(1.0f);
        auto lb = t.getLocalBounds();
        t.setOrigin(lb.left + lb.width / 2.0f, lb.top + lb.height / 2.0f);
        t.setPosition(x + w / 2.0f, y + hdrH / 2.0f);
        target.draw(t);
        return y + hdrH + 6.0f;
    };

    // ── LEFT PANEL: Country + Player Leaderboards ─────────────────────────
    if (L.leftPanelW > 60.0f) {
        float px = L.leftPanelX;
        float pw = L.leftPanelW;
        float py = pad;

        // Card: Country Leaderboard
        {
            int maxShow = std::min(static_cast<int>(m_countryWins.size()), 10);
            float headerH = fs(28) + 12.0f;
            float cardH = headerH + 6.0f + std::max(1, maxShow) * lineH + pad;
            drawCard(px, py, pw, cardH);
            float cy = drawHeader(px, py, pw, "COUNTRIES", sf::Color(255, 215, 0, 220));

            if (m_countryWins.empty()) {
                sf::Text t;
                t.setFont(m_font);
                t.setString("No winners yet");
                t.setCharacterSize(fs(20));
                t.setFillColor(sf::Color(150, 150, 150, 140));
                auto lb = t.getLocalBounds();
                t.setOrigin(lb.left + lb.width / 2.0f, 0.0f);
                t.setPosition(px + pw / 2.0f, cy);
                target.draw(t);
            } else {
                for (int i = 0; i < maxShow; ++i) {
                    const auto& cw = m_countryWins[i];
                    float ey = cy + i * lineH;

                    // Rank number
                    sf::Text rank;
                    rank.setFont(m_font);
                    rank.setString(std::to_string(i + 1) + ".");
                    rank.setCharacterSize(fs(22));
                    rank.setFillColor(i == 0 ? sf::Color(255, 215, 0, 220) : sf::Color(180, 180, 180, 200));
                    rank.setPosition(px + 6.0f, ey);
                    target.draw(rank);

                    // Flag circle
                    float flagR = fs(22) * 0.5f;
                    sf::CircleShape flag(flagR);
                    flag.setOrigin(flagR, flagR);
                    flag.setPosition(px + 40.0f, ey + lineH / 2.0f);
                    auto fit = m_flagTextures.find(cw.label);
                    if (fit != m_flagTextures.end()) {
                        flag.setTexture(&fit->second);
                        auto ts = fit->second.getSize();
                        int sq = std::min(ts.x, ts.y);
                        int ox = (ts.x - sq) / 2;
                        int oy = (ts.y - sq) / 2;
                        flag.setTextureRect(sf::IntRect(ox, oy, sq, sq));
                        flag.setFillColor(sf::Color::White);
                    } else {
                        flag.setFillColor(sf::Color(180, 180, 180));
                    }
                    flag.setOutlineColor(sf::Color(255, 255, 255, 80));
                    flag.setOutlineThickness(1.0f);
                    target.draw(flag);

                    // Country label
                    sf::Text name;
                    name.setFont(m_font);
                    name.setString(cw.label);
                    name.setCharacterSize(fs(20));
                    name.setFillColor(sf::Color(230, 230, 240, 210));
                    name.setPosition(px + 40.0f + flagR + 6.0f, ey + 1.0f);
                    target.draw(name);

                    // Wins
                    sf::Text wins;
                    wins.setFont(m_font);
                    wins.setString(std::to_string(cw.wins) + "W");
                    wins.setCharacterSize(fs(22));
                    wins.setFillColor(sf::Color(255, 215, 0, 200));
                    auto wb = wins.getLocalBounds();
                    wins.setOrigin(wb.left + wb.width, 0.0f);
                    wins.setPosition(px + pw - 8.0f, ey);
                    target.draw(wins);
                }
            }
            py += cardH + pad;
        }

        // Card: Player Leaderboard (from PlayerDatabase, real players only)
        {
            int maxShow = std::min(static_cast<int>(m_cachedPlayerLeaderboard.size()), 8);
            float playerLineH = fs(26) + 10.0f;
            float headerH = fs(28) + 12.0f;
            float cardH = headerH + 6.0f + std::max(1, maxShow) * playerLineH + pad;
            drawCard(px, py, pw, cardH);
            float cy = drawHeader(px, py, pw, "PLAYERS", sf::Color(100, 200, 255, 220));

            if (m_cachedPlayerLeaderboard.empty()) {
                sf::Text t;
                t.setFont(m_font);
                t.setString("No players yet");
                t.setCharacterSize(fs(20));
                t.setFillColor(sf::Color(150, 150, 150, 140));
                auto lb = t.getLocalBounds();
                t.setOrigin(lb.left + lb.width / 2.0f, 0.0f);
                t.setPosition(px + pw / 2.0f, cy);
                target.draw(t);
            } else {
                for (int i = 0; i < maxShow; ++i) {
                    const auto& pe = m_cachedPlayerLeaderboard[i];
                    float ey = cy + i * playerLineH;

                    sf::Text rank;
                    rank.setFont(m_font);
                    rank.setString(std::to_string(i + 1) + ".");
                    rank.setCharacterSize(fs(22));
                    rank.setFillColor(i == 0 ? sf::Color(100, 200, 255, 220) : sf::Color(180, 180, 180, 200));
                    rank.setPosition(px + 6.0f, ey);
                    target.draw(rank);

                    // Avatar
                    float avR = fs(22) * 0.55f;
                    const sf::Texture* avatarTex = nullptr;
                    if (!pe.avatarUrl.empty())
                        avatarTex = m_avatarCache.getTexture(pe.avatarUrl);
                    if (avatarTex) {
                        sf::CircleShape av(avR);
                        av.setOrigin(avR, avR);
                        av.setPosition(px + 40.0f, ey + playerLineH / 2.0f);
                        av.setTexture(avatarTex);
                        av.setFillColor(sf::Color::White);
                        av.setOutlineColor(sf::Color(100, 200, 255, 100));
                        av.setOutlineThickness(1.0f);
                        target.draw(av);
                    } else {
                        sf::CircleShape dot(avR);
                        dot.setOrigin(avR, avR);
                        dot.setPosition(px + 40.0f, ey + playerLineH / 2.0f);
                        dot.setFillColor(sf::Color(100, 200, 255, 60));
                        dot.setOutlineColor(sf::Color(100, 200, 255, 80));
                        dot.setOutlineThickness(1.0f);
                        target.draw(dot);
                    }

                    sf::Text name;
                    name.setFont(m_font);
                    std::string disp = pe.displayName;
                    int maxChars = static_cast<int>(pw - 90.0f) / std::max(1, static_cast<int>(fs(20) * 0.6f));
                    if (static_cast<int>(disp.size()) > maxChars && maxChars > 3)
                        disp = disp.substr(0, maxChars - 2) + "..";
                    name.setString(disp);
                    name.setCharacterSize(fs(20));
                    name.setFillColor(sf::Color(230, 230, 240, 210));
                    name.setPosition(px + 40.0f + avR + 6.0f, ey + 1.0f);
                    target.draw(name);

                    sf::Text pts;
                    pts.setFont(m_font);
                    pts.setString(std::to_string(pe.points) + "p");
                    pts.setCharacterSize(fs(22));
                    pts.setFillColor(sf::Color(100, 200, 255, 200));
                    auto pb = pts.getLocalBounds();
                    pts.setOrigin(pb.left + pb.width, 0.0f);
                    pts.setPosition(px + pw - 8.0f, ey);
                    target.draw(pts);
                }
            }
            py += cardH + pad;
        }

        // Card: Recent Eliminations
        {
            int maxShow = std::min(static_cast<int>(m_eliminationFeed.size()), 8);
            float headerH = fs(28) + 12.0f;
            float cardH = headerH + 6.0f + std::max(1, maxShow) * lineH + pad;
            drawCard(px, py, pw, cardH);
            float cy = drawHeader(px, py, pw, "ELIMINATIONS", sf::Color(255, 80, 80, 220));

            if (m_eliminationFeed.empty()) {
                sf::Text t;
                t.setFont(m_font);
                t.setString("None yet");
                t.setCharacterSize(fs(20));
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

                    // Avatar or flag circle
                    float flagR = 8.0f;
                    const sf::Texture* avatarTex = nullptr;
                    if (!e.avatarUrl.empty())
                        avatarTex = m_avatarCache.getTexture(e.avatarUrl);

                    if (avatarTex) {
                        sf::CircleShape av(flagR);
                        av.setOrigin(flagR, flagR);
                        av.setPosition(px + 14.0f, ey + lineH / 2.0f);
                        av.setTexture(avatarTex);
                        av.setFillColor(sf::Color(255, 255, 255, a));
                        av.setOutlineColor(sf::Color(255, 255, 255, static_cast<sf::Uint8>(a / 3)));
                        av.setOutlineThickness(1.0f);
                        target.draw(av);
                    } else {
                        sf::CircleShape dot(flagR);
                        dot.setOrigin(flagR, flagR);
                        dot.setPosition(px + 14.0f, ey + lineH / 2.0f);
                        auto fit = m_flagTextures.find(e.label);
                        if (fit != m_flagTextures.end()) {
                            dot.setTexture(&fit->second);
                            auto ts = fit->second.getSize();
                            int sq = std::min(ts.x, ts.y);
                            int ox = (ts.x - sq) / 2;
                            int oy = (ts.y - sq) / 2;
                            dot.setTextureRect(sf::IntRect(ox, oy, sq, sq));
                            dot.setFillColor(sf::Color(255, 255, 255, a));
                        } else {
                            sf::Color dc = e.color; dc.a = a;
                            dot.setFillColor(dc);
                        }
                        dot.setOutlineColor(sf::Color(255, 255, 255, static_cast<sf::Uint8>(a / 3)));
                        dot.setOutlineThickness(1.0f);
                        target.draw(dot);
                    }

                    sf::Text t;
                    t.setFont(m_font);
                    t.setString(e.displayName + " [" + e.label + "]");
                    t.setCharacterSize(fs(20));
                    t.setFillColor(sf::Color(255, 120, 120, a));
                    t.setOutlineColor(sf::Color(0, 0, 0, static_cast<sf::Uint8>(a * 0.5f)));
                    t.setOutlineThickness(1.0f);
                    t.setPosition(px + 28.0f, ey);
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

            float headerH = fs(28) + 12.0f;
            float cardH = headerH + 6.0f + static_cast<float>(stats.size()) * lineH + pad;
            drawCard(px, py, pw, cardH);
            float cy = drawHeader(px, py, pw, "GAME STATS", sf::Color(100, 180, 255, 220));

            for (size_t i = 0; i < stats.size(); ++i) {
                float ey = cy + i * lineH;
                sf::Text lbl;
                lbl.setFont(m_font);
                lbl.setString(stats[i].label);
                lbl.setCharacterSize(fs(20));
                lbl.setFillColor(sf::Color(160, 160, 180, 180));
                lbl.setPosition(px + 8.0f, ey);
                target.draw(lbl);

                sf::Text val;
                val.setFont(m_font);
                val.setString(stats[i].value);
                val.setCharacterSize(fs(22));
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

            float headerH = fs(28) + 12.0f;
            float cardH = headerH + 6.0f + static_cast<float>(info.size()) * lineH + pad;
            drawCard(px, py, pw, cardH);
            float cy = drawHeader(px, py, pw, "ARENA INFO", sf::Color(180, 140, 255, 220));

            for (size_t i = 0; i < info.size(); ++i) {
                float ey = cy + i * lineH;
                sf::Text lbl;
                lbl.setFont(m_font);
                lbl.setString(info[i].label);
                lbl.setCharacterSize(fs(20));
                lbl.setFillColor(sf::Color(160, 160, 180, 180));
                lbl.setPosition(px + 8.0f, ey);
                target.draw(lbl);

                sf::Text val;
                val.setFont(m_font);
                val.setString(info[i].value);
                val.setCharacterSize(fs(22));
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

            float headerH = fs(28) + 12.0f;
            float smallH = fs(20) + 4.0f;
            float cardH = headerH + 6.0f + lines.size() * smallH + pad;
            drawCard(px, py, pw, cardH);
            float cy = drawHeader(px, py, pw, "HOW TO PLAY", sf::Color(100, 255, 150, 220));

            for (size_t i = 0; i < lines.size(); ++i) {
                if (lines[i].empty()) continue;
                sf::Text t;
                t.setFont(m_font);
                t.setString(lines[i]);
                t.setCharacterSize(fs(20));
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
        {{"command", "join"}, {"description", "Join with a country label"}, {"aliases", nlohmann::json::array({"play", "me"})}},
        {{"command", "1/2/3/4"}, {"description", "Answer quiz questions (A-D)"}, {"aliases", nlohmann::json::array({"a","b","c","d"})}},
    });
}

std::vector<std::pair<std::string, int>> CountryElimination::getLeaderboard() const {
    std::vector<std::pair<std::string, int>> result;
    for (const auto& cw : m_countryWins) {
        result.emplace_back(cw.label, cw.wins);
    }
    return result;
}

void CountryElimination::configure(const nlohmann::json& settings) {
    if (settings.contains("arena_speed") && settings["arena_speed"].is_number()) {
        m_arenaAngularVel = std::max(0.05f, settings["arena_speed"].get<float>());
        m_arenaSpeedDefault = m_arenaAngularVel;
    }
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
    if (settings.contains("name_text_scale") && settings["name_text_scale"].is_number())
        m_nameTextScale = std::clamp(settings["name_text_scale"].get<float>(), 0.3f, 3.0f);
    if (settings.contains("avatar_scale") && settings["avatar_scale"].is_number())
        m_avatarScale = std::clamp(settings["avatar_scale"].get<float>(), 0.3f, 3.0f);
    if (settings.contains("flag_shape_rect") && settings["flag_shape_rect"].is_boolean())
        m_flagShapeRect = settings["flag_shape_rect"].get<bool>();
    if (settings.contains("flag_outline") && settings["flag_outline"].is_boolean())
        m_flagOutline = settings["flag_outline"].get<bool>();
    if (settings.contains("flag_outline_thickness") && settings["flag_outline_thickness"].is_number())
        m_flagOutlineThickness = std::clamp(settings["flag_outline_thickness"].get<float>(), 0.0f, 10.0f);
    if (settings.contains("elim_infinite_linger") && settings["elim_infinite_linger"].is_boolean())
        m_elimInfiniteLinger = settings["elim_infinite_linger"].get<bool>();
    if (settings.contains("elim_persist_rounds") && settings["elim_persist_rounds"].is_boolean())
        m_elimPersistRounds = settings["elim_persist_rounds"].get<bool>();
    if (settings.contains("rainbow_ring") && settings["rainbow_ring"].is_boolean())
        m_rainbowRing = settings["rainbow_ring"].get<bool>();
    if (settings.contains("allow_reentry") && settings["allow_reentry"].is_boolean())
        m_allowReentry = settings["allow_reentry"].get<bool>();
    if (settings.contains("show_bot_names") && settings["show_bot_names"].is_boolean())
        m_showBotNames = settings["show_bot_names"].get<bool>();
    if (settings.contains("max_entries_per_player") && settings["max_entries_per_player"].is_number_integer())
        m_maxEntriesPerPlayer = std::max(1, settings["max_entries_per_player"].get<int>());
    if (settings.contains("label_text_scale") && settings["label_text_scale"].is_number())
        m_labelTextScale = std::clamp(settings["label_text_scale"].get<float>(), 0.3f, 3.0f);
    if (settings.contains("avatar_outline_thickness") && settings["avatar_outline_thickness"].is_number())
        m_avatarOutlineThickness = std::clamp(settings["avatar_outline_thickness"].get<float>(), 0.0f, 5.0f);
    if (settings.contains("text_elements") && settings["text_elements"].is_array())
        applyTextOverrides(settings["text_elements"]);

    // Quiz settings
    if (settings.contains("quiz_enabled") && settings["quiz_enabled"].is_boolean())
        m_quizEnabled = settings["quiz_enabled"].get<bool>();
    if (settings.contains("quiz_interval") && settings["quiz_interval"].is_number())
        m_quizInterval = std::max(10.0f, settings["quiz_interval"].get<float>());
    if (settings.contains("quiz_duration") && settings["quiz_duration"].is_number())
        m_quizDuration = std::clamp(settings["quiz_duration"].get<float>(), 5.0f, 60.0f);
    if (settings.contains("quiz_points") && settings["quiz_points"].is_number_integer())
        m_quizPoints = std::max(0, settings["quiz_points"].get<int>());
    if (settings.contains("quiz_shield_secs") && settings["quiz_shield_secs"].is_number())
        m_quizShieldSecs = std::max(0.0f, settings["quiz_shield_secs"].get<float>());

    // Configurable sizes
    if (settings.contains("ball_radius") && settings["ball_radius"].is_number())
        m_ballRadius = std::clamp(settings["ball_radius"].get<float>(), 0.2f, 2.0f);
    if (settings.contains("gap_initial") && settings["gap_initial"].is_number())
        m_gapInitial = std::clamp(settings["gap_initial"].get<float>(), 0.05f, 1.0f);
    if (settings.contains("wall_thickness") && settings["wall_thickness"].is_number())
        m_wallThickness = std::clamp(settings["wall_thickness"].get<float>(), 0.1f, 1.5f);
    if (settings.contains("arena_radius") && settings["arena_radius"].is_number())
        m_arenaRadius = std::clamp(settings["arena_radius"].get<float>(), 3.0f, 20.0f);
    if (settings.contains("countdown_duration") && settings["countdown_duration"].is_number())
        m_countdownDuration = std::max(1.0f, settings["countdown_duration"].get<float>());
    if (settings.contains("gravity") && settings["gravity"].is_number())
        m_gravity = std::clamp(settings["gravity"].get<float>(), 0.0f, 50.0f);
    if (settings.contains("elim_feed_max") && settings["elim_feed_max"].is_number_integer())
        m_elimFeedMax = std::max(0, settings["elim_feed_max"].get<int>());
    if (settings.contains("shield_duration_sub") && settings["shield_duration_sub"].is_number())
        m_shieldDurationSub = std::max(0.0f, settings["shield_duration_sub"].get<float>());
    if (settings.contains("shield_duration_superchat") && settings["shield_duration_superchat"].is_number())
        m_shieldDurationSuperchat = std::max(0.0f, settings["shield_duration_superchat"].get<float>());
    if (settings.contains("shield_duration_points") && settings["shield_duration_points"].is_number())
        m_shieldDurationPoints = std::max(0.0f, settings["shield_duration_points"].get<float>());
    if (settings.contains("score_win") && settings["score_win"].is_number_integer())
        m_scoreWin = std::max(0, settings["score_win"].get<int>());
    if (settings.contains("score_sub") && settings["score_sub"].is_number_integer())
        m_scoreSub = std::max(0, settings["score_sub"].get<int>());
    if (settings.contains("score_superchat") && settings["score_superchat"].is_number_integer())
        m_scoreSuperchat = std::max(0, settings["score_superchat"].get<int>());
    if (settings.contains("score_points") && settings["score_points"].is_number_integer())
        m_scorePoints = std::max(0, settings["score_points"].get<int>());
    if (settings.contains("score_participation") && settings["score_participation"].is_number_integer())
        m_scoreParticipation = std::max(0, settings["score_participation"].get<int>());
    if (settings.contains("allow_flagless_join") && settings["allow_flagless_join"].is_boolean())
        m_allowFlaglessJoin = settings["allow_flagless_join"].get<bool>();
    if (settings.contains("auto_detect_country") && settings["auto_detect_country"].is_boolean())
        m_autoDetectCountry = settings["auto_detect_country"].get<bool>();

    // Audio visualizer
    if (settings.contains("visualizer_enabled") && settings["visualizer_enabled"].is_boolean())
        m_visualizerEnabled = settings["visualizer_enabled"].get<bool>();
    if (settings.contains("visualizer_style") && settings["visualizer_style"].is_number_integer())
        m_visualizerStyle = std::clamp(settings["visualizer_style"].get<int>(), 0, 3);
    if (settings.contains("visualizer_height") && settings["visualizer_height"].is_number())
        m_visualizerHeight = std::clamp(settings["visualizer_height"].get<float>(), 5.0f, 200.0f);
    if (settings.contains("visualizer_opacity") && settings["visualizer_opacity"].is_number())
        m_visualizerOpacity = std::clamp(settings["visualizer_opacity"].get<float>(), 0.0f, 1.0f);
    if (settings.contains("visualizer_bands") && settings["visualizer_bands"].is_number_integer())
        m_visualizerBands = std::clamp(settings["visualizer_bands"].get<int>(), 8, 128);
    if (settings.contains("visualizer_smoothing") && settings["visualizer_smoothing"].is_number())
        m_visualizerSmoothing = std::clamp(settings["visualizer_smoothing"].get<float>(), 0.0f, 0.95f);
    if (settings.contains("visualizer_gain") && settings["visualizer_gain"].is_number())
        m_visualizerGain = std::clamp(settings["visualizer_gain"].get<float>(), 0.1f, 10.0f);

    spdlog::info("[CountryElimination] configure: infinite_linger={}, persist_rounds={}, max_elim_visible={}",
                 m_elimInfiniteLinger, m_elimPersistRounds, m_maxEliminatedVisible);

    if (m_world) spawnBots();
}

nlohmann::json CountryElimination::getSettings() const {
    return {
        {"arena_speed", m_arenaSpeedDefault},
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
        {"name_text_scale", m_nameTextScale},
        {"avatar_scale", m_avatarScale},
        {"flag_shape_rect", m_flagShapeRect},
        {"flag_outline", m_flagOutline},
        {"flag_outline_thickness", m_flagOutlineThickness},
        {"elim_infinite_linger", m_elimInfiniteLinger},
        {"elim_persist_rounds", m_elimPersistRounds},
        {"rainbow_ring", m_rainbowRing},
        {"allow_reentry", m_allowReentry},
        {"show_bot_names", m_showBotNames},
        {"max_entries_per_player", m_maxEntriesPerPlayer},
        {"label_text_scale", m_labelTextScale},
        {"avatar_outline_thickness", m_avatarOutlineThickness},
        {"quiz_enabled", m_quizEnabled},
        {"quiz_interval", m_quizInterval},
        {"quiz_duration", m_quizDuration},
        {"quiz_points", m_quizPoints},
        {"quiz_shield_secs", m_quizShieldSecs},
        {"ball_radius", m_ballRadius},
        {"gap_initial", m_gapInitial},
        {"wall_thickness", m_wallThickness},
        {"arena_radius", m_arenaRadius},
        {"countdown_duration", m_countdownDuration},
        {"gravity", m_gravity},
        {"elim_feed_max", m_elimFeedMax},
        {"shield_duration_sub", m_shieldDurationSub},
        {"shield_duration_superchat", m_shieldDurationSuperchat},
        {"shield_duration_points", m_shieldDurationPoints},
        {"score_win", m_scoreWin},
        {"score_sub", m_scoreSub},
        {"score_superchat", m_scoreSuperchat},
        {"score_points", m_scorePoints},
        {"score_participation", m_scoreParticipation},
        {"allow_flagless_join", m_allowFlaglessJoin},
        {"auto_detect_country", m_autoDetectCountry},
        {"visualizer_enabled", m_visualizerEnabled},
        {"visualizer_style", m_visualizerStyle},
        {"visualizer_height", m_visualizerHeight},
        {"visualizer_opacity", m_visualizerOpacity},
        {"visualizer_bands", m_visualizerBands},
        {"visualizer_smoothing", m_visualizerSmoothing},
        {"visualizer_gain", m_visualizerGain},
        {"text_elements", textElementsJson()},
    };
}

} // namespace is::games::country_elimination

// ═════════════════════════════════════════════════════════════════════════════
// Quiz System  (placed after namespace close — re-opened)
// ═════════════════════════════════════════════════════════════════════════════
namespace is::games::country_elimination {

// ── Quiz lifecycle ──────────────────────────────────────────────────────────

void CountryElimination::startQuiz() {
    auto& catalog = getQuizCatalog();
    if (catalog.empty()) return;

    // Pick next question from shuffled order
    if (m_quizOrderPos >= static_cast<int>(m_quizOrder.size())) {
        m_quizOrder = shuffledQuizIndices(m_rng);
        m_quizOrderPos = 0;
    }
    m_quizCurrentIdx = m_quizOrder[m_quizOrderPos++];
    m_quizActive = true;
    m_quizTimer = m_quizDuration;
    m_quizRevealTimer = 0.0f;
    m_quizCorrectCount = 0;
    m_quizAnswers.clear();

    spdlog::info("[CountryElimination] Quiz started: Q#{}", m_quizCurrentIdx);
}

void CountryElimination::endQuiz() {
    if (!m_quizActive && m_quizRevealTimer <= 0.0f) return;

    auto& catalog = getQuizCatalog();
    if (m_quizCurrentIdx < 0 || m_quizCurrentIdx >= static_cast<int>(catalog.size())) {
        m_quizActive = false;
        return;
    }

    const auto& q = catalog[m_quizCurrentIdx];
    int correctIdx = q.correctIndex;

    // Award points and shields to correct answerers
    m_quizCorrectCount = 0;
    for (const auto& [userId, ans] : m_quizAnswers) {
        if (ans != correctIdx) continue;
        m_quizCorrectCount++;

        // Find a player entry for this user
        for (auto& [key, p] : m_players) {
            if (p.userId != userId) continue;
            p.score += m_quizPoints;
            if (p.alive && m_quizShieldSecs > 0.0f) {
                p.hasShield = true;
                p.shieldTimer = std::max(p.shieldTimer, m_quizShieldSecs);
            }
            break; // one reward per user
        }
    }

    m_quizActive = false;
    m_quizRevealTimer = 4.0f; // show correct answer for 4 seconds
    m_quizCooldown = m_quizInterval;

    if (m_quizCorrectCount > 0) {
        sendChatFeedback("✅ " + std::to_string(m_quizCorrectCount) + " correct! Answer: "
                         + std::string(1, static_cast<char>('A' + correctIdx)) + ") " + q.options[correctIdx]);
    } else {
        sendChatFeedback("❌ Nobody got it! Answer: "
                         + std::string(1, static_cast<char>('A' + correctIdx)) + ") " + q.options[correctIdx]);
    }
}

void CountryElimination::updateQuiz(float dt) {
    if (!m_quizEnabled) return;

    // Reveal phase (showing correct answer after quiz ended)
    if (m_quizRevealTimer > 0.0f) {
        m_quizRevealTimer -= dt;
        return;
    }

    // Active quiz countdown
    if (m_quizActive) {
        m_quizTimer -= dt;
        if (m_quizTimer <= 0.0f) {
            endQuiz();
        }
        return;
    }

    // Cooldown until next quiz
    m_quizCooldown -= dt;
    if (m_quizCooldown <= 0.0f) {
        startQuiz();
    }
}

void CountryElimination::handleQuizAnswer(const std::string& userId,
                                           const std::string& displayName,
                                           int answerIndex) {
    if (!m_quizActive) return;
    if (answerIndex < 0 || answerIndex > 3) return;

    // Only first answer counts
    if (m_quizAnswers.count(userId)) return;
    m_quizAnswers[userId] = answerIndex;
}

// ── Quiz Rendering ──────────────────────────────────────────────────────────

void CountryElimination::renderQuizOverlay(sf::RenderTarget& target, const ScreenLayout& L) {
    if (!m_fontLoaded) return;
    if (!m_quizEnabled) return;

    auto& catalog = getQuizCatalog();
    if (m_quizCurrentIdx < 0 || m_quizCurrentIdx >= static_cast<int>(catalog.size())) return;

    bool showingReveal = (!m_quizActive && m_quizRevealTimer > 0.0f);
    if (!m_quizActive && !showingReveal) return;

    const auto& q = catalog[m_quizCurrentIdx];
    float cx = (L.safeLeft + L.safeRight) / 2.0f;

    // Position: below the arena circle
    float topY = L.arenaCY + L.arenaRadiusPx + L.H * 0.03f;
    float boxW = L.safeW * 0.85f;
    float boxX = cx - boxW / 2.0f;

    // Semi-transparent background panel
    float panelH = L.H * 0.22f;
    sf::RectangleShape panel(sf::Vector2f(boxW, panelH));
    panel.setPosition(boxX, topY);
    panel.setFillColor(sf::Color(15, 18, 40, 210));
    panel.setOutlineColor(sf::Color(100, 140, 220, 120));
    panel.setOutlineThickness(1.5f);
    target.draw(panel);

    float pad = L.safeW * 0.03f;
    float textX = boxX + pad;
    float textW = boxW - pad * 2.0f;

    // ── Timer bar ────────────────────────────────────────────────────────
    if (m_quizActive) {
        float barH = 4.0f;
        float frac = std::clamp(m_quizTimer / m_quizDuration, 0.0f, 1.0f);
        sf::RectangleShape barBg(sf::Vector2f(boxW - pad * 2, barH));
        barBg.setPosition(textX, topY + 6.0f);
        barBg.setFillColor(sf::Color(40, 40, 60, 150));
        target.draw(barBg);
        sf::RectangleShape bar(sf::Vector2f((boxW - pad * 2) * frac, barH));
        bar.setPosition(textX, topY + 6.0f);
        sf::Uint8 r = static_cast<sf::Uint8>(255 * (1.0f - frac));
        sf::Uint8 g = static_cast<sf::Uint8>(255 * frac);
        bar.setFillColor(sf::Color(r, g, 80));
        target.draw(bar);
    }

    // ── Question text ────────────────────────────────────────────────────
    float qY = topY + 16.0f;
    {
        sf::Text qt;
        qt.setFont(m_font);
        qt.setString(q.question);
        qt.setCharacterSize(fs(28));
        qt.setFillColor(sf::Color(255, 255, 255, 230));
        qt.setOutlineColor(sf::Color(0, 0, 0, 180));
        qt.setOutlineThickness(1.0f);
        // Wrap: if too wide, shrink font
        auto lb = qt.getLocalBounds();
        if (lb.width > textW) {
            float scale = textW / lb.width;
            qt.setScale(scale, scale);
            lb = qt.getLocalBounds();
        }
        qt.setOrigin(lb.left + lb.width / 2.0f, lb.top);
        qt.setPosition(cx, qY);
        target.draw(qt);
        qY += lb.height * qt.getScale().y + 12.0f;
    }

    // ── 4 answer options (2×2 grid) ──────────────────────────────────────
    const char* labels[] = {"A)", "B)", "C)", "D)"};
    sf::Color optColors[] = {
        sf::Color(70, 130, 230),  // A - blue
        sf::Color(230, 160, 50),  // B - orange
        sf::Color(60, 190, 100),  // C - green
        sf::Color(200, 70, 100),  // D - red/pink
    };

    float cellW = (textW - pad) / 2.0f;
    float cellH = (panelH - (qY - topY) - 14.0f) / 2.0f;
    cellH = std::max(cellH, 28.0f);

    for (int i = 0; i < 4; ++i) {
        int col = i % 2;
        int row = i / 2;
        float ox = textX + col * (cellW + pad);
        float oy = qY + row * (cellH + 4.0f);

        // Cell background
        sf::RectangleShape cell(sf::Vector2f(cellW, cellH));
        cell.setPosition(ox, oy);

        bool isCorrect = (i == q.correctIndex);
        if (showingReveal && isCorrect) {
            cell.setFillColor(sf::Color(30, 140, 60, 200));
            cell.setOutlineColor(sf::Color(80, 255, 120, 200));
            cell.setOutlineThickness(2.0f);
        } else if (showingReveal && !isCorrect) {
            cell.setFillColor(sf::Color(60, 20, 20, 150));
            cell.setOutlineColor(sf::Color(100, 50, 50, 80));
            cell.setOutlineThickness(1.0f);
        } else {
            cell.setFillColor(sf::Color(optColors[i].r, optColors[i].g, optColors[i].b, 50));
            cell.setOutlineColor(sf::Color(optColors[i].r, optColors[i].g, optColors[i].b, 140));
            cell.setOutlineThickness(1.0f);
        }
        target.draw(cell);

        // Option text
        sf::Text ot;
        ot.setFont(m_font);
        ot.setString(std::string(labels[i]) + " " + q.options[i]);
        ot.setCharacterSize(fs(24));
        ot.setFillColor(sf::Color(255, 255, 255, showingReveal && !isCorrect ? static_cast<sf::Uint8>(100) : static_cast<sf::Uint8>(220)));
        ot.setOutlineColor(sf::Color(0, 0, 0, 150));
        ot.setOutlineThickness(1.0f);

        auto lb = ot.getLocalBounds();
        if (lb.width > cellW - 10.0f) {
            float scale = (cellW - 10.0f) / lb.width;
            ot.setScale(scale, scale);
            lb = ot.getLocalBounds();
        }
        ot.setOrigin(lb.left, lb.top + lb.height / 2.0f);
        ot.setPosition(ox + 6.0f, oy + cellH / 2.0f);
        target.draw(ot);
    }

    // ── Result text during reveal phase ──────────────────────────────────
    if (showingReveal) {
        sf::Text rt;
        rt.setFont(m_font);
        if (m_quizCorrectCount > 0)
            rt.setString(std::to_string(m_quizCorrectCount) + " correct!");
        else
            rt.setString("Nobody got it!");
        rt.setCharacterSize(fs(24));
        rt.setFillColor(m_quizCorrectCount > 0 ? sf::Color(80, 255, 120, 230) : sf::Color(255, 100, 100, 230));
        rt.setOutlineColor(sf::Color(0, 0, 0, 180));
        rt.setOutlineThickness(1.0f);
        auto lb = rt.getLocalBounds();
        rt.setOrigin(lb.left + lb.width / 2.0f, lb.top + lb.height / 2.0f);
        rt.setPosition(cx, topY + panelH - 10.0f);
        target.draw(rt);
    }

    // ── "Answer 1-4 in chat!" hint during active quiz ────────────────────
    if (m_quizActive) {
        sf::Text ht;
        ht.setFont(m_font);
        ht.setString("Type 1-4 in chat to answer!");
        ht.setCharacterSize(fs(20));
        ht.setFillColor(sf::Color(200, 200, 255, 180));
        ht.setOutlineColor(sf::Color(0, 0, 0, 120));
        ht.setOutlineThickness(1.0f);
        auto lb = ht.getLocalBounds();
        ht.setOrigin(lb.left + lb.width / 2.0f, lb.top + lb.height / 2.0f);
        ht.setPosition(cx, topY + panelH - 10.0f);
        target.draw(ht);
    }
}

} // namespace is::games::country_elimination
