#pragma once

#include "platform/ChatMessage.h"
#include <SFML/Graphics.hpp>
#include <string>
#include <nlohmann/json.hpp>

namespace is::games {

/// Abstract interface that all games must implement.
/// This is the core extension point for adding new games.
class IGame {
public:
    virtual ~IGame() = default;

    /// Unique identifier for this game type.
    virtual std::string id() const = 0;

    /// Human-readable display name.
    virtual std::string displayName() const = 0;

    /// Short description.
    virtual std::string description() const = 0;

    /// Initialize game state. Called once when the game is loaded.
    virtual void initialize() = 0;

    /// Clean up game state. Called when switching away from this game.
    virtual void shutdown() = 0;

    /// Process a chat message from a viewer.
    virtual void onChatMessage(const platform::ChatMessage& msg) = 0;

    /// Fixed-timestep game logic update.
    virtual void update(double dt) = 0;

    /// Render the game to the given target.
    /// @param alpha Interpolation factor for smooth rendering between updates.
    virtual void render(sf::RenderTarget& target, double alpha) = 0;

    /// Get current game state as JSON (for the web dashboard).
    virtual nlohmann::json getState() const = 0;

    /// Get available chat commands as JSON.
    virtual nlohmann::json getCommands() const = 0;
};

} // namespace is::games
