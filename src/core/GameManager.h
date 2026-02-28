#pragma once

#include "games/IGame.h"
#include "platform/ChatMessage.h"

#include <SFML/Graphics.hpp>
#include <string>
#include <memory>
#include <unordered_map>

namespace is::core {

/// Manages game instances and delegates updates/rendering to the active game.
class GameManager {
public:
    GameManager();
    ~GameManager();

    /// Load and activate a game by its registered name.
    void loadGame(const std::string& name);

    /// Unload the current game.
    void unloadGame();

    /// Get the active game (may be nullptr).
    games::IGame* activeGame() const;

    /// Forward a chat message to the active game.
    void handleChatMessage(const platform::ChatMessage& msg);

    /// Update the active game.
    void update(double dt);

    /// Render the active game.
    void render(sf::RenderTarget& target, double alpha);

    /// Get list of available game names.
    std::vector<std::string> availableGames() const;

private:
    std::unique_ptr<games::IGame> m_activeGame;
    std::string                   m_activeGameName;
};

} // namespace is::core
