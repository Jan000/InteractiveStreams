#include "rendering/UIOverlay.h"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace is::rendering {

UIOverlay::UIOverlay() = default;
UIOverlay::~UIOverlay() = default;

bool UIOverlay::loadFont(const std::string& path, const std::string& name) {
    sf::Font font;
    if (font.loadFromFile(path)) {
        m_fonts[name] = std::move(font);
        spdlog::info("Font '{}' loaded from: {}", name, path);
        return true;
    }
    spdlog::warn("Failed to load font from: {}", path);
    return false;
}

void UIOverlay::setTitle(const std::string& title) {
    m_title = title;
}

void UIOverlay::showNotification(const std::string& text, float duration) {
    m_notifications.push_back({text, duration});
}

void UIOverlay::update(float dt) {
    for (auto& notif : m_notifications) {
        notif.timeLeft -= dt;
    }
    m_notifications.erase(
        std::remove_if(m_notifications.begin(), m_notifications.end(),
            [](const auto& n) { return n.timeLeft <= 0; }),
        m_notifications.end());
}

void UIOverlay::render(sf::RenderTarget& target) {
    auto size = target.getSize();

    // Watermark / title (top-right)
    if (m_fonts.count("default")) {
        sf::Text titleText;
        titleText.setFont(m_fonts["default"]);
        titleText.setString(m_title);
        titleText.setCharacterSize(16);
        titleText.setFillColor(sf::Color(255, 255, 255, 100));
        auto bounds = titleText.getLocalBounds();
        titleText.setPosition(size.x - bounds.width - 20, 10);
        target.draw(titleText);
    }

    // Notifications (center-bottom)
    float notifY = size.y - 80.0f;
    for (const auto& notif : m_notifications) {
        float alpha = std::min(1.0f, notif.timeLeft);

        sf::RectangleShape bg(sf::Vector2f(400, 36));
        bg.setOrigin(200, 18);
        bg.setPosition(size.x / 2.0f, notifY);
        bg.setFillColor(sf::Color(0, 0, 0, static_cast<sf::Uint8>(180 * alpha)));
        bg.setOutlineThickness(1.0f);
        bg.setOutlineColor(sf::Color(100, 200, 255, static_cast<sf::Uint8>(150 * alpha)));
        target.draw(bg);

        if (m_fonts.count("default")) {
            sf::Text text;
            text.setFont(m_fonts["default"]);
            text.setString(notif.text);
            text.setCharacterSize(14);
            text.setFillColor(sf::Color(255, 255, 255, static_cast<sf::Uint8>(255 * alpha)));
            auto bounds = text.getLocalBounds();
            text.setPosition(size.x / 2.0f - bounds.width / 2, notifY - 8);
            target.draw(text);
        }

        notifY -= 42;
    }
}

} // namespace is::rendering
