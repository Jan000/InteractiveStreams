#pragma once

#include "games/IGame.h"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace is::games {

/// Factory function type for creating game instances.
using GameFactory = std::function<std::unique_ptr<IGame>()>;

/// Singleton registry for game factories. Games self-register using REGISTER_GAME macro.
class GameRegistry {
public:
    static GameRegistry& instance();

    /// Register a game factory under the given name.
    void registerGame(const std::string& name, GameFactory factory);

    /// Create a new game instance by name.
    std::unique_ptr<IGame> create(const std::string& name) const;

    /// List all registered game names.
    std::vector<std::string> list() const;

    /// Check if a game is registered.
    bool has(const std::string& name) const;

private:
    GameRegistry() = default;
    std::unordered_map<std::string, GameFactory> m_factories;
};

/// Helper struct for static auto-registration.
struct GameRegistrar {
    GameRegistrar(const std::string& name, GameFactory factory) {
        GameRegistry::instance().registerGame(name, std::move(factory));
    }
};

/// Macro to auto-register a game class. Use in the .cpp file.
/// The ID must match what IGame::id() returns for this class.
/// Usage: REGISTER_GAME(ChaosArena, "chaos_arena")
/// IMPORTANT: Do NOT use GameClass().id() here – constructing a game instance
/// during static initialization causes SIGSEGV on Linux because game classes
/// have SFML/PostProcessing members that require an OpenGL context and
/// spdlog to be initialized first (static init order fiasco).
#define REGISTER_GAME(GameClass, GameId) \
    static is::games::GameRegistrar s_registrar_##GameClass( \
        GameId, \
        []() -> std::unique_ptr<is::games::IGame> { return std::make_unique<GameClass>(); } \
    )

} // namespace is::games
