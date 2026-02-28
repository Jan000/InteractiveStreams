#pragma once

#include <box2d/box2d.h>
#include <SFML/Graphics.hpp>
#include <vector>

namespace is::games::chaos_arena {

/// Represents an arena platform / obstacle.
struct ArenaPlatform {
    b2Body*     body = nullptr;
    sf::Vector2f position;
    sf::Vector2f size;
    sf::Color    color;
    bool         destructible = false;
    float        health       = 100.0f;
    float        maxHealth    = 100.0f;
};

/// The battle arena with platforms, walls, and hazards.
class Arena {
public:
    Arena() = default;
    ~Arena() = default;

    /// Generate the arena layout in the given physics world.
    void generate(b2World& world, float width, float height);

    /// Update arena state (e.g., crumbling platforms).
    void update(double dt);

    /// Render the arena.
    void render(sf::RenderTarget& target, float pixelsPerMeter);

    /// Get world bounds.
    float width() const  { return m_width; }
    float height() const { return m_height; }

    /// Check if a position is in a death zone (below arena).
    bool isInDeathZone(const b2Vec2& pos) const;

    const std::vector<ArenaPlatform>& platforms() const { return m_platforms; }

private:
    void createBoundary(b2World& world);
    void createMainPlatform(b2World& world);
    void createFloatingPlatforms(b2World& world);
    void createDestructibleBlocks(b2World& world);

    b2Body* createStaticBox(b2World& world, float x, float y, float hw, float hh);

    std::vector<ArenaPlatform> m_platforms;
    float m_width  = 40.0f;
    float m_height = 22.5f;

    // Visual
    sf::Color m_bgColor{20, 20, 35};
    float m_gridAlpha = 0.08f;
};

} // namespace is::games::chaos_arena
