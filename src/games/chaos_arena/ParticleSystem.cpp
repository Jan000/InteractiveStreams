#include "games/chaos_arena/ParticleSystem.h"
#include <cmath>
#include <algorithm>

namespace is::games::chaos_arena {

static constexpr float DEG_TO_RAD = 3.14159265f / 180.0f;

ParticleSystem::ParticleSystem(size_t maxParticles)
    : m_maxParticles(maxParticles)
    , m_rng(std::random_device{}())
{
    m_particles.reserve(maxParticles);
}

float ParticleSystem::randomFloat(float min, float max) {
    std::uniform_real_distribution<float> dist(min, max);
    return dist(m_rng);
}

void ParticleSystem::emit(const ParticleEmitterConfig& config) {
    for (int i = 0; i < config.count; ++i) {
        if (m_particles.size() >= m_maxParticles) break;

        Particle p;
        p.position = config.position;

        // Random direction within spread
        float angle = (config.angle + randomFloat(-config.spread / 2, config.spread / 2)) * DEG_TO_RAD;
        float speed = config.speed + randomFloat(-config.speedVar, config.speedVar);
        p.velocity.x = std::cos(angle) * speed;
        p.velocity.y = std::sin(angle) * speed;

        p.life = config.life + randomFloat(-config.lifeVar, config.lifeVar);
        p.maxLife = p.life;
        p.size = config.size + randomFloat(-config.sizeVar, config.sizeVar);
        p.color = config.color;
        p.rotation = randomFloat(0, 360);
        p.rotSpeed = randomFloat(-360, 360);
        p.drag = config.drag;
        p.gravity = config.gravity;
        p.fadeAlpha = config.fadeAlpha;
        p.shrink = config.shrink;

        m_particles.push_back(p);
    }
}

void ParticleSystem::emitExplosion(sf::Vector2f pos, sf::Color color, int count) {
    ParticleEmitterConfig cfg;
    cfg.position = pos;
    cfg.count = count;
    cfg.speed = 200.0f;
    cfg.speedVar = 100.0f;
    cfg.spread = 360.0f;
    cfg.life = 0.8f;
    cfg.lifeVar = 0.3f;
    cfg.size = 6.0f;
    cfg.sizeVar = 3.0f;
    cfg.color = color;
    cfg.gravity = 80.0f;
    cfg.drag = 0.96f;
    emit(cfg);

    // Inner bright flash
    cfg.count = count / 3;
    cfg.speed = 80.0f;
    cfg.speedVar = 40.0f;
    cfg.life = 0.3f;
    cfg.size = 10.0f;
    cfg.color = sf::Color(255, 255, 200);
    cfg.gravity = 0.0f;
    emit(cfg);
}

void ParticleSystem::emitHitSpark(sf::Vector2f pos, sf::Vector2f dir, sf::Color color) {
    ParticleEmitterConfig cfg;
    cfg.position = pos;
    cfg.count = 15;
    cfg.speed = 150.0f;
    cfg.speedVar = 80.0f;
    float angle = std::atan2(dir.y, dir.x) / DEG_TO_RAD;
    cfg.angle = angle;
    cfg.spread = 60.0f;
    cfg.life = 0.4f;
    cfg.lifeVar = 0.2f;
    cfg.size = 3.0f;
    cfg.sizeVar = 2.0f;
    cfg.color = color;
    cfg.gravity = 40.0f;
    emit(cfg);
}

void ParticleSystem::emitDust(sf::Vector2f pos) {
    ParticleEmitterConfig cfg;
    cfg.position = pos;
    cfg.count = 8;
    cfg.speed = 30.0f;
    cfg.speedVar = 20.0f;
    cfg.angle = -90.0f;  // Upward
    cfg.spread = 120.0f;
    cfg.life = 0.5f;
    cfg.lifeVar = 0.2f;
    cfg.size = 5.0f;
    cfg.sizeVar = 3.0f;
    cfg.color = sf::Color(180, 170, 150, 200);
    cfg.gravity = -10.0f;
    cfg.drag = 0.94f;
    emit(cfg);
}

void ParticleSystem::emitTrail(sf::Vector2f pos, sf::Color color) {
    ParticleEmitterConfig cfg;
    cfg.position = pos;
    cfg.count = 2;
    cfg.speed = 10.0f;
    cfg.speedVar = 5.0f;
    cfg.spread = 360.0f;
    cfg.life = 0.3f;
    cfg.lifeVar = 0.1f;
    cfg.size = 4.0f;
    cfg.sizeVar = 2.0f;
    cfg.color = color;
    cfg.color.a = 150;
    cfg.gravity = 0.0f;
    cfg.drag = 0.9f;
    emit(cfg);
}

void ParticleSystem::emitPowerUpPickup(sf::Vector2f pos, sf::Color color) {
    ParticleEmitterConfig cfg;
    cfg.position = pos;
    cfg.count = 30;
    cfg.speed = 120.0f;
    cfg.speedVar = 60.0f;
    cfg.spread = 360.0f;
    cfg.life = 0.6f;
    cfg.lifeVar = 0.2f;
    cfg.size = 5.0f;
    cfg.sizeVar = 3.0f;
    cfg.color = color;
    cfg.gravity = -30.0f;
    emit(cfg);
}

void ParticleSystem::emitDeath(sf::Vector2f pos, sf::Color color) {
    emitExplosion(pos, color, 60);

    // Extra ring of particles
    ParticleEmitterConfig cfg;
    cfg.position = pos;
    cfg.count = 20;
    cfg.speed = 300.0f;
    cfg.speedVar = 50.0f;
    cfg.spread = 360.0f;
    cfg.life = 0.5f;
    cfg.size = 3.0f;
    cfg.color = sf::Color::White;
    cfg.gravity = 0.0f;
    cfg.drag = 0.92f;
    emit(cfg);
}

void ParticleSystem::update(float dt) {
    for (auto& p : m_particles) {
        p.velocity.x *= p.drag;
        p.velocity.y *= p.drag;
        p.velocity.y += p.gravity * dt;

        p.position += p.velocity * dt;
        p.rotation += p.rotSpeed * dt;
        p.life -= dt;

        // Visual updates
        float lifeRatio = std::max(0.0f, p.life / p.maxLife);
        if (p.fadeAlpha) {
            p.color.a = static_cast<sf::Uint8>(255 * lifeRatio);
        }
    }

    // Remove dead particles
    m_particles.erase(
        std::remove_if(m_particles.begin(), m_particles.end(),
            [](const Particle& p) { return p.life <= 0.0f; }),
        m_particles.end());
}

void ParticleSystem::render(sf::RenderTarget& target) {
    sf::RectangleShape rect;
    for (const auto& p : m_particles) {
        float lifeRatio = std::max(0.0f, p.life / p.maxLife);
        float size = p.shrink ? p.size * lifeRatio : p.size;
        if (size < 0.5f) continue;

        rect.setSize(sf::Vector2f(size, size));
        rect.setOrigin(size / 2, size / 2);
        rect.setPosition(p.position);
        rect.setRotation(p.rotation);
        rect.setFillColor(p.color);
        target.draw(rect);
    }
}

} // namespace is::games::chaos_arena
