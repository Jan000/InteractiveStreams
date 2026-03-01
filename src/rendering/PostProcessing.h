#pragma once

#include <SFML/Graphics.hpp>
#include <string>

namespace is::rendering {

/// GPU-accelerated post-processing effects applied to the final rendered frame.
/// Falls back to lightweight software implementations when shaders are
/// not available (e.g. inside a VM or on very old hardware).
class PostProcessing {
public:
    PostProcessing() = default;

    /// Initialize with target dimensions and load GLSL shaders.
    void initialize(unsigned int width, unsigned int height);

    /// Apply vignette effect (GPU or software fallback).
    void applyVignette(sf::RenderTexture& target, float intensity = 0.4f);

    /// Apply chromatic aberration (GPU only – no-op without shader support).
    void applyChromaticAberration(sf::RenderTexture& target, float amount = 2.0f);

    /// Apply bloom glow effect (GPU only).
    void applyBloom(sf::RenderTexture& target, float threshold = 0.7f, float intensity = 0.4f);

    /// Apply CRT retro effect (GPU only).
    void applyCRT(sf::RenderTexture& target, float scanlineIntensity = 0.15f, float curvature = 0.03f);

    /// Apply scanline overlay for retro effect (software fallback).
    void applyScanlines(sf::RenderTarget& target, float intensity = 0.05f);

    /// Whether GPU shaders are available on this system.
    bool shadersAvailable() const { return m_shadersAvailable; }

private:
    bool loadShader(sf::Shader& shader, const std::string& fragPath);
    void applyShaderPass(sf::RenderTexture& target, sf::Shader& shader);

    unsigned int m_width  = 1080;
    unsigned int m_height = 1920;

    bool       m_shadersAvailable = false;
    sf::Shader m_vignetteShader;
    sf::Shader m_chromaticShader;
    sf::Shader m_bloomShader;
    sf::Shader m_crtShader;

    bool m_vignetteLoaded  = false;
    bool m_chromaticLoaded = false;
    bool m_bloomLoaded     = false;
    bool m_crtLoaded       = false;

    /// Temporary texture used as intermediate for multi-pass effects.
    sf::RenderTexture m_tempTarget;
};

} // namespace is::rendering
