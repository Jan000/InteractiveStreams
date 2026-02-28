#include "games/chaos_arena/PhysicsWorld.h"
#include <algorithm>

namespace is::games::chaos_arena {

PhysicsWorld::PhysicsWorld(float gravityX, float gravityY) {
    b2Vec2 gravity(gravityX, gravityY);
    m_world = std::make_unique<b2World>(gravity);
    m_world->SetContactListener(this);
}

PhysicsWorld::~PhysicsWorld() = default;

void PhysicsWorld::step(float dt, int velocityIterations, int positionIterations) {
    m_world->Step(dt, velocityIterations, positionIterations);
}

b2Body* PhysicsWorld::createPlayerBody(float x, float y, float halfWidth, float halfHeight) {
    b2BodyDef bodyDef;
    bodyDef.type = b2_dynamicBody;
    bodyDef.position.Set(x, y);
    bodyDef.fixedRotation = true;  // Players don't rotate
    bodyDef.linearDamping = 0.5f;

    b2Body* body = m_world->CreateBody(&bodyDef);

    // Main body shape
    b2PolygonShape shape;
    shape.SetAsBox(halfWidth, halfHeight);

    b2FixtureDef fixtureDef;
    fixtureDef.shape = &shape;
    fixtureDef.density = 1.5f;
    fixtureDef.friction = 0.4f;
    fixtureDef.restitution = 0.05f;
    body->CreateFixture(&fixtureDef);

    // Foot sensor for ground detection
    b2PolygonShape footShape;
    footShape.SetAsBox(halfWidth * 0.8f, 0.1f, b2Vec2(0, halfHeight), 0);

    b2FixtureDef footDef;
    footDef.shape = &footShape;
    footDef.isSensor = true;
    footDef.userData.pointer = 1;  // Mark as foot sensor
    body->CreateFixture(&footDef);

    return body;
}

b2Body* PhysicsWorld::createProjectileBody(float x, float y, float radius, float vx, float vy) {
    b2BodyDef bodyDef;
    bodyDef.type = b2_dynamicBody;
    bodyDef.position.Set(x, y);
    bodyDef.bullet = true;  // CCD for fast projectiles
    bodyDef.gravityScale = 0.3f;

    b2Body* body = m_world->CreateBody(&bodyDef);

    b2CircleShape shape;
    shape.m_radius = radius;

    b2FixtureDef fixtureDef;
    fixtureDef.shape = &shape;
    fixtureDef.density = 0.5f;
    fixtureDef.friction = 0.1f;
    fixtureDef.restitution = 0.6f;
    fixtureDef.userData.pointer = 2;  // Mark as projectile
    body->CreateFixture(&fixtureDef);

    body->SetLinearVelocity(b2Vec2(vx, vy));

    return body;
}

void PhysicsWorld::destroyBody(b2Body* body) {
    if (body) {
        // Clean up ground contacts
        m_groundContacts.erase(
            std::remove_if(m_groundContacts.begin(), m_groundContacts.end(),
                [body](const auto& gc) { return gc.body == body; }),
            m_groundContacts.end());
        m_world->DestroyBody(body);
    }
}

bool PhysicsWorld::isOnGround(b2Body* body) const {
    for (const auto& gc : m_groundContacts) {
        if (gc.body == body && gc.count > 0) return true;
    }
    return false;
}

void PhysicsWorld::BeginContact(b2Contact* contact) {
    auto* fixtureA = contact->GetFixtureA();
    auto* fixtureB = contact->GetFixtureB();

    // Check if foot sensor is involved
    if (fixtureA->GetUserData().pointer == 1 && !fixtureB->IsSensor()) {
        addGroundContact(fixtureA->GetBody());
    }
    if (fixtureB->GetUserData().pointer == 1 && !fixtureA->IsSensor()) {
        addGroundContact(fixtureB->GetBody());
    }
}

void PhysicsWorld::EndContact(b2Contact* contact) {
    auto* fixtureA = contact->GetFixtureA();
    auto* fixtureB = contact->GetFixtureB();

    if (fixtureA->GetUserData().pointer == 1 && !fixtureB->IsSensor()) {
        removeGroundContact(fixtureA->GetBody());
    }
    if (fixtureB->GetUserData().pointer == 1 && !fixtureA->IsSensor()) {
        removeGroundContact(fixtureB->GetBody());
    }
}

void PhysicsWorld::PostSolve(b2Contact* contact, const b2ContactImpulse* impulse) {
    if (m_contactCallback && impulse->count > 0) {
        b2WorldManifold worldManifold;
        contact->GetWorldManifold(&worldManifold);

        float maxImpulse = 0;
        for (int i = 0; i < impulse->count; ++i) {
            maxImpulse = std::max(maxImpulse, impulse->normalImpulses[i]);
        }

        if (maxImpulse > 0.5f) {  // Threshold to avoid tiny contact events
            ContactInfo info;
            info.bodyA = contact->GetFixtureA()->GetBody();
            info.bodyB = contact->GetFixtureB()->GetBody();
            info.contactPoint = worldManifold.points[0];
            info.normal = worldManifold.normal;
            info.impulse = maxImpulse;
            m_contactCallback(info);
        }
    }
}

void PhysicsWorld::addGroundContact(b2Body* body) {
    for (auto& gc : m_groundContacts) {
        if (gc.body == body) {
            gc.count++;
            return;
        }
    }
    m_groundContacts.push_back({body, 1});
}

void PhysicsWorld::removeGroundContact(b2Body* body) {
    for (auto& gc : m_groundContacts) {
        if (gc.body == body) {
            gc.count = std::max(0, gc.count - 1);
            return;
        }
    }
}

} // namespace is::games::chaos_arena
