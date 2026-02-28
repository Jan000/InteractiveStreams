// ═══════════════════════════════════════════════════════════════════════════
// Tests: Player – timers, cooldowns, invulnerability, damage calculation
// ═══════════════════════════════════════════════════════════════════════════
#include <doctest/doctest.h>
#include "games/chaos_arena/Player.h"

using Player = is::games::chaos_arena::Player;

TEST_SUITE("Player") {

    TEST_CASE("Default state") {
        Player p;
        CHECK(p.health == 100.0f);
        CHECK(p.maxHealth == 100.0f);
        CHECK(p.damage == 10.0f);
        CHECK(p.alive == true);
        CHECK(p.blocking == false);
        CHECK(p.kills == 0);
        CHECK(p.deaths == 0);
        CHECK(p.score == 0);
        CHECK(p.moveSpeed == 8.0f);
        CHECK(p.jumpForce == 12.0f);
        CHECK(p.jumpsLeft == 2);
        CHECK(p.facingDir == 1);
    }

    TEST_CASE("updateTimers decreases cooldowns") {
        Player p;
        p.attackCooldown  = 1.0f;
        p.specialCooldown = 3.0f;
        p.dashCooldown    = 2.0f;
        p.invulnTimer     = 1.5f;

        p.updateTimers(1.0f);

        CHECK(p.attackCooldown == doctest::Approx(0.0f));
        CHECK(p.specialCooldown == doctest::Approx(2.0f));
        CHECK(p.dashCooldown == doctest::Approx(1.0f));
        CHECK(p.invulnTimer == doctest::Approx(0.5f));
    }

    TEST_CASE("Timers clamp to zero (never negative)") {
        Player p;
        p.attackCooldown  = 0.1f;
        p.specialCooldown = 0.1f;

        p.updateTimers(10.0f);  // Large dt

        CHECK(p.attackCooldown == 0.0f);
        CHECK(p.specialCooldown == 0.0f);
        CHECK(p.dashCooldown == 0.0f);
        CHECK(p.invulnTimer == 0.0f);
        CHECK(p.hitFlashTimer == 0.0f);
    }

    TEST_CASE("Blocking state is linked to blockTimer") {
        Player p;
        CHECK(p.blocking == false);

        p.blockTimer = 1.0f;
        p.updateTimers(0.0f);  // dt=0 just recalculates blocking
        CHECK(p.blocking == true);

        p.updateTimers(1.0f);  // blockTimer → 0
        CHECK(p.blocking == false);
    }

    TEST_CASE("isInvulnerable reflects invulnTimer and shieldTimer") {
        Player p;
        CHECK(p.isInvulnerable() == false);

        p.invulnTimer = 1.0f;
        CHECK(p.isInvulnerable() == true);

        p.invulnTimer = 0.0f;
        p.shieldTimer = 0.5f;
        CHECK(p.isInvulnerable() == true);

        p.shieldTimer = 0.0f;
        CHECK(p.isInvulnerable() == false);
    }

    TEST_CASE("getEffectiveDamage with and without boost") {
        Player p;
        p.damage = 10.0f;
        CHECK(p.getEffectiveDamage() == doctest::Approx(10.0f));

        p.damageBoostTimer = 5.0f;
        CHECK(p.getEffectiveDamage() == doctest::Approx(15.0f));  // 10 * 1.5

        p.damage = 20.0f;
        CHECK(p.getEffectiveDamage() == doctest::Approx(30.0f));  // 20 * 1.5
    }

    TEST_CASE("Speed boost resets after timer expires") {
        Player p;
        p.moveSpeed = 12.0f;  // Boosted
        p.speedBoostTimer = 0.5f;

        p.updateTimers(0.3f);
        CHECK(p.moveSpeed == 12.0f);  // Still boosted

        p.updateTimers(0.3f);  // Timer now <= 0
        CHECK(p.moveSpeed == 8.0f);   // Reset to default
    }

    TEST_CASE("Damage boost resets after timer expires") {
        Player p;
        p.damage = 20.0f;  // Boosted
        p.damageBoostTimer = 0.5f;

        p.updateTimers(0.3f);
        CHECK(p.damage == 20.0f);  // Still boosted

        p.updateTimers(0.3f);  // Timer now <= 0
        CHECK(p.damage == 10.0f);  // Reset to default
    }

    TEST_CASE("animTimer increases over time") {
        Player p;
        float initial = p.animTimer;
        p.updateTimers(1.0f);
        CHECK(p.animTimer == doctest::Approx(initial + 1.0f));
    }

    TEST_CASE("Static constants are correct") {
        CHECK(Player::HALF_WIDTH == doctest::Approx(0.4f));
        CHECK(Player::HALF_HEIGHT == doctest::Approx(0.7f));
        CHECK(Player::ATTACK_CD == doctest::Approx(0.5f));
        CHECK(Player::SPECIAL_CD == doctest::Approx(5.0f));
        CHECK(Player::DASH_CD == doctest::Approx(3.0f));
        CHECK(Player::BLOCK_DUR == doctest::Approx(1.5f));
    }
}
