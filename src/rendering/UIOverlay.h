#pragma once

#include <SFML/Graphics.hpp>
#include <string>
#include <vector>
#include <unordered_map>

namespace is::rendering {

/// UI overlay that renders HUD elements on top of the game.
/// Handles text rendering, info panels, watermarks, etc.
class UIOverlay {
public:
    UIOverlay();
    ~UIOverlay();

    /// Load a font for text rendering.
    bool loadFont(const std::string& path, const std::string& name = "default");

    /// Render the overlay on the target.
    void render(sf::RenderTarget& target);

    /// Set the stream title / watermark text.
    void setTitle(const std::string& title);

    /// Show a temporary notification.
    void showNotification(const std::string& text, float duration = 3.0f);

    /// Update notifications.
    void update(float dt);

private:
    std::unordered_map<std::string, sf::Font> m_fonts;
    std::string m_title = "InteractiveStreams";

    struct Notification {
        std::string text;
        float       timeLeft;
    };
    std::vector<Notification> m_notifications;
};

} // namespace is::rendering
