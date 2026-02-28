#include "rendering/Camera.h"
#include <cmath>
#include <random>

namespace is::rendering {

Camera::Camera(float width, float height) {
    m_view.setSize(width, height);
    m_view.setCenter(0, 0);
    m_position = {0, 0};
    m_target = {0, 0};
}

void Camera::setTarget(sf::Vector2f target) {
    m_target = target;
}

void Camera::update(float dt) {
    // Smooth follow
    m_position.x += (m_target.x - m_position.x) * m_smoothSpeed * dt;
    m_position.y += (m_target.y - m_position.y) * m_smoothSpeed * dt;

    sf::Vector2f finalPos = m_position;

    // Screen shake
    if (m_shakeTimer > 0) {
        m_shakeTimer -= dt;
        float intensity = m_shakeIntensity * (m_shakeTimer / m_shakeDuration);

        static std::mt19937 rng(std::random_device{}());
        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

        finalPos.x += dist(rng) * intensity;
        finalPos.y += dist(rng) * intensity;
    }

    m_view.setCenter(finalPos);
    m_view.setSize(m_view.getSize().x / m_zoom * m_zoom, m_view.getSize().y / m_zoom * m_zoom);
}

void Camera::apply(sf::RenderTarget& target) const {
    target.setView(m_view);
}

void Camera::shake(float intensity, float duration) {
    m_shakeIntensity = intensity;
    m_shakeTimer = duration;
    m_shakeDuration = duration;
}

void Camera::setZoom(float zoom) {
    m_zoom = std::max(0.1f, std::min(10.0f, zoom));
}

} // namespace is::rendering
