#pragma once

#include <SFML/Graphics.hpp>
#include <box2d/box2d.h>
#include <string>

namespace is::games::chaos_arena {

/// Types of power-ups that can spawn in the arena.
enum class PowerUpType {
    Health,       ///< Restores health
    SpeedBoost,   ///< Increases movement speed
    DamageBoost,  ///< Increases damage output
    Shield,       ///< Temporary invulnerability
    DoubleJump,   ///< Extra jump (resets jump count)
};

/// A power-up item in the arena.
struct PowerUp {
    b2Body*     body     = nullptr;
    PowerUpType type     = PowerUpType::Health;
    sf::Vector2f position;
    float       radius   = 0.5f;
    float       bobTimer = 0.0f;
    bool        alive    = true;
    float       glowTimer = 0.0f;

    sf::Color getColor() const {
        switch (type) {
            case PowerUpType::Health:      return sf::Color(50, 220, 50);
            case PowerUpType::SpeedBoost:  return sf::Color(50, 150, 255);
            case PowerUpType::DamageBoost: return sf::Color(255, 80, 50);
            case PowerUpType::Shield:      return sf::Color(255, 220, 50);
            case PowerUpType::DoubleJump:  return sf::Color(200, 100, 255);
            default: return sf::Color::White;
        }
    }

    std::string getName() const {
        switch (type) {
            case PowerUpType::Health:      return "Health";
            case PowerUpType::SpeedBoost:  return "Speed";
            case PowerUpType::DamageBoost: return "Damage";
            case PowerUpType::Shield:      return "Shield";
            case PowerUpType::DoubleJump:  return "Jump";
            default: return "Unknown";
        }
    }

    void update(float dt) {
        bobTimer += dt * 3.0f;
        glowTimer += dt * 2.0f;
    }
};

} // namespace is::games::chaos_arena
