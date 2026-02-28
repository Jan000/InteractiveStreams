#pragma once

#include <box2d/box2d.h>
#include <memory>
#include <vector>
#include <functional>

namespace is::games::chaos_arena {

struct Player;

/// Contact callback information.
struct ContactInfo {
    b2Body* bodyA;
    b2Body* bodyB;
    b2Vec2  contactPoint;
    b2Vec2  normal;
    float   impulse;
};

/// Wraps Box2D world and manages physics simulation.
class PhysicsWorld : public b2ContactListener {
public:
    PhysicsWorld(float gravityX = 0.0f, float gravityY = 15.0f);
    ~PhysicsWorld();

    /// Step the physics simulation.
    void step(float dt, int velocityIterations = 8, int positionIterations = 3);

    /// Create a dynamic body for a player.
    b2Body* createPlayerBody(float x, float y, float halfWidth, float halfHeight);

    /// Create a projectile body.
    b2Body* createProjectileBody(float x, float y, float radius, float vx, float vy);

    /// Destroy a body.
    void destroyBody(b2Body* body);

    /// Get the underlying Box2D world.
    b2World& world() { return *m_world; }

    /// Set callback for contact events.
    using ContactCallback = std::function<void(const ContactInfo&)>;
    void setContactCallback(ContactCallback cb) { m_contactCallback = std::move(cb); }

    /// Check ground contact for a body.
    bool isOnGround(b2Body* body) const;

private:
    // b2ContactListener overrides
    void BeginContact(b2Contact* contact) override;
    void EndContact(b2Contact* contact) override;
    void PostSolve(b2Contact* contact, const b2ContactImpulse* impulse) override;

    std::unique_ptr<b2World> m_world;
    ContactCallback          m_contactCallback;

    // Track ground contacts per body
    struct GroundContactCount {
        b2Body* body;
        int     count;
    };
    std::vector<GroundContactCount> m_groundContacts;

    void addGroundContact(b2Body* body);
    void removeGroundContact(b2Body* body);
};

} // namespace is::games::chaos_arena
