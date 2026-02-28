#include "rendering/PostProcessing.h"
#include <cmath>

namespace is::rendering {

void PostProcessing::initialize(unsigned int width, unsigned int height) {
    m_width = width;
    m_height = height;
}

void PostProcessing::applyVignette(sf::RenderTarget& target, float intensity) {
    // Software vignette using multiple semi-transparent rectangles at edges
    float w = static_cast<float>(target.getSize().x);
    float h = static_cast<float>(target.getSize().y);

    // Four gradient strips for edges
    int steps = 20;
    for (int i = 0; i < steps; ++i) {
        float t = static_cast<float>(i) / steps;
        float alpha = intensity * t * t * 80;
        sf::Uint8 a = static_cast<sf::Uint8>(std::min(255.0f, alpha));

        float inset = t * w * 0.15f;

        // Top
        sf::RectangleShape top(sf::Vector2f(w, inset / steps));
        top.setPosition(0, i * inset / steps);
        top.setFillColor(sf::Color(0, 0, 0, a));
        target.draw(top);

        // Bottom
        sf::RectangleShape bottom(sf::Vector2f(w, inset / steps));
        bottom.setPosition(0, h - (i + 1) * inset / steps);
        bottom.setFillColor(sf::Color(0, 0, 0, a));
        target.draw(bottom);

        // Left
        sf::RectangleShape left(sf::Vector2f(inset / steps, h));
        left.setPosition(i * inset / steps, 0);
        left.setFillColor(sf::Color(0, 0, 0, a));
        target.draw(left);

        // Right
        sf::RectangleShape right(sf::Vector2f(inset / steps, h));
        right.setPosition(w - (i + 1) * inset / steps, 0);
        right.setFillColor(sf::Color(0, 0, 0, a));
        target.draw(right);
    }
}

void PostProcessing::applyChromaticAberration(sf::RenderTexture& target, float amount) {
    // This would ideally use a shader. Placeholder for future GLSL implementation.
    (void)target;
    (void)amount;
}

void PostProcessing::applyScanlines(sf::RenderTarget& target, float intensity) {
    float h = static_cast<float>(target.getSize().y);
    float w = static_cast<float>(target.getSize().x);
    sf::Uint8 alpha = static_cast<sf::Uint8>(intensity * 255);

    for (float y = 0; y < h; y += 4.0f) {
        sf::RectangleShape line(sf::Vector2f(w, 1.0f));
        line.setPosition(0, y);
        line.setFillColor(sf::Color(0, 0, 0, alpha));
        target.draw(line);
    }
}

} // namespace is::rendering
