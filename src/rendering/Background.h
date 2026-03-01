#pragma once

#include <SFML/Graphics.hpp>

namespace is::rendering {

/// Animated parallax background for visual depth.
class Background {
public:
    Background() = default;

    /// Initialize with render target size.
    void initialize(unsigned int width, unsigned int height);

    /// Update background animation.
    void update(float dt);

    /// Render the background.
    void render(sf::RenderTarget& target);

private:
    /// A star/particle in the background.
    struct BackgroundStar {
        sf::Vector2f position;
        float        size;
        float        speed;
        float        brightness;
        float        twinklePhase;
    };

    std::vector<BackgroundStar> m_stars;
    float m_width  = 1080;
    float m_height = 1920;
    float m_time   = 0.0f;

    // Nebula-like color regions
    struct NebulaBlob {
        sf::Vector2f position;
        float        radius;
        sf::Color    color;
        float        phase;
    };
    std::vector<NebulaBlob> m_nebulae;
};

} // namespace is::rendering
