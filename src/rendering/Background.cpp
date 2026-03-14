#include "rendering/Background.h"
#include <cmath>
#include <random>

namespace is::rendering {

void Background::initialize(unsigned int width, unsigned int height, int starCount) {
    m_width = static_cast<float>(width);
    m_height = static_cast<float>(height);

    std::mt19937 rng(42);
    std::uniform_real_distribution<float> xDist(0, m_width);
    std::uniform_real_distribution<float> yDist(0, m_height);
    std::uniform_real_distribution<float> sizeDist(0.5f, 2.5f);
    std::uniform_real_distribution<float> speedDist(0.1f, 0.5f);
    std::uniform_real_distribution<float> brightDist(0.3f, 1.0f);
    std::uniform_real_distribution<float> phaseDist(0.0f, 6.28f);

    m_stars.clear();
    int count = std::clamp(starCount, 0, 500);
    for (int i = 0; i < count; ++i) {
        m_stars.push_back({
            {xDist(rng), yDist(rng)},
            sizeDist(rng),
            speedDist(rng),
            brightDist(rng),
            phaseDist(rng)
        });
    }
    m_vertices.resize(static_cast<size_t>(count) * 4);
}

void Background::update(float dt) {
    m_time += dt;

    // Pre-compute vertex positions and opaque colors (no alpha blending)
    for (size_t i = 0; i < m_stars.size(); ++i) {
        const auto& star = m_stars[i];
        float twinkle = 0.6f + 0.4f * std::sin(m_time * star.speed * 5.0f + star.twinklePhase);
        sf::Uint8 gray = static_cast<sf::Uint8>(255 * star.brightness * twinkle);
        sf::Color c(gray, gray, gray); // Opaque — no alpha blend cost

        float s = star.size;
        float x = star.position.x;
        float y = star.position.y;
        size_t vi = i * 4;
        m_vertices[vi + 0] = {{x - s, y - s}, c};
        m_vertices[vi + 1] = {{x + s, y - s}, c};
        m_vertices[vi + 2] = {{x + s, y + s}, c};
        m_vertices[vi + 3] = {{x - s, y + s}, c};
    }
}

void Background::render(sf::RenderTarget& target, float zoom,
                        float cx, float cy) {
    if (zoom != 1.0f && zoom > 0.0f) {
        sf::Transform xf;
        xf.translate(cx, cy);
        xf.scale(zoom, zoom);
        xf.translate(-cx, -cy);
        target.draw(m_vertices, sf::RenderStates(xf));
    } else {
        target.draw(m_vertices);
    }
}

} // namespace is::rendering
