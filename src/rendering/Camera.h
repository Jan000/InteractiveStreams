#pragma once

#include <SFML/Graphics.hpp>

namespace is::rendering {

/// Camera system for smooth following, shake effects, and zoom.
class Camera {
public:
    Camera(float width, float height);

    /// Set camera target position.
    void setTarget(sf::Vector2f target);

    /// Update camera (smooth follow + shake decay).
    void update(float dt);

    /// Apply camera transform to a render target.
    void apply(sf::RenderTarget& target) const;

    /// Add screen shake.
    void shake(float intensity, float duration = 0.3f);

    /// Set zoom level (1.0 = default).
    void setZoom(float zoom);
    float getZoom() const { return m_zoom; }

    /// Get the current view.
    const sf::View& getView() const { return m_view; }

private:
    sf::View     m_view;
    sf::Vector2f m_target;
    sf::Vector2f m_position;
    float        m_zoom        = 1.0f;
    float        m_smoothSpeed = 5.0f;

    // Shake
    float m_shakeIntensity = 0.0f;
    float m_shakeTimer     = 0.0f;
    float m_shakeDuration  = 0.0f;
};

} // namespace is::rendering
