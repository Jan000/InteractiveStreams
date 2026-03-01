#include "games/chaos_arena/Arena.h"
#include <cmath>

namespace is::games::chaos_arena {

void Arena::generate(b2World& world, float width, float height) {
    m_width = width;
    m_height = height;
    m_platforms.clear();

    createBoundary(world);
    createMainPlatform(world);
    createFloatingPlatforms(world);
    createDestructibleBlocks(world);
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

void Arena::createFloatingPlatforms(b2World& world) {
    // Platforms arranged for vertical arena (22.5 wide x 40 tall)
    struct PlatDef { float x, y, hw, hh; };
    std::vector<PlatDef> defs = {
        // Lower tier
        { -6.0f,  14.0f, 3.0f, 0.3f},
        {  6.0f,  14.0f, 3.0f, 0.3f},
        // Mid-low tier
        {  0.0f,  10.0f, 3.5f, 0.3f},
        { -8.0f,   7.0f, 2.5f, 0.3f},
        {  8.0f,   7.0f, 2.5f, 0.3f},
        // Center tier
        { -4.0f,   3.0f, 3.0f, 0.3f},
        {  4.0f,   3.0f, 3.0f, 0.3f},
        {  0.0f,  -1.0f, 3.5f, 0.3f},
        // Mid-high tier
        { -7.0f,  -5.0f, 2.5f, 0.3f},
        {  7.0f,  -5.0f, 2.5f, 0.3f},
        // Upper tier
        {  0.0f,  -9.0f, 3.0f, 0.3f},
        { -5.0f, -13.0f, 2.0f, 0.3f},
        {  5.0f, -13.0f, 2.0f, 0.3f},
        // Top tier
        {  0.0f, -16.0f, 2.5f, 0.3f},
    };

    sf::Color platColor(100, 100, 160);
    for (auto& d : defs) {
        auto* body = createStaticBox(world, d.x, d.y, d.hw, d.hh);
        m_platforms.push_back({body, {d.x, d.y},
            {d.hw * 2, d.hh * 2}, platColor, false});
    }
}

void Arena::createDestructibleBlocks(b2World& world) {
    // Destructible blocks spread vertically across the arena
    struct BlockDef { float x, y; };
    std::vector<BlockDef> blocks = {
        { -3.0f,  12.0f},
        {  3.0f,  12.0f},
        {  0.0f,   5.0f},
        { -6.0f,  -2.0f},
        {  6.0f,  -2.0f},
        {  0.0f,  -7.0f},
        { -4.0f, -11.0f},
        {  4.0f, -11.0f},
    };

    float blockHW = 0.6f;
    float blockHH = 0.6f;
    sf::Color blockColor(160, 100, 80);

    for (auto& b : blocks) {
        auto* body = createStaticBox(world, b.x, b.y, blockHW, blockHH);
        m_platforms.push_back({body, {b.x, b.y},
            {blockHW * 2, blockHH * 2}, blockColor, true, 50.0f, 50.0f});
    }
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
