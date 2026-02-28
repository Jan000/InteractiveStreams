#pragma once

#include <SFML/Graphics.hpp>
#include <vector>
#include <random>

namespace is::games::chaos_arena {

/// A single particle.
struct Particle {
    sf::Vector2f position;
    sf::Vector2f velocity;
    sf::Color    color;
    float        life        = 1.0f;
    float        maxLife     = 1.0f;
    float        size        = 4.0f;
    float        rotation    = 0.0f;
    float        rotSpeed    = 0.0f;
    float        drag        = 0.98f;
    float        gravity     = 50.0f;
    bool         fadeAlpha   = true;
    bool         shrink      = true;
};

/// Particle emitter configuration.
struct ParticleEmitterConfig {
    sf::Vector2f position;
    int          count       = 20;
    float        speed       = 100.0f;
    float        speedVar    = 50.0f;
    float        angle       = 0.0f;       // Direction in degrees
    float        spread      = 360.0f;     // Spread in degrees
    float        life        = 1.0f;
    float        lifeVar     = 0.3f;
    float        size        = 4.0f;
    float        sizeVar     = 2.0f;
    sf::Color    color       = sf::Color::White;
    sf::Color    colorEnd    = sf::Color(255, 255, 255, 0);
    float        gravity     = 50.0f;
    float        drag        = 0.98f;
    bool         fadeAlpha   = true;
    bool         shrink      = true;
};

/// Manages and renders particles for visual effects.
class ParticleSystem {
public:
    ParticleSystem(size_t maxParticles = 10000);

    /// Emit particles from a config.
    void emit(const ParticleEmitterConfig& config);

    /// Convenience emitters for common effects.
    void emitExplosion(sf::Vector2f pos, sf::Color color, int count = 40);
    void emitHitSpark(sf::Vector2f pos, sf::Vector2f dir, sf::Color color);
    void emitDust(sf::Vector2f pos);
    void emitTrail(sf::Vector2f pos, sf::Color color);
    void emitPowerUpPickup(sf::Vector2f pos, sf::Color color);
    void emitDeath(sf::Vector2f pos, sf::Color color);

    /// Update all particles.
    void update(float dt);

    /// Render all particles.
    void render(sf::RenderTarget& target);

    /// Get active particle count.
    size_t activeCount() const { return m_particles.size(); }

    /// Clear all particles.
    void clear() { m_particles.clear(); }

private:
    std::vector<Particle> m_particles;
    size_t                m_maxParticles;
    std::mt19937          m_rng;

    float randomFloat(float min, float max);
};

} // namespace is::games::chaos_arena
