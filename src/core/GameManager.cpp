#include "core/GameManager.h"
#include "core/Application.h"
#include "games/GameRegistry.h"
#include "rendering/Renderer.h"
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
    if (m_chatFeedback)
        m_activeGame->setChatFeedback(m_chatFeedback);
    m_activeGame->initialize();

    // Apply stored per-game settings
    nlohmann::json settingsToApply;
    {
        std::lock_guard<std::mutex> lock(m_settingsMutex);
        auto sit = m_gameSettings.find(name);
        if (sit != m_gameSettings.end()) {
            settingsToApply = sit->second;
        }
        m_pendingSettings.erase(name);
    }
    if (!settingsToApply.empty()) {
        m_activeGame->configure(settingsToApply);
        spdlog::info("Applied {} settings for game '{}'.", settingsToApply.size(), name);
    }

    spdlog::info("Game '{}' loaded and initialized.", name);

    // Update window title to reflect the active game
    try {
        auto& app = core::Application::instance();
        app.renderer().setWindowTitle("InteractiveStreams - " + m_activeGame->displayName());
    } catch (...) {
        // Application may not be fully initialized yet
    }
}

void GameManager::requestSwitch(const std::string& gameName, SwitchMode mode) {
    // Always queue the switch – the main loop will execute it via
    // checkPendingSwitch().  This is critical because web-API threads must
    // never call loadGame() directly (the main loop may be mid-render).
    std::lock_guard<std::mutex> lock(m_switchMutex);
    m_hasPending  = true;
    m_pendingGame = gameName;
    m_pendingMode = mode;

    const char* modeStr = "immediate";
    if (mode == SwitchMode::AfterRound)     modeStr = "after round";
    else if (mode == SwitchMode::AfterGame) modeStr = "after game";
    spdlog::info("Game switch to '{}' queued ({}).", gameName, modeStr);
}

void GameManager::cancelPendingSwitch() {
    std::lock_guard<std::mutex> lock(m_switchMutex);
    if (m_hasPending) {
        spdlog::info("Pending game switch to '{}' cancelled.", m_pendingGame);
        m_hasPending = false;
        m_pendingGame.clear();
    }
}

void GameManager::checkPendingSwitch() {
    std::string gameName;
    {
        std::lock_guard<std::mutex> lock(m_switchMutex);
        if (!m_hasPending) return;

        bool shouldSwitch = false;

        if (m_pendingMode == SwitchMode::Immediate) {
            shouldSwitch = true;  // execute on the very next main-loop tick
        } else if (!m_activeGame) {
            shouldSwitch = true;  // no game loaded – switch immediately
        } else if (m_pendingMode == SwitchMode::AfterRound) {
            shouldSwitch = m_activeGame->isRoundComplete();
        } else if (m_pendingMode == SwitchMode::AfterGame) {
            shouldSwitch = m_activeGame->isGameOver();
        }

        if (!shouldSwitch) return;

        gameName = m_pendingGame;
        m_hasPending = false;
        m_pendingGame.clear();
    }

    // Perform the switch outside the lock (always on the main thread)
    spdlog::info("Deferred game switch executing: loading '{}'.", gameName);
    loadGame(gameName);
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

std::string GameManager::activeGameName() const {
    return m_activeGameName;
}

void GameManager::setChatFeedback(games::ChatFeedbackCallback cb) {
    m_chatFeedback = std::move(cb);
    // Also install on the currently active game (if any)
    if (m_activeGame && m_chatFeedback)
        m_activeGame->setChatFeedback(m_chatFeedback);
}

void GameManager::handleChatMessage(const platform::ChatMessage& msg) {
    if (m_activeGame) {
        m_activeGame->onChatMessage(msg);
    }
}

void GameManager::update(double dt) {
    applyPendingSettings();
    if (m_activeGame) {
        m_activeGame->update(dt);
    }
}

void GameManager::render(sf::RenderTarget& target, double alpha) {
    applyPendingSettings();
    if (m_activeGame) {
        m_activeGame->render(target, alpha);
    }
}

std::vector<std::string> GameManager::availableGames() const {
    return games::GameRegistry::instance().list();
}

bool GameManager::hasPendingSwitch() const {
    std::lock_guard<std::mutex> lock(m_switchMutex);
    return m_hasPending;
}

std::string GameManager::pendingGameName() const {
    std::lock_guard<std::mutex> lock(m_switchMutex);
    return m_pendingGame;
}

SwitchMode GameManager::pendingSwitchMode() const {
    std::lock_guard<std::mutex> lock(m_switchMutex);
    return m_pendingMode;
}

void GameManager::setGameSettings(const std::string& gameId, const nlohmann::json& settings) {
    {
        std::lock_guard<std::mutex> lock(m_settingsMutex);
        m_gameSettings[gameId] = settings;
        m_pendingSettings[gameId] = settings;
    }

    spdlog::info("Queued live settings update for game '{}'.", gameId);
}

nlohmann::json GameManager::getGameSettings(const std::string& gameId) const {
    std::lock_guard<std::mutex> lock(m_settingsMutex);
    auto it = m_gameSettings.find(gameId);
    return (it != m_gameSettings.end()) ? it->second : nlohmann::json::object();
}

nlohmann::json GameManager::getAllGameSettings() const {
    std::lock_guard<std::mutex> lock(m_settingsMutex);
    nlohmann::json result = nlohmann::json::object();
    for (const auto& [id, settings] : m_gameSettings) {
        result[id] = settings;
    }
    return result;
}

void GameManager::applyPendingSettings() {
    if (!m_activeGame) return;

    nlohmann::json settingsToApply;
    {
        std::lock_guard<std::mutex> lock(m_settingsMutex);
        auto it = m_pendingSettings.find(m_activeGameName);
        if (it == m_pendingSettings.end()) return;
        settingsToApply = it->second;
        m_pendingSettings.erase(it);
    }

    if (!settingsToApply.empty()) {
        m_activeGame->configure(settingsToApply);
        spdlog::info("Applied live settings update for active game '{}'.", m_activeGameName);
    }
}

} // namespace is::core
