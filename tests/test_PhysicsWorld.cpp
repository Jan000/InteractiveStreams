// ═══════════════════════════════════════════════════════════════════════════
// Tests: PhysicsWorld – creation, body management, gravity, ground detection
// ═══════════════════════════════════════════════════════════════════════════
#include <doctest/doctest.h>
#include "games/chaos_arena/PhysicsWorld.h"

using PhysicsWorld = is::games::chaos_arena::PhysicsWorld;

TEST_SUITE("PhysicsWorld") {

    TEST_CASE("World creation with default gravity") {
        PhysicsWorld pw;
        auto gravity = pw.world().GetGravity();
        CHECK(gravity.x == doctest::Approx(0.0f));
        CHECK(gravity.y == doctest::Approx(15.0f));
    }

    TEST_CASE("World creation with custom gravity") {
        PhysicsWorld pw(0.0f, -9.81f);
        auto gravity = pw.world().GetGravity();
        CHECK(gravity.x == doctest::Approx(0.0f));
        CHECK(gravity.y == doctest::Approx(-9.81f));
    }

    TEST_CASE("Create player body") {
        PhysicsWorld pw;
        b2Body* body = pw.createPlayerBody(0.0f, 0.0f, 0.4f, 0.7f);

        REQUIRE(body != nullptr);
        CHECK(body->GetType() == b2_dynamicBody);
        CHECK(body->IsFixedRotation() == true);

        // Body should have two fixtures: main shape + foot sensor
        int fixtureCount = 0;
        for (auto* f = body->GetFixtureList(); f; f = f->GetNext()) {
            fixtureCount++;
        }
        CHECK(fixtureCount == 2);

        pw.destroyBody(body);
    }

    TEST_CASE("Create projectile body") {
        PhysicsWorld pw;
        b2Body* body = pw.createProjectileBody(5.0f, 3.0f, 0.2f, 10.0f, 0.0f);

        REQUIRE(body != nullptr);
        CHECK(body->GetType() == b2_dynamicBody);
        CHECK(body->IsBullet() == true);  // CCD enabled

        auto vel = body->GetLinearVelocity();
        CHECK(vel.x == doctest::Approx(10.0f));
        CHECK(vel.y == doctest::Approx(0.0f));

        pw.destroyBody(body);
    }

    TEST_CASE("Destroy body removes it from world") {
        PhysicsWorld pw;
        b2Body* body = pw.createPlayerBody(0, 0, 0.4f, 0.7f);
        int countBefore = pw.world().GetBodyCount();

        pw.destroyBody(body);
        int countAfter = pw.world().GetBodyCount();

        CHECK(countAfter == countBefore - 1);
    }

    TEST_CASE("Destroy nullptr is safe") {
        PhysicsWorld pw;
        pw.destroyBody(nullptr);  // Should not crash
    }

    TEST_CASE("isOnGround returns false for airborne body") {
        PhysicsWorld pw;
        b2Body* body = pw.createPlayerBody(0.0f, -10.0f, 0.4f, 0.7f);
        // No ground contact yet
        CHECK(pw.isOnGround(body) == false);
        pw.destroyBody(body);
    }

    TEST_CASE("Physics step does not crash") {
        PhysicsWorld pw;
        b2Body* p1 = pw.createPlayerBody(-2, 0, 0.4f, 0.7f);
        b2Body* p2 = pw.createPlayerBody(2, 0, 0.4f, 0.7f);

        // Run several steps
        for (int i = 0; i < 60; i++) {
            pw.step(1.0f / 60.0f);
        }

        CHECK(p1->GetPosition().y > 0.0f);  // Should have fallen due to gravity

        pw.destroyBody(p1);
        pw.destroyBody(p2);
    }

    TEST_CASE("Contact callback is callable") {
        PhysicsWorld pw;
        bool called = false;
        pw.setContactCallback([&called](const is::games::chaos_arena::ContactInfo&) {
            called = true;
        });
        // Just verify it can be set without crash
        CHECK(called == false);
    }

    TEST_CASE("Player body position is set correctly") {
        PhysicsWorld pw;
        b2Body* body = pw.createPlayerBody(5.5f, -3.2f, 0.4f, 0.7f);

        auto pos = body->GetPosition();
        CHECK(pos.x == doctest::Approx(5.5f));
        CHECK(pos.y == doctest::Approx(-3.2f));

        pw.destroyBody(body);
    }

    TEST_CASE("Multiple bodies can coexist") {
        PhysicsWorld pw;
        std::vector<b2Body*> bodies;
        for (int i = 0; i < 20; i++) {
            bodies.push_back(pw.createPlayerBody(
                static_cast<float>(i) * 2.0f, 0.0f, 0.4f, 0.7f));
        }

        CHECK(pw.world().GetBodyCount() == 20);

        for (auto* b : bodies) {
            pw.destroyBody(b);
        }

        CHECK(pw.world().GetBodyCount() == 0);
    }

    TEST_CASE("Ground detection with static platform") {
        PhysicsWorld pw;

        // Create a static ground platform
        b2BodyDef groundDef;
        groundDef.type = b2_staticBody;
        groundDef.position.Set(0.0f, 5.0f);
        b2Body* ground = pw.world().CreateBody(&groundDef);

        b2PolygonShape groundShape;
        groundShape.SetAsBox(20.0f, 0.5f);
        ground->CreateFixture(&groundShape, 0.0f);

        // Create player above the ground
        b2Body* player = pw.createPlayerBody(0.0f, 3.0f, 0.4f, 0.7f);

        // Step physics enough for player to land
        for (int i = 0; i < 120; i++) {
            pw.step(1.0f / 60.0f);
        }

        // Player should now be on ground
        CHECK(pw.isOnGround(player) == true);

        pw.destroyBody(player);
        pw.world().DestroyBody(ground);
    }
}
