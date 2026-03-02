#pragma once

#include <SFML/Graphics.hpp>
#include <string>
#include <cmath>

namespace is::rendering {

/// Animation state for a player character.
enum class AnimState {
    Idle,
    Walk,
    Jump,
    Attack,
    Dash,
    Block,
    Hit,
    Death
};

/// Procedural pixel-art sprite animator.
/// Draws an animated character using simple SFML shapes – no external
/// sprite-sheets required.  Each frame is generated on-the-fly based
/// on the current AnimState and a timer value.
///
/// The character design:
///   - Head (rounded rectangle/circle)
///   - Body (rectangle with slight taper)
///   - Arms (two thin rectangles, animated per state)
///   - Legs (two thin rectangles, animated per state)
///   - Accessories: sword during attack, shield during block, speed lines during dash
class SpriteAnimator {
public:
    /// Draw a character at the given position.
    /// @param target     Render target
    /// @param position   Center of character (screen coords)
    /// @param width      Character width in pixels
    /// @param height     Character height in pixels
    /// @param color      Base character color
    /// @param state      Current animation state
    /// @param timer      Continuous timer for animation (seconds)
    /// @param facingDir  1 = right, -1 = left
    /// @param alpha      Opacity multiplier (0-255)
    static void draw(sf::RenderTarget& target,
                     sf::Vector2f position,
                     float width, float height,
                     sf::Color color,
                     AnimState state,
                     float timer,
                     int facingDir = 1,
                     sf::Uint8 alpha = 255);

private:
    /// Draw body parts
    static void drawHead(sf::RenderTarget& target, sf::Vector2f pos,
                         float w, float h, sf::Color color, AnimState state,
                         float timer, int facingDir, sf::Uint8 alpha);
    static void drawBody(sf::RenderTarget& target, sf::Vector2f pos,
                         float w, float h, sf::Color color, AnimState state,
                         float timer, sf::Uint8 alpha);
    static void drawArms(sf::RenderTarget& target, sf::Vector2f pos,
                         float w, float h, sf::Color color, AnimState state,
                         float timer, int facingDir, sf::Uint8 alpha);
    static void drawLegs(sf::RenderTarget& target, sf::Vector2f pos,
                         float w, float h, sf::Color color, AnimState state,
                         float timer, sf::Uint8 alpha);
    static void drawAccessories(sf::RenderTarget& target, sf::Vector2f pos,
                                float w, float h, sf::Color color, AnimState state,
                                float timer, int facingDir, sf::Uint8 alpha);

    /// Helper: darken a color
    static sf::Color darken(sf::Color c, float factor) {
        return sf::Color(
            static_cast<sf::Uint8>(c.r * factor),
            static_cast<sf::Uint8>(c.g * factor),
            static_cast<sf::Uint8>(c.b * factor),
            c.a);
    }

    /// Helper: lighten a color
    static sf::Color lighten(sf::Color c, float factor) {
        return sf::Color(
            static_cast<sf::Uint8>(std::min(255.0f, c.r + (255 - c.r) * factor)),
            static_cast<sf::Uint8>(std::min(255.0f, c.g + (255 - c.g) * factor)),
            static_cast<sf::Uint8>(std::min(255.0f, c.b + (255 - c.b) * factor)),
            c.a);
    }
};

} // namespace is::rendering
