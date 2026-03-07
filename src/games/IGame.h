#pragma once

#include "platform/ChatMessage.h"
#include <SFML/Graphics.hpp>
#include <string>
#include <functional>
#include <nlohmann/json.hpp>

namespace is::games {

/// Callback type for sending chat feedback to viewers.
/// Games call this to send confirmation messages (e.g. "Player joined!").
using ChatFeedbackCallback = std::function<void(const std::string& message)>;

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

    /// Maximum number of players this game supports (0 = unlimited).
    virtual int maxPlayers() const { return 0; }

    /// Set the font scale factor (default 1.0).
    virtual void setFontScale(float scale) { m_fontScale = scale; }

    /// Get the current font scale factor.
    float fontScale() const { return m_fontScale; }

    /// Configure game-specific settings from JSON.
    /// Called when a game is loaded or when settings change.
    /// Games should override this to accept custom parameters.
    virtual void configure(const nlohmann::json& settings) { (void)settings; }

    /// Set the chat feedback callback.  The stream instance installs this
    /// so that games can send confirmation messages to viewers.
    void setChatFeedback(ChatFeedbackCallback cb) { m_chatFeedback = std::move(cb); }

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

    /// Returns true if the current round / phase has ended and a game switch
    /// could happen without interrupting active gameplay.
    virtual bool isRoundComplete() const = 0;

    /// Returns true if the entire game session is over.
    virtual bool isGameOver() const = 0;

    /// Get current game state as JSON (for the web dashboard).
    virtual nlohmann::json getState() const = 0;

    /// Get available chat commands as JSON.
    virtual nlohmann::json getCommands() const = 0;

protected:
    /// Send a feedback message to viewers via the installed callback.
    void sendChatFeedback(const std::string& message) {
        if (m_chatFeedback) m_chatFeedback(message);
    }

    float m_fontScale = 1.0f;
    ChatFeedbackCallback m_chatFeedback;
};

} // namespace is::games
