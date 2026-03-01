#pragma once

#include "games/IGame.h"
#include "platform/ChatMessage.h"

#include <SFML/Graphics.hpp>
#include <string>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace is::core {

/// Mode for game switching.
enum class SwitchMode {
    Immediate,   ///< Switch right now, even mid-game
    AfterRound,  ///< Wait until the current round ends
    AfterGame    ///< Wait until the full game session ends
};

/// Manages game instances and delegates updates/rendering to the active game.
/// Supports immediate and deferred game switching (thread-safe).
class GameManager {
public:
    GameManager();
    ~GameManager();

    /// Load and activate a game by its registered name (immediate switch).
    void loadGame(const std::string& name);

    /// Request a game switch with a specific mode.
    /// If mode is Immediate, switches right away.
    /// Otherwise queues the switch for the main-loop to execute at the right time.
    void requestSwitch(const std::string& gameName, SwitchMode mode);

    /// Cancel a pending deferred switch.
    void cancelPendingSwitch();

    /// Check if a deferred switch should execute and perform it.
    /// Called from the main loop after update().
    void checkPendingSwitch();

    /// Unload the current game.
    void unloadGame();

    /// Get the active game (may be nullptr).
    games::IGame* activeGame() const;

    /// Get the active game name.
    std::string activeGameName() const;

    /// Forward a chat message to the active game.
    void handleChatMessage(const platform::ChatMessage& msg);

    /// Set a chat feedback callback that will be installed on every loaded game.
    void setChatFeedback(games::ChatFeedbackCallback cb);

    /// Update the active game.
    void update(double dt);

    /// Render the active game.
    void render(sf::RenderTarget& target, double alpha);

    /// Get list of available game names.
    std::vector<std::string> availableGames() const;

    /// Get info about a pending switch (for dashboard display).
    bool hasPendingSwitch() const;
    std::string pendingGameName() const;
    SwitchMode  pendingSwitchMode() const;

private:
    std::unique_ptr<games::IGame> m_activeGame;
    std::string                   m_activeGameName;

    // Chat feedback callback (installed on every newly loaded game)
    games::ChatFeedbackCallback   m_chatFeedback;

    // Deferred switch state (guarded by mutex for thread safety)
    mutable std::mutex m_switchMutex;
    bool               m_hasPending   = false;
    std::string        m_pendingGame;
    SwitchMode         m_pendingMode  = SwitchMode::Immediate;
};

} // namespace is::core
