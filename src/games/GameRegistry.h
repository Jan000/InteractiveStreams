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
/// Usage: REGISTER_GAME(ChaosArena)
#define REGISTER_GAME(GameClass) \
    static is::games::GameRegistrar s_registrar_##GameClass( \
        GameClass().id(), \
        []() -> std::unique_ptr<is::games::IGame> { return std::make_unique<GameClass>(); } \
    )

} // namespace is::games
