#pragma once

#include <box2d/box2d.h>
#include <SFML/Graphics.hpp>
#include <vector>
#include <random>

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

    /// Generate the arena layout in the given physics world (random seed).
    void generate(b2World& world, float width, float height);

    /// Generate the arena with a specific seed for reproducible layouts.
    void generate(b2World& world, float width, float height, unsigned int seed);

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

    /// Get the seed used for the current layout.
    unsigned int currentSeed() const { return m_seed; }

private:
    void createBoundary(b2World& world);
    void createMainPlatform(b2World& world);
    void generateProceduralPlatforms(b2World& world, std::mt19937& rng);
    void generateProceduralBlocks(b2World& world, std::mt19937& rng);

    b2Body* createStaticBox(b2World& world, float x, float y, float hw, float hh);

    /// Check if a rectangle overlaps with existing platforms
    bool overlapsExisting(float x, float y, float hw, float hh, float margin) const;

    std::vector<ArenaPlatform> m_platforms;
    float m_width  = 40.0f;
    float m_height = 22.5f;
    unsigned int m_seed = 0;

    // Visual
    sf::Color m_bgColor{20, 20, 35};
    float m_gridAlpha = 0.08f;
};

} // namespace is::games::chaos_arena
