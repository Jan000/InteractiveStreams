#include "rendering/PostProcessing.h"
#include <spdlog/spdlog.h>
#include <cmath>

namespace is::rendering {

// ─── Initialisation ──────────────────────────────────────────────────────────

void PostProcessing::initialize(unsigned int width, unsigned int height) {
    m_width  = width;
    m_height = height;

    m_shadersAvailable = sf::Shader::isAvailable();
    if (!m_shadersAvailable) {
        spdlog::warn("[PostProcessing] Shaders not available – using software fallbacks.");
        return;
    }

    spdlog::info("[PostProcessing] GPU shaders available. Loading GLSL effects...");

    m_vignetteLoaded  = loadShader(m_vignetteShader,  "assets/shaders/vignette.frag");
    m_chromaticLoaded = loadShader(m_chromaticShader,  "assets/shaders/chromatic_aberration.frag");
    m_bloomLoaded     = loadShader(m_bloomShader,      "assets/shaders/bloom.frag");
    m_crtLoaded       = loadShader(m_crtShader,        "assets/shaders/crt.frag");

    // Create temp render texture for multi-pass effects
    if (!m_tempTarget.create(width, height)) {
        spdlog::warn("[PostProcessing] Failed to create temp RenderTexture for shader passes.");
    }

    int loaded = (int)m_vignetteLoaded + (int)m_chromaticLoaded +
                 (int)m_bloomLoaded + (int)m_crtLoaded;
    spdlog::info("[PostProcessing] {}/4 shaders loaded successfully.", loaded);
}

bool PostProcessing::loadShader(sf::Shader& shader, const std::string& fragPath) {
    if (!shader.loadFromFile(fragPath, sf::Shader::Fragment)) {
        spdlog::warn("[PostProcessing] Failed to load shader: {}", fragPath);
        return false;
    }
    spdlog::info("[PostProcessing] Loaded shader: {}", fragPath);
    return true;
}

// ─── Generic shader pass ─────────────────────────────────────────────────────

void PostProcessing::applyShaderPass(sf::RenderTexture& target, sf::Shader& shader) {
    // Read current contents into a sprite, draw through the shader back.
    target.display();
    sf::Sprite sprite(target.getTexture());
    sf::RenderStates states;
    states.shader = &shader;

    // Draw to the temp target, then blit back
    m_tempTarget.clear(sf::Color::Transparent);
    m_tempTarget.draw(sprite, states);
    m_tempTarget.display();

    // Copy result back
    sf::Sprite result(m_tempTarget.getTexture());
    target.draw(result);
}

// ─── Vignette ────────────────────────────────────────────────────────────────

void PostProcessing::applyVignette(sf::RenderTexture& target, float intensity) {
    if (m_shadersAvailable && m_vignetteLoaded) {
        m_vignetteShader.setUniform("texture", sf::Shader::CurrentTexture);
        m_vignetteShader.setUniform("intensity", intensity);
        applyShaderPass(target, m_vignetteShader);
        return;
    }

    // Software fallback – simplified gradient rectangles at edges
    float w = static_cast<float>(target.getSize().x);
    float h = static_cast<float>(target.getSize().y);

    int steps = 20;
    for (int i = 0; i < steps; ++i) {
        float t = static_cast<float>(i) / steps;
        float alpha = intensity * t * t * 80;
        sf::Uint8 a = static_cast<sf::Uint8>(std::min(255.0f, alpha));
        float inset = t * w * 0.15f;

        sf::RectangleShape top(sf::Vector2f(w, inset / steps));
        top.setPosition(0, i * inset / steps);
        top.setFillColor(sf::Color(0, 0, 0, a));
        target.draw(top);

        sf::RectangleShape bottom(sf::Vector2f(w, inset / steps));
        bottom.setPosition(0, h - (i + 1) * inset / steps);
        bottom.setFillColor(sf::Color(0, 0, 0, a));
        target.draw(bottom);

        sf::RectangleShape left(sf::Vector2f(inset / steps, h));
        left.setPosition(i * inset / steps, 0);
        left.setFillColor(sf::Color(0, 0, 0, a));
        target.draw(left);

        sf::RectangleShape right(sf::Vector2f(inset / steps, h));
        right.setPosition(w - (i + 1) * inset / steps, 0);
        right.setFillColor(sf::Color(0, 0, 0, a));
        target.draw(right);
    }
}

// ─── Chromatic Aberration ────────────────────────────────────────────────────

void PostProcessing::applyChromaticAberration(sf::RenderTexture& target, float amount) {
    if (!m_shadersAvailable || !m_chromaticLoaded) return;

    m_chromaticShader.setUniform("texture", sf::Shader::CurrentTexture);
    m_chromaticShader.setUniform("amount", amount);
    m_chromaticShader.setUniform("resolution",
        sf::Glsl::Vec2(static_cast<float>(m_width), static_cast<float>(m_height)));
    applyShaderPass(target, m_chromaticShader);
}

// ─── Bloom ───────────────────────────────────────────────────────────────────

void PostProcessing::applyBloom(sf::RenderTexture& target, float threshold, float intensity) {
    if (!m_shadersAvailable || !m_bloomLoaded) return;

    m_bloomShader.setUniform("texture", sf::Shader::CurrentTexture);
    m_bloomShader.setUniform("threshold", threshold);
    m_bloomShader.setUniform("intensity", intensity);
    m_bloomShader.setUniform("resolution",
        sf::Glsl::Vec2(static_cast<float>(m_width), static_cast<float>(m_height)));
    applyShaderPass(target, m_bloomShader);
}

// ─── CRT ─────────────────────────────────────────────────────────────────────

void PostProcessing::applyCRT(sf::RenderTexture& target, float scanlineIntensity, float curvature) {
    if (!m_shadersAvailable || !m_crtLoaded) return;

    m_crtShader.setUniform("texture", sf::Shader::CurrentTexture);
    m_crtShader.setUniform("scanlineIntensity", scanlineIntensity);
    m_crtShader.setUniform("curvature", curvature);
    m_crtShader.setUniform("resolution",
        sf::Glsl::Vec2(static_cast<float>(m_width), static_cast<float>(m_height)));
    applyShaderPass(target, m_crtShader);
}

// ─── Scanlines (software) ───────────────────────────────────────────────────

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
