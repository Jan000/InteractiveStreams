#pragma once

#include <SFML/Graphics.hpp>

namespace is::rendering {

/// Post-processing effects applied to the final rendered frame.
class PostProcessing {
public:
    PostProcessing() = default;

    /// Initialize with target dimensions.
    void initialize(unsigned int width, unsigned int height);

    /// Apply vignette effect.
    void applyVignette(sf::RenderTarget& target, float intensity = 0.4f);

    /// Apply chromatic aberration (subtle color fringing).
    void applyChromaticAberration(sf::RenderTexture& target, float amount = 1.0f);

    /// Apply scanline overlay for retro effect.
    void applyScanlines(sf::RenderTarget& target, float intensity = 0.05f);

private:
    unsigned int m_width = 1920;
    unsigned int m_height = 1080;
};

} // namespace is::rendering
