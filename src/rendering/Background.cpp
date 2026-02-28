#include "rendering/Background.h"
#include <cmath>
#include <random>

namespace is::rendering {

void Background::initialize(unsigned int width, unsigned int height) {
    m_width = static_cast<float>(width);
    m_height = static_cast<float>(height);

    std::mt19937 rng(42);  // Deterministic seed for consistent background
    std::uniform_real_distribution<float> xDist(0, m_width);
    std::uniform_real_distribution<float> yDist(0, m_height);
    std::uniform_real_distribution<float> sizeDist(0.5f, 3.0f);
    std::uniform_real_distribution<float> speedDist(0.1f, 0.5f);
    std::uniform_real_distribution<float> brightDist(0.3f, 1.0f);
    std::uniform_real_distribution<float> phaseDist(0.0f, 6.28f);

    // Generate stars
    m_stars.clear();
    for (int i = 0; i < 200; ++i) {
        m_stars.push_back({
            {xDist(rng), yDist(rng)},
            sizeDist(rng),
            speedDist(rng),
            brightDist(rng),
            phaseDist(rng)
        });
    }

    // Generate nebula blobs
    m_nebulae.clear();
    std::uniform_real_distribution<float> radiusDist(100, 300);
    m_nebulae.push_back({{m_width * 0.2f, m_height * 0.3f}, radiusDist(rng), sf::Color(30, 10, 60, 15), phaseDist(rng)});
    m_nebulae.push_back({{m_width * 0.7f, m_height * 0.6f}, radiusDist(rng), sf::Color(10, 20, 50, 15), phaseDist(rng)});
    m_nebulae.push_back({{m_width * 0.5f, m_height * 0.8f}, radiusDist(rng), sf::Color(40, 5, 30, 10), phaseDist(rng)});
}

void Background::update(float dt) {
    m_time += dt;
}

void Background::render(sf::RenderTarget& target) {
    // Nebula blobs (soft colored circles)
    for (const auto& neb : m_nebulae) {
        float pulse = 1.0f + std::sin(m_time * 0.3f + neb.phase) * 0.15f;
        float r = neb.radius * pulse;

        sf::CircleShape blob(r);
        blob.setOrigin(r, r);
        blob.setPosition(neb.position);
        blob.setFillColor(neb.color);
        target.draw(blob);
    }

    // Stars
    for (const auto& star : m_stars) {
        float twinkle = 0.6f + 0.4f * std::sin(m_time * star.speed * 5.0f + star.twinklePhase);
        sf::Uint8 alpha = static_cast<sf::Uint8>(255 * star.brightness * twinkle);

        sf::CircleShape dot(star.size);
        dot.setOrigin(star.size, star.size);
        dot.setPosition(star.position);
        dot.setFillColor(sf::Color(255, 255, 255, alpha));
        target.draw(dot);
    }
}

} // namespace is::rendering
