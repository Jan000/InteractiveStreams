#include "games/chaos_arena/Arena.h"
#include <spdlog/spdlog.h>
#include <cmath>
#include <algorithm>

namespace is::games::chaos_arena {

void Arena::generate(b2World& world, float width, float height) {
    // Use a random seed
    std::random_device rd;
    generate(world, width, height, rd());
}

void Arena::generate(b2World& world, float width, float height, unsigned int seed) {
    m_width = width;
    m_height = height;
    m_seed = seed;
    m_platforms.clear();

    std::mt19937 rng(seed);

    createBoundary(world);
    createMainPlatform(world);
    generateProceduralPlatforms(world, rng);
    generateProceduralBlocks(world, rng);

    spdlog::info("[Arena] Generated procedural layout (seed={}, {} platforms).",
                 seed, m_platforms.size());
}

void Arena::createBoundary(b2World& world) {
    float hw = m_width / 2.0f;
    float hh = m_height / 2.0f;
    float wallThickness = 0.5f;

    // Left wall
    auto* left = createStaticBox(world, -hw - wallThickness, 0.0f, wallThickness, hh + 2.0f);
    m_platforms.push_back({left, {-hw - wallThickness, 0.0f},
        {wallThickness * 2, (hh + 2.0f) * 2}, sf::Color(60, 60, 80), false});

    // Right wall
    auto* right = createStaticBox(world, hw + wallThickness, 0.0f, wallThickness, hh + 2.0f);
    m_platforms.push_back({right, {hw + wallThickness, 0.0f},
        {wallThickness * 2, (hh + 2.0f) * 2}, sf::Color(60, 60, 80), false});

    // Ceiling
    auto* ceiling = createStaticBox(world, 0.0f, -hh - wallThickness, hw + 2.0f, wallThickness);
    m_platforms.push_back({ceiling, {0.0f, -hh - wallThickness},
        {(hw + 2.0f) * 2, wallThickness * 2}, sf::Color(60, 60, 80), false});
}

void Arena::createMainPlatform(b2World& world) {
    float groundY = m_height / 2.0f - 1.0f;
    float groundHW = m_width / 2.0f - 2.0f;
    float groundHH = 1.0f;

    auto* ground = createStaticBox(world, 0.0f, groundY, groundHW, groundHH);
    m_platforms.push_back({ground, {0.0f, groundY},
        {groundHW * 2, groundHH * 2}, sf::Color(80, 80, 120), false});
}

void Arena::generateProceduralPlatforms(b2World& world, std::mt19937& rng) {
    // Divide arena vertically into tiers.  Each tier gets a random number
    // of platforms placed with constraints to ensure reachability:
    //   - maximum vertical gap ≤ ~5m  (player jump reach)
    //   - platforms don't overlap and have minimum spacing
    //   - at least one platform per tier for reachability

    float hh = m_height / 2.0f;
    float hw = m_width  / 2.0f;

    // Define tier Y-ranges (from bottom to top)
    // Arena Y goes from -hh (top) to +hh (bottom), ground is at +hh-1
    struct Tier { float yMin; float yMax; int minPlats; int maxPlats; };
    std::vector<Tier> tiers = {
        { hh * 0.5f,   hh * 0.8f,  1, 3 },   // Lower tier
        { hh * 0.1f,   hh * 0.45f, 2, 3 },   // Mid-low tier
        {-hh * 0.25f,  hh * 0.05f, 2, 4 },   // Center tier
        {-hh * 0.6f,  -hh * 0.1f,  2, 3 },   // Mid-high tier
        {-hh * 0.9f,  -hh * 0.5f,  1, 3 },   // Upper tier
        {-hh * 1.0f,  -hh * 0.8f,  0, 2 },   // Top tier (optional)
    };

    // Platform width and color distributions
    std::uniform_real_distribution<float> widthDist(1.8f, 3.5f);
    const float platHH = 0.3f;
    const float safeMargin = 1.5f; // minimum gap between platforms

    // Color palette for platforms (varied blues/purples)
    std::vector<sf::Color> palette = {
        sf::Color(100, 100, 160),
        sf::Color(90, 110, 150),
        sf::Color(110, 90, 160),
        sf::Color(80, 100, 140),
        sf::Color(100, 80, 150),
    };
    std::uniform_int_distribution<int> colorDist(0, static_cast<int>(palette.size()) - 1);

    for (const auto& tier : tiers) {
        std::uniform_int_distribution<int> countDist(tier.minPlats, tier.maxPlats);
        int count = countDist(rng);

        std::uniform_real_distribution<float> yDist(tier.yMin, tier.yMax);
        // X range: stay inside walls with margin
        float xMargin = 3.0f;

        for (int i = 0; i < count; ++i) {
            float platHW = widthDist(rng);
            std::uniform_real_distribution<float> xDist(-hw + xMargin, hw - xMargin);

            // Try to place the platform (avoid overlaps)
            bool placed = false;
            for (int attempt = 0; attempt < 20; ++attempt) {
                float px = xDist(rng);
                float py = yDist(rng);

                if (!overlapsExisting(px, py, platHW, platHH, safeMargin)) {
                    auto* body = createStaticBox(world, px, py, platHW, platHH);
                    m_platforms.push_back({body, {px, py},
                        {platHW * 2, platHH * 2}, palette[colorDist(rng)], false});
                    placed = true;
                    break;
                }
            }

            // If mandatory platform wasn't placed and this is a required tier,
            // force-place in the center region
            if (!placed && i < tier.minPlats) {
                float py = (tier.yMin + tier.yMax) / 2.0f;
                float px = (i % 2 == 0) ? -hw * 0.3f : hw * 0.3f;
                auto* body = createStaticBox(world, px, py, platHW, platHH);
                m_platforms.push_back({body, {px, py},
                    {platHW * 2, platHH * 2}, palette[colorDist(rng)], false});
            }
        }
    }
}

void Arena::generateProceduralBlocks(b2World& world, std::mt19937& rng) {
    // Place destructible blocks in gaps between platforms
    float hh = m_height / 2.0f;
    float hw = m_width  / 2.0f;

    std::uniform_int_distribution<int> countDist(5, 10);
    int count = countDist(rng);

    std::uniform_real_distribution<float> xDist(-hw + 3.0f, hw - 3.0f);
    std::uniform_real_distribution<float> yDist(-hh + 2.0f, hh - 3.0f);

    float blockHW = 0.6f;
    float blockHH = 0.6f;

    // Block color palette (warm tones)
    std::vector<sf::Color> blockPalette = {
        sf::Color(160, 100, 80),
        sf::Color(150, 110, 70),
        sf::Color(170, 90, 85),
        sf::Color(140, 100, 90),
    };
    std::uniform_int_distribution<int> colorDist(0, static_cast<int>(blockPalette.size()) - 1);

    std::uniform_real_distribution<float> healthDist(35.0f, 75.0f);

    for (int i = 0; i < count; ++i) {
        for (int attempt = 0; attempt < 20; ++attempt) {
            float bx = xDist(rng);
            float by = yDist(rng);

            if (!overlapsExisting(bx, by, blockHW, blockHH, 1.2f)) {
                auto* body = createStaticBox(world, bx, by, blockHW, blockHH);
                float hp = healthDist(rng);
                sf::Color col = blockPalette[colorDist(rng)];
                m_platforms.push_back({body, {bx, by},
                    {blockHW * 2, blockHH * 2}, col, true, hp, hp});
                break;
            }
        }
    }
}

bool Arena::overlapsExisting(float x, float y, float hw, float hh, float margin) const {
    for (const auto& plat : m_platforms) {
        float pHW = plat.size.x / 2.0f + margin;
        float pHH = plat.size.y / 2.0f + margin;
        float dx = std::abs(x - plat.position.x);
        float dy = std::abs(y - plat.position.y);
        if (dx < (hw + pHW) && dy < (hh + pHH)) {
            return true;
        }
    }
    return false;
}

b2Body* Arena::createStaticBox(b2World& world, float x, float y, float hw, float hh) {
    b2BodyDef bodyDef;
    bodyDef.type = b2_staticBody;
    bodyDef.position.Set(x, y);

    b2Body* body = world.CreateBody(&bodyDef);

    b2PolygonShape shape;
    shape.SetAsBox(hw, hh);

    b2FixtureDef fixtureDef;
    fixtureDef.shape = &shape;
    fixtureDef.friction = 0.6f;
    fixtureDef.restitution = 0.1f;
    body->CreateFixture(&fixtureDef);

    return body;
}

void Arena::update(double dt) {
    // Update destructible platforms
    for (auto it = m_platforms.begin(); it != m_platforms.end(); ) {
        if (it->destructible && it->health <= 0.0f) {
            // Remove from physics world
            if (it->body) {
                it->body->GetWorld()->DestroyBody(it->body);
                it->body = nullptr;
            }
            it = m_platforms.erase(it);
        } else {
            ++it;
        }
    }
}

void Arena::render(sf::RenderTarget& target, float ppm) {
    auto size = target.getSize();

    // Grid overlay for visual depth
    sf::Color gridColor(255, 255, 255, static_cast<sf::Uint8>(m_gridAlpha * 255));
    float gridSpacing = 2.0f * ppm;
    float offsetX = size.x / 2.0f;
    float offsetY = size.y / 2.0f;

    for (float x = std::fmod(offsetX, gridSpacing); x < size.x; x += gridSpacing) {
        sf::Vertex line[] = {
            sf::Vertex(sf::Vector2f(x, 0), gridColor),
            sf::Vertex(sf::Vector2f(x, static_cast<float>(size.y)), gridColor)
        };
        target.draw(line, 2, sf::Lines);
    }
    for (float y = std::fmod(offsetY, gridSpacing); y < size.y; y += gridSpacing) {
        sf::Vertex line[] = {
            sf::Vertex(sf::Vector2f(0, y), gridColor),
            sf::Vertex(sf::Vector2f(static_cast<float>(size.x), y), gridColor)
        };
        target.draw(line, 2, sf::Lines);
    }

    // Platforms
    for (const auto& plat : m_platforms) {
        sf::RectangleShape rect(sf::Vector2f(plat.size.x * ppm, plat.size.y * ppm));
        rect.setOrigin(plat.size.x * ppm / 2, plat.size.y * ppm / 2);
        rect.setPosition(offsetX + plat.position.x * ppm, offsetY + plat.position.y * ppm);

        sf::Color fillColor = plat.color;
        if (plat.destructible) {
            // Darken based on damage
            float pct = plat.health / plat.maxHealth;
            fillColor.r = static_cast<sf::Uint8>(fillColor.r * pct);
            fillColor.g = static_cast<sf::Uint8>(fillColor.g * pct);
        }
        rect.setFillColor(fillColor);

        // Subtle outline/glow
        rect.setOutlineThickness(1.0f);
        rect.setOutlineColor(sf::Color(
            static_cast<sf::Uint8>(std::min(255, fillColor.r + 30)),
            static_cast<sf::Uint8>(std::min(255, fillColor.g + 30)),
            static_cast<sf::Uint8>(std::min(255, fillColor.b + 40)),
            180));

        target.draw(rect);

        // Bright top edge highlight for platforms (not boundary walls)
        if (plat.size.y < 2.0f) {
            sf::RectangleShape topEdge(sf::Vector2f(plat.size.x * ppm, 2.0f));
            topEdge.setOrigin(plat.size.x * ppm / 2, plat.size.y * ppm / 2);
            topEdge.setPosition(offsetX + plat.position.x * ppm, offsetY + plat.position.y * ppm);
            topEdge.setFillColor(sf::Color(
                static_cast<sf::Uint8>(std::min(255, fillColor.r + 60)),
                static_cast<sf::Uint8>(std::min(255, fillColor.g + 60)),
                static_cast<sf::Uint8>(std::min(255, fillColor.b + 80)),
                220));
            target.draw(topEdge);
        }
    }
}

bool Arena::isInDeathZone(const b2Vec2& pos) const {
    // Below the arena
    return pos.y > m_height / 2.0f + 2.0f;
}

} // namespace is::games::chaos_arena
