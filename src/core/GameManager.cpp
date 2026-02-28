#include "core/GameManager.h"
#include "games/GameRegistry.h"
#include <spdlog/spdlog.h>

namespace is::core {

GameManager::GameManager() = default;
GameManager::~GameManager() = default;

void GameManager::loadGame(const std::string& name) {
    if (m_activeGame) {
        unloadGame();
    }

    auto game = games::GameRegistry::instance().create(name);
    if (!game) {
        spdlog::error("Failed to load game: '{}' – not found in registry.", name);
        return;
    }

    m_activeGame = std::move(game);
    m_activeGameName = name;
    m_activeGame->initialize();
    spdlog::info("Game '{}' loaded and initialized.", name);
}

void GameManager::unloadGame() {
    if (m_activeGame) {
        m_activeGame->shutdown();
        spdlog::info("Game '{}' unloaded.", m_activeGameName);
        m_activeGame.reset();
        m_activeGameName.clear();
    }
}

games::IGame* GameManager::activeGame() const {
    return m_activeGame.get();
}

void GameManager::handleChatMessage(const platform::ChatMessage& msg) {
    if (m_activeGame) {
        m_activeGame->onChatMessage(msg);
    }
}

void GameManager::update(double dt) {
    if (m_activeGame) {
        m_activeGame->update(dt);
    }
}

void GameManager::render(sf::RenderTarget& target, double alpha) {
    if (m_activeGame) {
        m_activeGame->render(target, alpha);
    }
}

std::vector<std::string> GameManager::availableGames() const {
    return games::GameRegistry::instance().list();
}

} // namespace is::core
