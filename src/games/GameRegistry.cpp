#include "games/GameRegistry.h"
#include <spdlog/spdlog.h>

namespace is::games {

GameRegistry& GameRegistry::instance() {
    static GameRegistry registry;
    return registry;
}

void GameRegistry::registerGame(const std::string& name, GameFactory factory) {
    m_factories[name] = std::move(factory);
    spdlog::info("Game registered: '{}'", name);
}

std::unique_ptr<IGame> GameRegistry::create(const std::string& name) const {
    auto it = m_factories.find(name);
    if (it != m_factories.end()) {
        return it->second();
    }
    return nullptr;
}

std::vector<std::string> GameRegistry::list() const {
    std::vector<std::string> names;
    names.reserve(m_factories.size());
    for (const auto& [name, _] : m_factories) {
        names.push_back(name);
    }
    return names;
}

bool GameRegistry::has(const std::string& name) const {
    return m_factories.count(name) > 0;
}

} // namespace is::games
