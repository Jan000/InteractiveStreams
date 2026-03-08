#pragma once

#include "games/IGame.h"
#include "games/chaos_arena/Arena.h"
#include "games/chaos_arena/Player.h"
#include "games/chaos_arena/PhysicsWorld.h"
#include "games/chaos_arena/ParticleSystem.h"
#include "games/chaos_arena/Projectile.h"
#include "games/chaos_arena/PowerUp.h"
#include "rendering/Background.h"
#include "rendering/PostProcessing.h"
#include "rendering/SpriteAnimator.h"

#include <SFML/Graphics/Font.hpp>
#include <vector>
#include <unordered_map>
#include <deque>
#include <random>

namespace is::games::chaos_arena {

/// Game state phases.
enum class GamePhase {
    Lobby,       ///< Waiting for players to join
    Countdown,   ///< Pre-round countdown
    Battle,      ///< Active combat
    RoundEnd,    ///< Showing round results
    GameOver     ///< Final scoreboard
};

/// A kill-feed entry shown on screen.
struct KillFeedEntry {
    std::string killer;
    std::string victim;
    std::string method;
    double      timeRemaining;
};

/// The main Chaos Arena game.
/// Players join via !join, control their character with chat commands,
/// and battle in a physics-based arena. Last one standing wins!
class ChaosArena : public IGame {
public:
    ChaosArena();
    ~ChaosArena() override;

    // IGame interface
    std::string id() const override { return "chaos_arena"; }
    std::string displayName() const override { return "Chaos Arena"; }
    std::string description() const override {
        return "Physics-based battle royale! Join with !join, fight with chat commands. Last one standing wins!";
    }

    void initialize() override;
    void shutdown() override;
    void onChatMessage(const platform::ChatMessage& msg) override;
    void update(double dt) override;
    void render(sf::RenderTarget& target, double alpha) override;
    bool isRoundComplete() const override;
    bool isGameOver() const override;
    nlohmann::json getState() const override;
    nlohmann::json getCommands() const override;
    std::vector<std::pair<std::string, int>> getLeaderboard() const override;
    int maxPlayers() const override { return m_maxPlayers; }

private:
    // Chat command handlers
    void cmdJoin(const std::string& userId, const std::string& displayName);
    void cmdLeft(const std::string& userId);
    void cmdRight(const std::string& userId);
    void cmdJump(const std::string& userId);
    void cmdJumpLeft(const std::string& userId);
    void cmdJumpRight(const std::string& userId);
    void cmdAttack(const std::string& userId);
    void cmdSpecial(const std::string& userId);
    void cmdDash(const std::string& userId);
    void cmdBlock(const std::string& userId);
    void cmdEmote(const std::string& userId, const std::string& emote);

    // Game logic
    void startCountdown();
    void startBattle();
    void checkRoundEnd();
    void endRound();
    void spawnPowerUp();
    void applyDamage(Player& attacker, Player& victim, float damage, const std::string& method);
    void eliminatePlayer(Player& player, const std::string& killerName, const std::string& method);
    void respawnPlayer(Player& player);
    sf::Color generatePlayerColor();

    // Rendering helpers
    void renderArena(sf::RenderTarget& target);
    void renderPlayers(sf::RenderTarget& target, double alpha);
    void renderProjectiles(sf::RenderTarget& target);
    void renderPowerUps(sf::RenderTarget& target);
    void renderParticles(sf::RenderTarget& target);
    void renderUI(sf::RenderTarget& target);
    void renderKillFeed(sf::RenderTarget& target);
    void renderLeaderboard(sf::RenderTarget& target);
    void renderCountdown(sf::RenderTarget& target);

    /// Scale a font size by the current font scale factor.
    unsigned int fs(int base) const {
        return static_cast<unsigned int>(std::max(1.0f, base * m_fontScale));
    }

    // State
    GamePhase                                       m_phase = GamePhase::Lobby;
    std::unordered_map<std::string, Player>         m_players;        // userId -> Player
    std::vector<Projectile>                         m_projectiles;
    std::vector<PowerUp>                            m_powerUps;
    std::deque<KillFeedEntry>                       m_killFeed;

    // Subsystems
    std::unique_ptr<PhysicsWorld>   m_physics;
    std::unique_ptr<Arena>          m_arena;
    std::unique_ptr<ParticleSystem> m_particles;

    // Rendering helpers
    rendering::Background      m_background;
    rendering::PostProcessing  m_postProcessing;
    sf::Font                   m_font;
    bool                       m_fontLoaded = false;

    // Timing
    double m_countdownTimer    = 0.0;
    double m_roundTimer        = 0.0;
    double m_lobbyTimer        = 0.0;
    double m_powerUpSpawnTimer = 0.0;
    int    m_roundNumber       = 0;
    int    m_maxRounds         = 5;
    double m_roundDuration     = 120.0;  // seconds
    double m_lobbyDuration     = 30.0;   // seconds
    int    m_minPlayers        = 2;
    int    m_maxPlayers        = 20;

    // Leaderboard
    struct LeaderboardEntry {
        std::string displayName;
        int kills = 0;
        int wins = 0;
        int totalScore = 0;
    };
    std::unordered_map<std::string, LeaderboardEntry> m_leaderboard;

    // RNG
    std::mt19937 m_rng;

    // Arena dimensions (world units)
    static constexpr float ARENA_WIDTH  = 22.5f;
    static constexpr float ARENA_HEIGHT = 40.0f;
    static constexpr float PIXELS_PER_METER = 48.0f;
};

} // namespace is::games::chaos_arena
