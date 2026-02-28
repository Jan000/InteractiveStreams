#pragma once

#include <SFML/Graphics.hpp>
#include <box2d/box2d.h>
#include <string>

namespace is::games::chaos_arena {

/// A projectile fired by a player's special attack.
struct Projectile {
    b2Body*      body     = nullptr;
    std::string  ownerId;       // UserId of the player who fired it
    sf::Color    color;
    float        damage   = 20.0f;
    float        radius   = 0.2f;
    float        life     = 3.0f;    // Seconds before despawn
    bool         alive    = true;

    void update(float dt) {
        life -= dt;
        if (life <= 0.0f) alive = false;
    }
};

} // namespace is::games::chaos_arena
