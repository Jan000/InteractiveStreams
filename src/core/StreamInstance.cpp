#include "core/StreamInstance.h"
#include "games/GameRegistry.h"
#include "streaming/StreamEncoder.h"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <random>

namespace is::core {

// ─────────────────────────────────────────────────────────────────────────────
StreamInstance::StreamInstance(const StreamConfig& config)
    : m_config(config)
{
    m_gameManager = std::make_unique<GameManager>();

    // Font for vote overlay (may fail if not found)
    m_fontLoaded = m_font.loadFromFile("assets/fonts/JetBrainsMono-Regular.ttf");

    // Load the initial game
    if (!m_config.fixedGame.empty()) {
        m_gameManager->loadGame(m_config.fixedGame);
    }

    spdlog::info("[Stream '{}'] Created ({}x{}, mode={})",
        m_config.name, width(), height(),
        m_config.gameMode == GameModeType::Fixed  ? "fixed" :
        m_config.gameMode == GameModeType::Vote   ? "vote"  : "random");
}

StreamInstance::~StreamInstance() {
    stopStreaming();
}

// ── Lazy render-target ───────────────────────────────────────────────────────

void StreamInstance::ensureRenderTexture() {
    if (m_rtReady) return;
    if (!m_renderTexture.create(width(), height())) {
        spdlog::error("[Stream '{}'] Failed to create RenderTexture {}x{}",
            m_config.name, width(), height());
    }
    m_renderTexture.setSmooth(true);
    m_rtReady = true;
}

sf::RenderTexture& StreamInstance::renderTexture() {
    ensureRenderTexture();
    return m_renderTexture;
}

// ── Main-loop methods ────────────────────────────────────────────────────────

void StreamInstance::handleChatMessage(const platform::ChatMessage& msg) {
    // Intercept vote commands
    if (m_voteState.active && msg.text.size() >= 5 &&
        msg.text.substr(0, 5) == "!vote") {
        std::string gameName = (msg.text.size() > 6) ? msg.text.substr(6) : "";
        // trim
        while (!gameName.empty() && gameName.back()  == ' ') gameName.pop_back();
        while (!gameName.empty() && gameName.front() == ' ') gameName.erase(gameName.begin());
        if (!gameName.empty()) handleVoteCommand(msg.userId, gameName);
        return; // don't forward vote commands to the game
    }
    m_gameManager->handleChatMessage(msg);
}

void StreamInstance::update(double dt) {
    m_gameManager->update(dt);
    m_gameManager->checkPendingSwitch();
    updateGameMode(dt);
}

void StreamInstance::render(double alpha) {
    ensureRenderTexture();
    m_renderTexture.clear(sf::Color(15, 15, 25));

    if (m_gameManager->activeGame()) {
        m_gameManager->render(m_renderTexture, alpha);
    }

    if (m_voteState.active) {
        renderVoteOverlay();
    }

    m_renderTexture.display();
    m_frameCapture = m_renderTexture.getTexture().copyToImage();
}

void StreamInstance::encodeFrame() {
    if (m_encoder && m_encoder->isRunning()) {
        m_encoder->encodeFrame(getFrameBuffer());
    }
}

const sf::Uint8* StreamInstance::getFrameBuffer() const {
    return m_frameCapture.getPixelsPtr();
}

// ── Game-mode logic ──────────────────────────────────────────────────────────

void StreamInstance::updateGameMode(double dt) {
    auto* game = m_gameManager->activeGame();
    if (!game) return;

    // Detect game-over
    if (game->isGameOver() && !m_waitingForTransition && !m_voteState.active) {
        m_waitingForTransition = true;
        m_transitionTimer      = 0.0;
        spdlog::info("[Stream '{}'] Game over – scheduling transition (mode={}).",
            m_config.name,
            m_config.gameMode == GameModeType::Fixed  ? "fixed" :
            m_config.gameMode == GameModeType::Vote   ? "vote"  : "random");
    }

    // Transition delay
    if (m_waitingForTransition) {
        m_transitionTimer += dt;
        if (m_transitionTimer >= m_transitionDelay) {
            m_waitingForTransition = false;
            switch (m_config.gameMode) {
                case GameModeType::Fixed:
                    restartCurrentGame();
                    break;
                case GameModeType::Vote:
                    m_voteState = VoteState{};
                    m_voteState.active = true;
                    spdlog::info("[Stream '{}'] Vote started ({:.0f}s)",
                        m_config.name, m_voteState.duration);
                    break;
                case GameModeType::Random:
                    startRandomGame();
                    break;
            }
        }
    }

    // Vote timer
    if (m_voteState.active) {
        m_voteState.timer += dt;
        if (m_voteState.timer >= m_voteState.duration) {
            m_voteState.active = false;
            startNextGameFromVote();
        }
    }
}

void StreamInstance::handleVoteCommand(const std::string& userId,
                                        const std::string& gameName) {
    if (!m_voteState.active) return;
    if (!games::GameRegistry::instance().has(gameName)) return;

    // Remove previous vote
    auto prev = m_voteState.userVotes.find(userId);
    if (prev != m_voteState.userVotes.end()) {
        m_voteState.tallies[prev->second]--;
    }
    m_voteState.userVotes[userId] = gameName;
    m_voteState.tallies[gameName]++;
}

void StreamInstance::startNextGameFromVote() {
    if (m_voteState.tallies.empty()) { restartCurrentGame(); return; }

    std::string winner;
    int maxVotes = 0;
    for (const auto& [game, count] : m_voteState.tallies) {
        if (count > maxVotes) { maxVotes = count; winner = game; }
    }
    spdlog::info("[Stream '{}'] Vote result: '{}' wins ({} votes)",
        m_config.name, winner, maxVotes);
    m_gameManager->loadGame(winner);
}

void StreamInstance::startRandomGame() {
    auto games = getAvailableGameIds();
    if (games.empty()) return;

    auto current = m_gameManager->activeGameName();
    if (games.size() > 1)
        games.erase(std::remove(games.begin(), games.end(), current), games.end());

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<size_t> dist(0, games.size() - 1);

    auto chosen = games[dist(gen)];
    spdlog::info("[Stream '{}'] Random game: '{}'", m_config.name, chosen);
    m_gameManager->loadGame(chosen);
}

void StreamInstance::restartCurrentGame() {
    auto current = m_gameManager->activeGameName();
    if (current.empty()) current = m_config.fixedGame;
    spdlog::info("[Stream '{}'] Restarting '{}'", m_config.name, current);
    m_gameManager->loadGame(current);
}

std::vector<std::string> StreamInstance::getAvailableGameIds() const {
    return games::GameRegistry::instance().list();
}

// ── Vote overlay rendering ───────────────────────────────────────────────────

void StreamInstance::renderVoteOverlay() {
    if (!m_fontLoaded) return;
    float w = static_cast<float>(width());
    float h = static_cast<float>(height());

    // Dim background
    sf::RectangleShape overlay(sf::Vector2f(w, h));
    overlay.setFillColor(sf::Color(0, 0, 0, 180));
    m_renderTexture.draw(overlay);

    // Title
    sf::Text title;
    title.setFont(m_font);
    title.setString("VOTE FOR NEXT GAME!");
    title.setCharacterSize(36);
    title.setFillColor(sf::Color(255, 215, 0));
    title.setStyle(sf::Text::Bold);
    auto tb = title.getLocalBounds();
    title.setPosition((w - tb.width) / 2.0f, h * 0.15f);
    m_renderTexture.draw(title);

    // Timer
    float remaining = static_cast<float>(m_voteState.duration - m_voteState.timer);
    sf::Text timer;
    timer.setFont(m_font);
    timer.setString("Time: " + std::to_string(static_cast<int>(remaining)) + "s");
    timer.setCharacterSize(24);
    timer.setFillColor(sf::Color(200, 200, 200));
    auto tmb = timer.getLocalBounds();
    timer.setPosition((w - tmb.width) / 2.0f, h * 0.22f);
    m_renderTexture.draw(timer);

    // Instructions
    sf::Text instr;
    instr.setFont(m_font);
    instr.setString("Type !vote <game_id> in chat");
    instr.setCharacterSize(18);
    instr.setFillColor(sf::Color(150, 150, 150));
    auto ib = instr.getLocalBounds();
    instr.setPosition((w - ib.width) / 2.0f, h * 0.27f);
    m_renderTexture.draw(instr);

    // Game cards
    auto gameIds   = getAvailableGameIds();
    float y        = h * 0.35f;
    float cardH    = 60.0f;
    float cardW    = w * 0.7f;
    float cardX    = (w - cardW) / 2.0f;

    for (const auto& gid : gameIds) {
        int votes = 0;
        auto it = m_voteState.tallies.find(gid);
        if (it != m_voteState.tallies.end()) votes = it->second;

        sf::RectangleShape card(sf::Vector2f(cardW, cardH));
        card.setPosition(cardX, y);
        card.setFillColor(sf::Color(30, 40, 60, 200));
        card.setOutlineColor(sf::Color(80, 130, 200));
        card.setOutlineThickness(1.5f);
        m_renderTexture.draw(card);

        auto temp = games::GameRegistry::instance().create(gid);
        std::string dispName = temp ? temp->displayName() : gid;

        sf::Text nameT;
        nameT.setFont(m_font);
        nameT.setString(dispName);
        nameT.setCharacterSize(20);
        nameT.setFillColor(sf::Color::White);
        nameT.setPosition(cardX + 15.0f, y + 8.0f);
        m_renderTexture.draw(nameT);

        sf::Text voteT;
        voteT.setFont(m_font);
        voteT.setString(std::to_string(votes) + " votes");
        voteT.setCharacterSize(16);
        voteT.setFillColor(sf::Color(88, 166, 255));
        voteT.setPosition(cardX + 15.0f, y + 34.0f);
        m_renderTexture.draw(voteT);

        sf::Text idT;
        idT.setFont(m_font);
        idT.setString("!vote " + gid);
        idT.setCharacterSize(14);
        idT.setFillColor(sf::Color(120, 120, 140));
        auto idb = idT.getLocalBounds();
        idT.setPosition(cardX + cardW - idb.width - 15.0f, y + 20.0f);
        m_renderTexture.draw(idT);

        y += cardH + 10.0f;
    }
}

// ── Streaming control ────────────────────────────────────────────────────────

bool StreamInstance::isStreaming() const {
    return m_encoder && m_encoder->isRunning();
}

void StreamInstance::startStreaming() {
    std::string url = m_config.getFullStreamUrl();
    if (url.empty()) {
        spdlog::info("[Stream '{}'] No stream URL configured.", m_config.name);
        return;
    }
    streaming::EncoderSettings es;
    es.outputUrl  = url;
    es.width      = width();
    es.height     = height();
    es.fps        = m_config.fps;
    es.bitrate    = m_config.bitrate;
    es.preset     = m_config.preset;
    es.codec      = m_config.codec;

    m_encoder = std::make_unique<streaming::StreamEncoder>(es);
    m_encoder->start();
    spdlog::info("[Stream '{}'] Streaming started to {}", m_config.name, url);
}

void StreamInstance::stopStreaming() {
    if (m_encoder) {
        m_encoder->stop();
        m_encoder.reset();
    }
}

// ── Configuration update ─────────────────────────────────────────────────────

void StreamInstance::updateConfig(const StreamConfig& c) {
    bool resChanged  = (c.resolution != m_config.resolution);
    bool gameChanged = (c.fixedGame != m_config.fixedGame &&
                        c.gameMode == GameModeType::Fixed);
    m_config = c;
    if (resChanged) { m_rtReady = false; }
    if (gameChanged) { m_gameManager->loadGame(m_config.fixedGame); }
}

// ── Serialisation ────────────────────────────────────────────────────────────

nlohmann::json StreamInstance::getState() const {
    nlohmann::json s;
    s["id"]         = m_config.id;
    s["name"]       = m_config.name;
    s["resolution"] = m_config.resolution == ResolutionPreset::Mobile ? "mobile" : "desktop";
    s["width"]      = width();
    s["height"]     = height();
    s["gameMode"]   = m_config.gameMode == GameModeType::Fixed  ? "fixed" :
                      m_config.gameMode == GameModeType::Vote   ? "vote"  : "random";
    s["streaming"]  = isStreaming();
    s["channelIds"] = m_config.channelIds;

    if (auto* g = m_gameManager->activeGame()) {
        s["game"]["id"]              = g->id();
        s["game"]["name"]            = g->displayName();
        s["game"]["state"]           = g->getState();
        s["game"]["commands"]        = g->getCommands();
        s["game"]["isRoundComplete"] = g->isRoundComplete();
        s["game"]["isGameOver"]      = g->isGameOver();
    }
    if (m_gameManager->hasPendingSwitch()) {
        s["pendingSwitch"]["game"] = m_gameManager->pendingGameName();
        int mode = static_cast<int>(m_gameManager->pendingSwitchMode());
        s["pendingSwitch"]["mode"] = mode == 1 ? "after_round" : "after_game";
    }
    if (m_voteState.active) {
        s["vote"]["active"]   = true;
        s["vote"]["timer"]    = m_voteState.timer;
        s["vote"]["duration"] = m_voteState.duration;
        s["vote"]["tallies"]  = m_voteState.tallies;
    }
    return s;
}

nlohmann::json StreamInstance::toJson() const {
    nlohmann::json j;
    j["id"]           = m_config.id;
    j["name"]         = m_config.name;
    j["resolution"]   = m_config.resolution == ResolutionPreset::Mobile ? "mobile" : "desktop";
    j["game_mode"]    = m_config.gameMode == GameModeType::Fixed ? "fixed" :
                        m_config.gameMode == GameModeType::Vote  ? "vote"  : "random";
    j["fixed_game"]   = m_config.fixedGame;
    j["channel_ids"]  = m_config.channelIds;
    j["stream_url"]   = m_config.streamUrl;
    j["stream_key"]   = m_config.streamKey;
    j["enabled"]      = m_config.enabled;
    j["fps"]          = m_config.fps;
    j["bitrate_kbps"] = m_config.bitrate;
    j["preset"]       = m_config.preset;
    j["codec"]        = m_config.codec;
    return j;
}

StreamConfig StreamInstance::configFromJson(const nlohmann::json& j) {
    StreamConfig c;
    c.id   = j.value("id", "");
    c.name = j.value("name", "Stream");

    std::string res = j.value("resolution", "mobile");
    c.resolution = (res == "desktop") ? ResolutionPreset::Desktop : ResolutionPreset::Mobile;

    std::string mode = j.value("game_mode", "fixed");
    if (mode == "vote")        c.gameMode = GameModeType::Vote;
    else if (mode == "random") c.gameMode = GameModeType::Random;
    else                       c.gameMode = GameModeType::Fixed;

    c.fixedGame = j.value("fixed_game", "chaos_arena");

    if (j.contains("channel_ids") && j["channel_ids"].is_array()) {
        for (const auto& ch : j["channel_ids"])
            c.channelIds.push_back(ch.get<std::string>());
    }

    c.streamUrl = j.value("stream_url", "");
    c.streamKey = j.value("stream_key", "");
    c.enabled   = j.value("enabled", true);
    c.fps       = j.value("fps", 30);
    c.bitrate   = j.value("bitrate_kbps", 4500);
    c.preset    = j.value("preset", "fast");
    c.codec     = j.value("codec", "libx264");
    return c;
}

} // namespace is::core
