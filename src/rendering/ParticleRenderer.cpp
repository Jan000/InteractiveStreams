#include "rendering/ParticleRenderer.h"

namespace is::rendering {

void ParticleRenderer::renderGlow(sf::RenderTarget& target, const sf::Texture& scene,
                                   float intensity) {
    // Simple additive glow approximation without shaders
    // (Shader-based bloom can be added later when GLSL shaders are set up)
    sf::Sprite sprite(scene);
    sprite.setColor(sf::Color(255, 255, 255, static_cast<sf::Uint8>(40 * intensity)));
    sprite.setScale(1.02f, 1.02f);
    sprite.setOrigin(scene.getSize().x * 0.01f, scene.getSize().y * 0.01f);
    target.draw(sprite, sf::BlendAdd);
}

} // namespace is::rendering
