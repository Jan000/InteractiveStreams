#pragma once

#include <SFML/Graphics.hpp>
#include <box2d/box2d.h>
#include <string>

namespace is::games::chaos_arena {

/// Represents a player in the Chaos Arena.
struct Player {
    // Identity
    std::string userId;
    std::string displayName;
    sf::Color   color;

    // Physics body (owned by Box2D world)
    b2Body*     body = nullptr;

    // Game state
    float       health       = 100.0f;
    float       maxHealth    = 100.0f;
    float       damage       = 10.0f;
    float       knockbackMul = 1.0f;
    bool        alive        = true;
    bool        blocking     = false;
    int         kills        = 0;
    int         deaths       = 0;
    int         score        = 0;

    // Movement
    float       moveSpeed    = 8.0f;
    float       jumpForce    = 12.0f;
    bool        onGround     = false;
    int         jumpsLeft    = 2;  // double jump
    int         facingDir    = 1;  // 1 = right, -1 = left

    // Abilities
    float       attackCooldown  = 0.0f;
    float       specialCooldown = 0.0f;
    float       dashCooldown    = 0.0f;
    float       blockTimer      = 0.0f;
    float       invulnTimer     = 0.0f;

    // Visual
    float       animTimer       = 0.0f;
    float       hitFlashTimer   = 0.0f;
    float       trailTimer      = 0.0f;
    sf::Vector2f prevPosition;
    sf::Vector2f renderPosition;

    // Power-up effects
    float       speedBoostTimer = 0.0f;
    float       damageBoostTimer= 0.0f;
    float       shieldTimer     = 0.0f;

    /// Size (half-extents in meters)
    static constexpr float HALF_WIDTH  = 0.4f;
    static constexpr float HALF_HEIGHT = 0.7f;

    /// Cooldown durations
    static constexpr float ATTACK_CD  = 0.5f;
    static constexpr float SPECIAL_CD = 5.0f;
    static constexpr float DASH_CD    = 3.0f;
    static constexpr float BLOCK_DUR  = 1.5f;

    void updateTimers(float dt) {
        attackCooldown  = std::max(0.0f, attackCooldown  - dt);
        specialCooldown = std::max(0.0f, specialCooldown - dt);
        dashCooldown    = std::max(0.0f, dashCooldown    - dt);
        blockTimer      = std::max(0.0f, blockTimer      - dt);
        invulnTimer     = std::max(0.0f, invulnTimer     - dt);
        hitFlashTimer   = std::max(0.0f, hitFlashTimer   - dt);
        trailTimer      = std::max(0.0f, trailTimer      - dt);
        speedBoostTimer = std::max(0.0f, speedBoostTimer - dt);
        damageBoostTimer= std::max(0.0f, damageBoostTimer- dt);
        shieldTimer     = std::max(0.0f, shieldTimer     - dt);
        animTimer      += dt;

        blocking = blockTimer > 0.0f;

        // Reset temp boosts
        if (speedBoostTimer <= 0.0f) moveSpeed = 8.0f;
        if (damageBoostTimer <= 0.0f) damage = 10.0f;
    }

    float getEffectiveDamage() const {
        return damageBoostTimer > 0.0f ? damage * 1.5f : damage;
    }

    bool isInvulnerable() const {
        return invulnTimer > 0.0f || shieldTimer > 0.0f;
    }
};

} // namespace is::games::chaos_arena
