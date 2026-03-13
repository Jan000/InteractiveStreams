#pragma once

#include <SFML/Graphics.hpp>

namespace is::rendering {

/// Animated parallax background for visual depth.
/// CPU-optimized: single batched VertexArray draw, no alpha blending.
class Background {
public:
    Background() = default;

    /// Initialize with render target size.
    void initialize(unsigned int width, unsigned int height, int starCount = 120);

    /// Update background animation.
    void update(float dt);

    /// Render the background.
    void render(sf::RenderTarget& target);

private:
    struct BackgroundStar {
        sf::Vector2f position;
        float        size;
        float        speed;
        float        brightness;
        float        twinklePhase;
    };

    std::vector<BackgroundStar> m_stars;
    sf::VertexArray m_vertices{sf::Quads};
    float m_width  = 1080;
    float m_height = 1920;
    float m_time   = 0.0f;
};

} // namespace is::rendering
