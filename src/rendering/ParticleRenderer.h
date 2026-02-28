#pragma once

#include <SFML/Graphics.hpp>

namespace is::rendering {

/// Renders the particle system (delegates to game's particle system).
/// Provides additional visual effects like bloom approximation.
class ParticleRenderer {
public:
    ParticleRenderer() = default;

    /// Render a glow/bloom effect on the target (post-process approximation).
    void renderGlow(sf::RenderTarget& target, const sf::Texture& scene, float intensity = 1.0f);

private:
    sf::Shader m_glowShader;
    bool m_shaderLoaded = false;
};

} // namespace is::rendering
