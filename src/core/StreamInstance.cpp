#include "core/StreamInstance.h"
#include "core/Config.h"
#include "core/Application.h"
#include "core/AudioMixer.h"
#include "core/ChannelManager.h"
#include "rendering/Renderer.h"
#include "games/GameRegistry.h"
#include "games/country_elimination/CountryAliases.h"
#include "streaming/StreamEncoder.h"
#include "platform/twitch/TwitchApi.h"
#include "platform/youtube/YouTubeApi.h"

#include <stb_image_write.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <random>

namespace is::core {

// ─────────────────────────────────────────────────────────────────────────────
StreamInstance::StreamInstance(const StreamConfig& config)
    : m_config(config)
{
    m_gameManager = std::make_unique<GameManager>();

    // Install chat feedback callback – routes game messages to all subscribed channels
    m_gameManager->setChatFeedback([this](const std::string& message) {
        try {
            auto& cm = Application::instance().channelManager();
            for (const auto& chId : m_config.channelIds) {
                cm.sendMessageToChannel(chId, message);
            }
        } catch (...) {}
    });

    // Install spectrum callback – games can query real-time audio spectrum data
    m_gameManager->setSpectrumCallback([](float* out, int numBands) -> bool {
        try {
            return Application::instance().audioMixer().getSpectrumBands(out, numBands);
        } catch (...) {
            return false;
        }
    });

    // Font for vote overlay (may fail if not found)
    m_fontLoaded = m_font.loadFromFile("assets/fonts/JetBrainsMono-Regular.ttf");

    // Build initial scoreboard panel list
    rebuildScoreboardPanels();

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

void StreamInstance::ensureSecondaryRenderTexture() {
    if (m_secondaryRtReady) return;
    int w = m_config.secondaryWidth();
    int h = m_config.secondaryHeight();
    if (!m_secondaryRenderTexture.create(w, h)) {
        spdlog::error("[Stream '{}'] Failed to create secondary RenderTexture {}x{}",
            m_config.name, w, h);
    }
    m_secondaryRenderTexture.setSmooth(true);
    m_secondaryRtReady = true;
}

sf::RenderTexture& StreamInstance::renderTexture() {
    ensureRenderTexture();
    return m_renderTexture;
}

// ── Main-loop methods ────────────────────────────────────────────────────────

void StreamInstance::handleChatMessage(const platform::ChatMessage& msg) {
    // Record per-stream statistics
    m_stats.recordMessage(msg.userId, msg.displayName);

    // Intercept vote commands (with or without ! prefix)
    std::string voteText = msg.text;
    if (!voteText.empty() && voteText[0] == '!') voteText = voteText.substr(1);
    if (m_voteState.active && voteText.size() >= 4 &&
        voteText.substr(0, 4) == "vote") {
        std::string gameName = (voteText.size() > 5) ? voteText.substr(5) : "";
        // trim
        while (!gameName.empty() && gameName.back()  == ' ') gameName.pop_back();
        while (!gameName.empty() && gameName.front() == ' ') gameName.erase(gameName.begin());
        if (!gameName.empty()) handleVoteCommand(msg.userId, gameName);
        return; // don't forward vote commands to the game
    }
    m_gameManager->handleChatMessage(msg);
    m_chatMessagesSinceLastInfo++;
}

void StreamInstance::update(double dt) {
    m_gameManager->update(dt);
    m_gameManager->checkPendingSwitch();
    updateGameMode(dt);

    // Refresh global scoreboard cache periodically
    m_scoreboardRefreshTimer += dt;
    if (m_scoreboardRefreshTimer >= SCOREBOARD_REFRESH_INTERVAL) {
        m_scoreboardRefreshTimer = 0.0;
        updateScoreboardCache();

        // Refresh round leaderboard from active game
        if (auto* game = m_gameManager->activeGame()) {
            m_playerRoundCache  = game->getLeaderboard();
            m_countryRoundCache = game->getCountryLeaderboard();
        } else {
            m_playerRoundCache.clear();
            m_countryRoundCache.clear();
        }

        // Rebuild panel groups (panels may appear/disappear as caches change)
        rebuildScoreboardPanels();
    }

    // Cycle scoreboard panel groups
    for (auto& [groupId, grp] : m_panelGroups) {
        if (grp.panelIndices.size() <= 1) continue;
        size_t idx = grp.panelIndices[static_cast<size_t>(
            grp.currentIndex % static_cast<int>(grp.panelIndices.size()))];
        double panelDuration = (idx < m_sbConfig.panels.size())
            ? m_sbConfig.panels[idx].durationSecs : 10.0;
        double fullCycle = panelDuration + m_sbConfig.fadeSecs;
        grp.cycleTimer += dt;
        if (grp.cycleTimer >= fullCycle) {
            grp.cycleTimer = 0.0;
            grp.currentIndex = (grp.currentIndex + 1)
                               % static_cast<int>(grp.panelIndices.size());
        }
    }

    // Periodic scoreboard chat posting
    if (m_sbConfig.chatInterval > 0) {
        m_scoreboardChatTimer += dt;
        if (m_scoreboardChatTimer >= static_cast<double>(m_sbConfig.chatInterval)) {
            m_scoreboardChatTimer = 0.0;
            sendScoreboardToChat();
        }
    }

    // Track game changes for description switching
    auto* game = m_gameManager->activeGame();
    std::string currentGameId = game ? game->id() : "";
    if (currentGameId != m_lastGameId) {
        m_lastGameId = currentGameId;
        m_infoMessageTimer = 0.0; // Reset info timer on game change

        if (game) {
            // Apply per-game font scale
            auto scaleIt = m_config.gameFontScales.find(currentGameId);
            float scale = (scaleIt != m_config.gameFontScales.end()) ? scaleIt->second : 1.0f;
            game->setFontScale(scale);
        }

        // Update stream description if per-game description is set
        if (!currentGameId.empty()) {
            auto descIt = m_config.gameDescriptions.find(currentGameId);
            if (descIt != m_config.gameDescriptions.end() && !descIt->second.empty()) {
                m_config.description = descIt->second;
            }
        }

        // Notify platforms (Twitch/YouTube) about the game change
        if (!currentGameId.empty()) {
            updatePlatformInfo(currentGameId);
        }
    }

    // Periodic info message
    if (game && !m_lastGameId.empty()) {
        auto intervalIt = m_config.gameInfoIntervals.find(m_lastGameId);
        int interval = (intervalIt != m_config.gameInfoIntervals.end()) ? intervalIt->second : 0;
        if (interval > 0) {
            m_infoMessageTimer += dt;
            if (m_infoMessageTimer >= static_cast<double>(interval)) {
                if (m_chatMessagesSinceLastInfo >= INFO_MSG_MIN_CHAT_ACTIVITY) {
                    m_infoMessageTimer = 0.0;
                    sendPeriodicInfoMessage();
                    m_chatMessagesSinceLastInfo = 0;
                } else {
                    // Chat is quiet – postpone, keep timer at threshold so it
                    // fires immediately once enough messages arrive.
                    m_infoMessageTimer = static_cast<double>(interval);
                }
            }
        }
    }
}

void StreamInstance::render(double alpha) {
    bool encoderRunning = isStreaming();
    bool windowActive   = !Application::instance().renderer().isHeadless();
    bool hasDualFormat  = m_config.dualFormat && !m_secondaryEncoders.empty();

    m_jpegFrameCounter++;
    bool needJpeg = (m_jpegFrameCounter >= m_jpegFrameInterval);

    // Determine if the encoder needs a new frame this iteration.
    // The encoder expects m_config.fps frames/sec, but the game loop runs at
    // application.target_fps (typically 60).  Only perform the expensive
    // GPU→CPU readback (copyToImage) on the frames the encoder actually needs.
    bool needEncode = false;
    if (encoderRunning && !m_encoders.empty()) {
        int targetFps = Application::instance().config().get<int>("application.target_fps", 60);
        int encFps    = m_config.fps > 0 ? m_config.fps : 30;
        int skip      = (targetFps > encFps) ? (targetFps / encFps) : 1;
        m_encodeFrameCounter++;
        if (m_encodeFrameCounter >= skip) {
            m_encodeFrameCounter = 0;
            needEncode = true;
        }
    }

    // Secondary format frame pacing (may have different FPS)
    bool needSecondaryEncode = false;
    if (hasDualFormat) {
        int targetFps = Application::instance().config().get<int>("application.target_fps", 60);
        int secFps    = m_config.secondaryFps > 0 ? m_config.secondaryFps : (m_config.fps > 0 ? m_config.fps : 30);
        int skip      = (targetFps > secFps) ? (targetFps / secFps) : 1;
        m_secondaryEncodeFrameCounter++;
        if (m_secondaryEncodeFrameCounter >= skip) {
            m_secondaryEncodeFrameCounter = 0;
            needSecondaryEncode = true;
        }
    }

    // Skip the entire GPU render when nothing needs a fresh frame
    // (headless mode, no encoder, and JPEG preview not yet due).
    if (!needEncode && !needSecondaryEncode && !windowActive && !needJpeg) {
        return;
    }
    if (needJpeg) m_jpegFrameCounter = 0;

    // ── Primary render ───────────────────────────────────────────────────
    ensureRenderTexture();
    m_renderTexture.clear(sf::Color(15, 15, 25));

    if (m_gameManager->activeGame()) {
        m_gameManager->render(m_renderTexture, alpha);
    }

    if (m_voteState.active) {
        renderVoteOverlay();
    }

    // Always draw the global scoreboard overlay
    renderGlobalScoreboard();

    m_renderTexture.display();

    // GPU→CPU readback: encoder needs it on encode frames, JPEG periodically
    if (needEncode || needJpeg) {
        m_frameCapture = m_renderTexture.getTexture().copyToImage();
    }
    if (needJpeg) {
        updateJpegBuffer();
    }
    m_frameReadyForEncoder = needEncode;

    // ── Secondary render (dual-format) ───────────────────────────────────
    if (needSecondaryEncode) {
        ensureSecondaryRenderTexture();
        m_secondaryRenderTexture.clear(sf::Color(15, 15, 25));

        if (m_gameManager->activeGame()) {
            m_gameManager->render(m_secondaryRenderTexture, alpha);
        }

        if (m_voteState.active) {
            renderSecondaryVoteOverlay();
        }

        renderSecondaryGlobalScoreboard();

        m_secondaryRenderTexture.display();
        m_secondaryFrameCapture = m_secondaryRenderTexture.getTexture().copyToImage();
        m_secondaryFrameReadyForEncoder = true;
    }
}

void StreamInstance::encodeFrame() {
    // Primary encoders
    if (!m_encoders.empty() && m_frameReadyForEncoder) {
        m_frameReadyForEncoder = false;

        const sf::Uint8* pixels = getFrameBuffer();
        if (pixels) {
            bool anyFailed = false;
            for (auto& [chId, enc] : m_encoders) {
                if (enc && enc->isRunning()) {
                    enc->encodeFrame(pixels);
                }
                if (enc && enc->hasFailed()) {
                    spdlog::error("[Stream '{}'] Encoder for channel '{}' failed – removing.",
                                  m_config.name, chId);
                    enc->stop();
                    enc.reset();
                    anyFailed = true;
                }
            }
            if (anyFailed) {
                std::erase_if(m_encoders, [](const auto& p) { return !p.second; });
            }
        }
    }

    // Secondary encoders (dual-format)
    if (!m_secondaryEncoders.empty() && m_secondaryFrameReadyForEncoder) {
        m_secondaryFrameReadyForEncoder = false;

        const sf::Uint8* secPixels = (m_secondaryFrameCapture.getSize().x > 0)
            ? m_secondaryFrameCapture.getPixelsPtr() : nullptr;
        if (secPixels) {
            bool anyFailed = false;
            for (auto& [chId, enc] : m_secondaryEncoders) {
                if (enc && enc->isRunning()) {
                    enc->encodeFrame(secPixels);
                }
                if (enc && enc->hasFailed()) {
                    spdlog::error("[Stream '{}'] Secondary encoder for channel '{}' failed – removing.",
                                  m_config.name, chId);
                    enc->stop();
                    enc.reset();
                    anyFailed = true;
                }
            }
            if (anyFailed) {
                std::erase_if(m_secondaryEncoders, [](const auto& p) { return !p.second; });
            }
        }
    }
}

const sf::Uint8* StreamInstance::getFrameBuffer() const {
    if (m_frameCapture.getSize().x == 0) return nullptr;
    return m_frameCapture.getPixelsPtr();
}

// ── JPEG encoding for web preview ────────────────────────────────────────────

static void stbiWriteCallback(void* context, void* data, int size) {
    auto* buf = static_cast<std::vector<uint8_t>*>(context);
    auto* bytes = static_cast<const uint8_t*>(data);
    buf->insert(buf->end(), bytes, bytes + size);
}

void StreamInstance::updateJpegBuffer() {
    const auto* pixels = m_frameCapture.getPixelsPtr();
    if (!pixels) return;

    std::vector<uint8_t> buf;
    buf.reserve(width() * height() / 4); // rough estimate
    stbi_write_jpg_to_func(stbiWriteCallback, &buf,
                           width(), height(), 4, pixels, 50);

    std::lock_guard<std::mutex> lock(m_jpegMutex);
    m_jpegBuffer = std::move(buf);
}

std::vector<uint8_t> StreamInstance::getJpegFrame() const {
    std::lock_guard<std::mutex> lock(m_jpegMutex);
    return m_jpegBuffer;
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
                    // Cache display names so we don't create game objects per frame
                    m_voteDisplayNames.clear();
                    for (const auto& gid : getAvailableGameIds()) {
                        auto tmp = games::GameRegistry::instance().create(gid);
                        m_voteDisplayNames[gid] = tmp ? tmp->displayName() : gid;
                    }
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

    // Reject vote for disabled games
    auto available = getAvailableGameIds();
    if (std::find(available.begin(), available.end(), gameName) == available.end()) return;

    // Reject vote for games exceeding player limit
    auto limitIt = m_config.gamePlayerLimits.find(gameName);
    if (limitIt != m_config.gamePlayerLimits.end() && limitIt->second > 0) {
        if (m_stats.uniqueViewerCount() > limitIt->second) return;
    }

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
    auto allGames = games::GameRegistry::instance().list();

    // Filter by enabledGames if configured
    if (!m_config.enabledGames.empty()) {
        std::vector<std::string> filtered;
        for (const auto& id : allGames) {
            if (std::find(m_config.enabledGames.begin(),
                          m_config.enabledGames.end(), id) != m_config.enabledGames.end()) {
                filtered.push_back(id);
            }
        }
        return filtered.empty() ? allGames : filtered;
    }

    return allGames;
}

// ── Vote overlay rendering ───────────────────────────────────────────────────

void StreamInstance::renderVoteOverlay() {
    renderVoteOverlayTo(m_renderTexture);
}

void StreamInstance::renderSecondaryVoteOverlay() {
    renderVoteOverlayTo(m_secondaryRenderTexture);
}

void StreamInstance::renderVoteOverlayTo(sf::RenderTexture& target) {
    if (!m_fontLoaded) return;
    float w = static_cast<float>(target.getSize().x);
    float h = static_cast<float>(target.getSize().y);
    float vfs = m_config.voteOverlayFontScale;
    auto fs = [vfs](int base) -> unsigned int {
        return static_cast<unsigned int>(std::max(1.0f, base * vfs));
    };

    // Dim background
    sf::RectangleShape overlay(sf::Vector2f(w, h));
    overlay.setFillColor(sf::Color(0, 0, 0, 180));
    target.draw(overlay);

    // Title
    sf::Text title;
    title.setFont(m_font);
    title.setString("VOTE FOR NEXT GAME!");
    title.setCharacterSize(fs(36));
    title.setFillColor(sf::Color(255, 215, 0));
    title.setStyle(sf::Text::Bold);
    auto tb = title.getLocalBounds();
    title.setPosition((w - tb.width) / 2.0f, h * 0.15f);
    target.draw(title);

    // Timer
    float remaining = static_cast<float>(m_voteState.duration - m_voteState.timer);
    sf::Text timer;
    timer.setFont(m_font);
    timer.setString("Time: " + std::to_string(static_cast<int>(remaining)) + "s");
    timer.setCharacterSize(fs(24));
    timer.setFillColor(sf::Color(200, 200, 200));
    auto tmb = timer.getLocalBounds();
    timer.setPosition((w - tmb.width) / 2.0f, h * 0.22f);
    target.draw(timer);

    // Instructions
    sf::Text instr;
    instr.setFont(m_font);
    instr.setString("Type !vote <game_id> in chat");
    instr.setCharacterSize(fs(18));
    instr.setFillColor(sf::Color(150, 150, 150));
    auto ib = instr.getLocalBounds();
    instr.setPosition((w - ib.width) / 2.0f, h * 0.27f);
    target.draw(instr);

    // Game cards – filter by player limit
    auto allGameIds = getAvailableGameIds();
    int viewers = m_stats.uniqueViewerCount();
    std::vector<std::string> gameIds;
    for (const auto& gid : allGameIds) {
        auto limitIt = m_config.gamePlayerLimits.find(gid);
        if (limitIt != m_config.gamePlayerLimits.end() && limitIt->second > 0) {
            if (viewers > limitIt->second) continue; // too many viewers
        }
        gameIds.push_back(gid);
    }

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
        target.draw(card);

        // Use cached display name to avoid creating full game objects per frame
        std::string dispName = gid;
        auto nameIt = m_voteDisplayNames.find(gid);
        if (nameIt != m_voteDisplayNames.end()) {
            dispName = nameIt->second;
        }

        sf::Text nameT;
        nameT.setFont(m_font);
        nameT.setString(dispName);
        nameT.setCharacterSize(fs(20));
        nameT.setFillColor(sf::Color::White);
        nameT.setPosition(cardX + 15.0f, y + 8.0f);
        target.draw(nameT);

        sf::Text voteT;
        voteT.setFont(m_font);
        voteT.setString(std::to_string(votes) + " votes");
        voteT.setCharacterSize(fs(16));
        voteT.setFillColor(sf::Color(88, 166, 255));
        voteT.setPosition(cardX + 15.0f, y + 34.0f);
        target.draw(voteT);

        sf::Text idT;
        idT.setFont(m_font);
        idT.setString("!vote " + gid);
        idT.setCharacterSize(fs(14));
        idT.setFillColor(sf::Color(120, 120, 140));
        auto idb = idT.getLocalBounds();
        idT.setPosition(cardX + cardW - idb.width - 15.0f, y + 20.0f);
        target.draw(idT);

        y += cardH + 10.0f;
    }
}

// ── Global scoreboard overlay ────────────────────────────────────────────────

void StreamInstance::updateScoreboardCache() {
    // Re-read global scoreboard config from Application
    m_sbConfig = Application::instance().scoreboardConfig();

    auto& db = Application::instance().playerDatabase();
    const auto& hidden = m_sbConfig.hiddenPlayers;

    // Determine what caches are needed based on configured panels
    bool needPlayerAlltime  = false, needPlayerRecent  = false;
    bool needCountryAlltime = false, needCountryRecent = false;
    int maxPlayerTopN = 5, maxCountryTopN = 5;

    for (const auto& p : m_sbConfig.panels) {
        if (!p.enabled || p.durationSecs <= 0.0) continue;
        if (p.contentType == "countries") {
            if (p.timeRange == "alltime") { needCountryAlltime = true; maxCountryTopN = std::max(maxCountryTopN, p.topN); }
            if (p.timeRange == "recent")  { needCountryRecent  = true; maxCountryTopN = std::max(maxCountryTopN, p.topN); }
        } else {
            if (p.timeRange == "alltime") { needPlayerAlltime = true; maxPlayerTopN = std::max(maxPlayerTopN, p.topN); }
            if (p.timeRange == "recent")  { needPlayerRecent  = true; maxPlayerTopN = std::max(maxPlayerTopN, p.topN); }
        }
    }

    if (needPlayerAlltime)
        m_playerAlltimeCache = db.getTopAllTimeFiltered(maxPlayerTopN, hidden);
    else
        m_playerAlltimeCache.clear();

    if (needPlayerRecent)
        m_playerRecentCache = db.getTopRecentFiltered(maxPlayerTopN, m_sbConfig.recentHours, hidden);
    else
        m_playerRecentCache.clear();

    if (needCountryAlltime)
        m_countryAlltimeCache = db.getTopCountriesAllTime(maxCountryTopN);
    else
        m_countryAlltimeCache.clear();

    if (needCountryRecent)
        m_countryRecentCache = db.getTopCountriesRecent(maxCountryTopN, m_sbConfig.recentHours);
    else
        m_countryRecentCache.clear();

    // Load flag textures on-demand if any country panel is configured
    if ((needCountryAlltime || needCountryRecent ||
         std::any_of(m_sbConfig.panels.begin(), m_sbConfig.panels.end(),
                     [](const auto& p) { return p.contentType == "countries" && p.timeRange == "round"; }))
        && !m_flagTexturesLoaded) {
        loadFlagTextures();
    }
}

void StreamInstance::rebuildScoreboardPanels() {
    m_panelGroups.clear();

    for (size_t i = 0; i < m_sbConfig.panels.size(); ++i) {
        const auto& p = m_sbConfig.panels[i];
        if (!p.enabled || p.durationSecs <= 0.0) continue;

        // Check if this panel has data
        bool hasData = false;
        if (p.contentType == "countries") {
            if (p.timeRange == "alltime") hasData = !m_countryAlltimeCache.empty();
            else if (p.timeRange == "recent") hasData = !m_countryRecentCache.empty();
            else if (p.timeRange == "round") hasData = !m_countryRoundCache.empty();
        } else {
            if (p.timeRange == "alltime") hasData = !m_playerAlltimeCache.empty();
            else if (p.timeRange == "recent") hasData = !m_playerRecentCache.empty();
            else if (p.timeRange == "round") hasData = !m_playerRoundCache.empty();
        }
        if (!hasData) continue;

        m_panelGroups[p.group].panelIndices.push_back(i);
    }

    // Clamp current indices
    for (auto& [groupId, grp] : m_panelGroups) {
        if (grp.panelIndices.empty()) {
            grp.currentIndex = 0;
        } else if (grp.currentIndex >= static_cast<int>(grp.panelIndices.size())) {
            grp.currentIndex = 0;
        }
    }
}

double StreamInstance::currentPanelDuration() const {
    // Legacy fallback — returns duration of first group's current panel
    for (const auto& [groupId, grp] : m_panelGroups) {
        if (grp.panelIndices.empty()) continue;
        size_t idx = grp.panelIndices[static_cast<size_t>(
            grp.currentIndex % static_cast<int>(grp.panelIndices.size()))];
        if (idx < m_sbConfig.panels.size())
            return m_sbConfig.panels[idx].durationSecs;
    }
    return 10.0;
}

// ── Flag sprite order (same as CountryElimination) ───────────────────────────
static const char* SB_SPRITE_ORDER[] = {
    "DZ","AO","BJ","BW","BF","BI","CM","CV","CF","TD",
    "CD","DJ","EG","GQ","ER","ET","GA","GM","GH","GN",
    "GW","CI","KE","LS","LR","LY","MG","MW","ML","MR",
    "MU","YT","MA","MZ","NA","NE","NG","CG","RE","RW",
    "SH","ST","SN","SC","SL","SO","ZA","SS","SD","SR",
    "SZ","TG","TN","UG","TZ","EH","YE","ZM","ZW",
    "AI","AG","AR","AW","BS","BB","BQ","BZ","BM","BO",
    "VG","BR","CA","KY","CL","CO","KM","CR","CU","CW",
    "DM","DO","EC","SV","FK","GF","GL","GD","GP","GT",
    "GY","HT","HN","JM","MQ","MX","MS","NI","PA","PY",
    "PE","PR","BL","KN","LC","PM","VC","SX","TT","TC",
    "US","VI","UY","VE",
    "AB","AF","AZ","BD","BT","BN","KH","CN","GE","HK",
    "IN","ID","JP","KZ","LA","MO","MY","MV","MN","MM",
    "NP","KP","MP","PW","PG","PH","SG","KR","LK","TW",
    "TJ","TH","TL","TM","VN",
    "AX","AL","AD","AM","AT","BY","BE","BA","BG","HR",
    "CY","CZ","DK","EE","FO","FI","FR","DE","GI","GR",
    "GG","HU","IS","IE","IM","IT","JE","XK","LV","LI",
    "LT","LU","MT","MD","MC","ME","NL","MK","NO","PL",
    "PT","RO","RU","SM","RS","SK","SI","ES","SE","CH",
    "TR","UA","GB","VA",
    "BH","IR","IQ","IL","KW","JO","KG","LB","OM","PK",
    "PS","QA","SA","SY","AE","UZ",
    "AS","AU","CX","CC","CK","FJ","PF","GU","KI","MH",
    "FM","NC","NZ","NR","NU","NF","WS","SB","TK","TO",
    "TV","VU","WF",
    "AQ","EU","JR","OLY","UN",
};
static constexpr int SB_NUM_SPRITE_FLAGS = sizeof(SB_SPRITE_ORDER) / sizeof(SB_SPRITE_ORDER[0]);

void StreamInstance::loadFlagTextures() {
    if (m_flagTexturesLoaded) return;
    m_flagTexturesLoaded = true;

    sf::Image spriteSheet;
    if (!spriteSheet.loadFromFile("assets/img/flagSprite60.png")) {
        spdlog::warn("[StreamInstance] Could not load flag sprite sheet");
        return;
    }

    unsigned int sheetW = spriteSheet.getSize().x;
    unsigned int sheetH = spriteSheet.getSize().y;

    // Auto-detect flag height by finding the first fully-transparent separator row
    unsigned int flagStride = 0;
    for (unsigned int y = 1; y < std::min(sheetH, 200u); ++y) {
        bool isTransparent = true;
        for (unsigned int x = 0; x < sheetW; ++x) {
            if (spriteSheet.getPixel(x, y).a != 0) { isTransparent = false; break; }
        }
        if (isTransparent) { flagStride = y + 1; break; }
    }
    if (flagStride == 0) flagStride = sheetH / SB_NUM_SPRITE_FLAGS;
    unsigned int flagH = flagStride - 1;
    unsigned int totalSlots = sheetH / flagStride;

    // Detect if first slot is a black header (skip it)
    unsigned int startSlot = 0;
    bool firstIsBlack = true;
    for (unsigned int x = 0; x < sheetW; ++x) {
        sf::Color c = spriteSheet.getPixel(x, 0);
        if (c.r != 0 || c.g != 0 || c.b != 0) { firstIsBlack = false; break; }
    }
    if (firstIsBlack && totalSlots > static_cast<unsigned int>(SB_NUM_SPRITE_FLAGS))
        startSlot = 1;

    int loaded = 0;
    for (int i = 0; i < SB_NUM_SPRITE_FLAGS; ++i) {
        unsigned int slot = startSlot + static_cast<unsigned int>(i);
        if (slot >= totalSlots) break;
        unsigned int y = slot * flagStride;
        sf::IntRect area(0, static_cast<int>(y), static_cast<int>(sheetW), static_cast<int>(flagH));
        m_flagTextures[SB_SPRITE_ORDER[i]].loadFromImage(spriteSheet, area);
        m_flagTextures[SB_SPRITE_ORDER[i]].setSmooth(true);
        ++loaded;
    }

    spdlog::info("[StreamInstance] Loaded {} flag textures for scoreboard", loaded);
}

// ── Hex color helper ─────────────────────────────────────────────────────────
static sf::Color hexToColor(const std::string& hex, uint8_t alpha = 255) {
    if (hex.size() >= 7 && hex[0] == '#') {
        unsigned int r = std::stoul(hex.substr(1, 2), nullptr, 16);
        unsigned int g = std::stoul(hex.substr(3, 2), nullptr, 16);
        unsigned int b = std::stoul(hex.substr(5, 2), nullptr, 16);
        return sf::Color(static_cast<uint8_t>(r), static_cast<uint8_t>(g),
                         static_cast<uint8_t>(b), alpha);
    }
    return sf::Color(170, 170, 190, alpha); // fallback
}

void StreamInstance::renderGlobalScoreboard() {
    renderGlobalScoreboardTo(m_renderTexture);
}

void StreamInstance::renderSecondaryGlobalScoreboard() {
    renderGlobalScoreboardTo(m_secondaryRenderTexture);
}

void StreamInstance::renderGlobalScoreboardTo(sf::RenderTexture& target) {
    if (!m_fontLoaded) return;
    if (m_panelGroups.empty()) return;

    float w = static_cast<float>(target.getSize().x);
    float h = static_cast<float>(target.getSize().y);

    // Render one panel per group simultaneously
    for (const auto& [groupId, grp] : m_panelGroups) {
        if (grp.panelIndices.empty()) continue;
        size_t panelIdx = grp.panelIndices[static_cast<size_t>(
            grp.currentIndex % static_cast<int>(grp.panelIndices.size()))];
        if (panelIdx >= m_sbConfig.panels.size()) continue;
        const auto& pc = m_sbConfig.panels[panelIdx];

        int baseFontSize   = pc.fontSize;
        int headerFontSize = baseFontSize + 2;
        float padX   = 12.0f;
        float padY   = 8.0f;
        float panelW = w * (pc.boxWidthPct / 100.0f);
        float headerH = headerFontSize * 1.8f;

        // Compute line height — account for flag/avatar size to prevent overlap
        float flagDiameter = 0.0f;
        if (pc.contentType == "countries" && pc.showFlags) {
            flagDiameter = baseFontSize * pc.flagSize;
        }
        float textLineH = baseFontSize * 1.6f;
        float lineH = std::max(textLineH, flagDiameter + 4.0f);

        float baseAlpha = std::clamp(pc.opacity, 0.0f, 1.0f);

        // Compute crossfade alpha for this group
        float fadeAlpha = 1.0f;
        if (grp.panelIndices.size() > 1) {
            double fadeSecs  = m_sbConfig.fadeSecs;
            double cycleSecs = pc.durationSecs;
            if (fadeSecs > 0.0 && cycleSecs > 0.0) {
                double t = grp.cycleTimer;
                if (t < fadeSecs) {
                    fadeAlpha = static_cast<float>(t / fadeSecs);
                } else if (t > cycleSecs) {
                    fadeAlpha = static_cast<float>(1.0 - (t - cycleSecs) / fadeSecs);
                }
            }
        }
        uint8_t alpha = static_cast<uint8_t>(255.0f * baseAlpha * fadeAlpha);

        sf::Color bgCol     = hexToColor(pc.bgColor, static_cast<uint8_t>(180 * baseAlpha * fadeAlpha));
        sf::Color borderCol = hexToColor(pc.borderColor, static_cast<uint8_t>(150 * baseAlpha * fadeAlpha));
        sf::Color titleCol  = hexToColor(pc.titleColor, alpha);
        sf::Color ptsCol    = hexToColor(pc.pointsColor, alpha);
        sf::Color nameCol   = hexToColor(pc.nameColor, alpha);
        sf::Color goldCol   = hexToColor(pc.goldColor, alpha);
        sf::Color silverCol = hexToColor(pc.silverColor, alpha);
        sf::Color bronzeCol = hexToColor(pc.bronzeColor, alpha);

        auto nameColorForRank = [&](int rank) -> sf::Color {
            if (rank == 1) return goldCol;
            if (rank == 2) return silverCol;
            if (rank == 3) return bronzeCol;
            return nameCol;
        };

        // Collect entries as pairs (name, value) with optional country code
        struct DisplayEntry {
            std::string name;
            std::string code; // country code for flag lookup (empty for players)
            int value = 0;
        };
        std::vector<DisplayEntry> entries;

        if (pc.contentType == "countries") {
            const auto& countryNames = is::games::country_elimination::getCountryDisplayNames();
            auto resolveName = [&](const std::string& code) -> std::string {
                auto it = countryNames.find(code);
                return (it != countryNames.end()) ? it->second : code;
            };
            if (pc.timeRange == "alltime") {
                for (const auto& e : m_countryAlltimeCache)
                    entries.push_back({resolveName(e.countryCode), e.countryCode, e.wins});
            } else if (pc.timeRange == "recent") {
                for (const auto& e : m_countryRecentCache)
                    entries.push_back({resolveName(e.countryCode), e.countryCode, e.wins});
            } else if (pc.timeRange == "round") {
                for (const auto& [code, wins] : m_countryRoundCache)
                    entries.push_back({resolveName(code), code, wins});
            }
        } else {
            if (pc.timeRange == "alltime") {
                for (const auto& e : m_playerAlltimeCache)
                    entries.push_back({e.displayName, {}, e.points});
            } else if (pc.timeRange == "recent") {
                for (const auto& e : m_playerRecentCache)
                    entries.push_back({e.displayName, {}, e.points});
            } else if (pc.timeRange == "round") {
                for (const auto& [name, score] : m_playerRoundCache)
                    entries.push_back({name, {}, score});
            }
        }

        if (entries.empty()) continue;

        int count = std::min(static_cast<int>(entries.size()), pc.topN);
        float panelH = headerH + static_cast<float>(count) * lineH + padY * 2.0f;

        // Compute panel position based on alignment
        float offsetX = w * (pc.posXPct / 100.0f);
        float offsetY = h * (pc.posYPct / 100.0f);
        float panelX, panelY;
        if (pc.alignX == "right")       panelX = w - panelW - offsetX;
        else if (pc.alignX == "center") panelX = (w - panelW) / 2.0f + offsetX;
        else                            panelX = offsetX; // "left"
        if (pc.alignY == "bottom")      panelY = h - panelH - offsetY;
        else if (pc.alignY == "center") panelY = (h - panelH) / 2.0f + offsetY;
        else                            panelY = offsetY; // "top"

        // Background
        sf::RectangleShape bg(sf::Vector2f(panelW, panelH));
        bg.setPosition(panelX, panelY);
        bg.setFillColor(bgCol);
        bg.setOutlineColor(borderCol);
        bg.setOutlineThickness(1.5f);
        target.draw(bg);

        // Title
        sf::Text header;
        header.setFont(m_font);
        header.setString(pc.title);
        header.setCharacterSize(static_cast<unsigned int>(headerFontSize));
        header.setFillColor(titleCol);
        header.setStyle(sf::Text::Bold);
        auto hb = header.getLocalBounds();
        header.setPosition(panelX + (panelW - hb.width) / 2.0f, panelY + padY);
        target.draw(header);

        // Entries
        float y = panelY + padY + headerH;
        int rank = 1;
        for (const auto& entry : entries) {
            if (rank > pc.topN) break;

            float contentX = panelX + padX;
            float textY = y + (lineH - textLineH) / 2.0f; // vertically center text in line

            // Draw flag for country panels
            if (pc.contentType == "countries" && pc.showFlags && !entry.code.empty()) {
                float flagR = flagDiameter / 2.0f;
                auto it = m_flagTextures.find(entry.code);
                if (it != m_flagTextures.end()) {
                    if (pc.flagShape == "rect") {
                        auto texSize = it->second.getSize();
                        float aspect = (texSize.y > 0) ? static_cast<float>(texSize.x) / texSize.y : 1.5f;
                        float rectH = flagDiameter;
                        float rectW = rectH * aspect;
                        sf::RectangleShape flagRect(sf::Vector2f(rectW, rectH));
                        flagRect.setTexture(&it->second);
                        flagRect.setPosition(contentX, y + (lineH - rectH) / 2.0f);
                        flagRect.setFillColor(sf::Color(255, 255, 255, alpha));
                        target.draw(flagRect);
                        contentX += rectW + 6.0f;
                    } else {
                        sf::CircleShape flagCircle(flagR);
                        flagCircle.setTexture(&it->second);
                        auto texSize = it->second.getSize();
                        int sq = static_cast<int>(std::min(texSize.x, texSize.y));
                        int ox = static_cast<int>((texSize.x - sq) / 2);
                        flagCircle.setTextureRect(sf::IntRect(ox, 0, sq, sq));
                        flagCircle.setPosition(contentX, y + (lineH - flagDiameter) / 2.0f);
                        flagCircle.setFillColor(sf::Color(255, 255, 255, alpha));
                        target.draw(flagCircle);
                        contentX += flagDiameter + 6.0f;
                    }
                } else {
                    // Fallback: colored circle
                    sf::CircleShape dot(flagR);
                    dot.setFillColor(nameColorForRank(rank));
                    dot.setPosition(contentX, y + (lineH - flagDiameter) / 2.0f);
                    target.draw(dot);
                    contentX += flagDiameter + 6.0f;
                }
            }

            // Build display text
            std::string displayText = std::to_string(rank) + ". ";
            if (pc.contentType == "countries") {
                if (pc.showCodes && !entry.code.empty()) displayText += entry.code;
                if (pc.showNames && !entry.name.empty()) {
                    if (pc.showCodes) displayText += " ";
                    displayText += entry.name;
                }
                if (!pc.showCodes && !pc.showNames && !entry.code.empty())
                    displayText += entry.code; // fallback: show code
            } else {
                displayText += entry.name;
            }

            // Pre-compute value text position (right-aligned in panel)
            std::string valStr = std::to_string(entry.value) + (pc.valueLabel.empty() ? "" : " " + pc.valueLabel);
            sf::Text ptsText;
            ptsText.setFont(m_font);
            ptsText.setString(valStr);
            ptsText.setCharacterSize(static_cast<unsigned int>(baseFontSize));
            auto pb = ptsText.getLocalBounds();
            float valX = panelX + panelW - pb.width - pb.left - padX;

            if (pc.showNames || pc.contentType == "players") {
                sf::Text nameText;
                nameText.setFont(m_font);
                nameText.setString(displayText);
                nameText.setCharacterSize(static_cast<unsigned int>(baseFontSize));
                nameText.setFillColor(nameColorForRank(rank));
                nameText.setPosition(contentX, textY);

                // Truncate name text if it would overlap the value column
                float maxNameW = valX - contentX - 6.0f;
                if (maxNameW > 0) {
                    auto nb = nameText.getLocalBounds();
                    if (nb.width > maxNameW) {
                        // Binary-search-style truncation: trim characters + add ellipsis
                        sf::String s = nameText.getString();
                        while (s.getSize() > 1) {
                            s = s.substring(0, s.getSize() - 1);
                            nameText.setString(s + "...");
                            if (nameText.getLocalBounds().width <= maxNameW) break;
                        }
                    }
                }
                target.draw(nameText);
            }

            // Draw value text (right-aligned)
            ptsText.setFillColor(ptsCol);
            ptsText.setPosition(valX, textY);
            target.draw(ptsText);

            y += lineH;
            rank++;
        }
    }
}

void StreamInstance::sendPeriodicInfoMessage() {
    if (m_lastGameId.empty()) return;

    auto msgIt = m_config.gameInfoMessages.find(m_lastGameId);
    if (msgIt == m_config.gameInfoMessages.end() || msgIt->second.empty()) return;

    auto& cm = Application::instance().channelManager();

    // Send to this stream's subscribed channels only
    for (const auto& chId : m_config.channelIds) {
        cm.sendMessageToChannel(chId, msgIt->second);
    }

    spdlog::debug("[Stream '{}'] Sent info message for game '{}'", m_config.name, m_lastGameId);
}

void StreamInstance::sendScoreboardToChat() {
    auto& cm = Application::instance().channelManager();

    // Find the first group's current panel and post its data
    std::string msg;
    for (const auto& [groupId, grp] : m_panelGroups) {
        if (grp.panelIndices.empty()) continue;
        size_t idx = grp.panelIndices[static_cast<size_t>(
            grp.currentIndex % static_cast<int>(grp.panelIndices.size()))];
        if (idx >= m_sbConfig.panels.size()) continue;
        const auto& pc = m_sbConfig.panels[idx];

        std::string title = pc.title;
        std::string chatMsg = "🏆 " + title + ": ";
        int rank = 1;

        if (pc.contentType == "countries") {
            const auto* data = (pc.timeRange == "alltime") ? &m_countryAlltimeCache
                             : (pc.timeRange == "recent")  ? &m_countryRecentCache
                             : nullptr;
            if (data && !data->empty()) {
                for (const auto& e : *data) {
                    if (rank > 1) chatMsg += " | ";
                    chatMsg += "#" + std::to_string(rank) + " " + e.countryCode
                             + " (" + std::to_string(e.wins) + (pc.valueLabel.empty() ? "" : " " + pc.valueLabel) + ")";
                    if (++rank > 5) break;
                }
                msg = chatMsg;
            } else if (pc.timeRange == "round" && !m_countryRoundCache.empty()) {
                for (const auto& [code, wins] : m_countryRoundCache) {
                    if (rank > 1) chatMsg += " | ";
                    chatMsg += "#" + std::to_string(rank) + " " + code
                             + " (" + std::to_string(wins) + (pc.valueLabel.empty() ? "" : " " + pc.valueLabel) + ")";
                    if (++rank > 5) break;
                }
                msg = chatMsg;
            }
        } else {
            if (pc.timeRange == "alltime" && !m_playerAlltimeCache.empty()) {
                for (const auto& e : m_playerAlltimeCache) {
                    if (rank > 1) chatMsg += " | ";
                    chatMsg += "#" + std::to_string(rank) + " " + e.displayName
                             + " (" + std::to_string(e.points) + (pc.valueLabel.empty() ? "" : " " + pc.valueLabel) + ")";
                    if (++rank > 5) break;
                }
                msg = chatMsg;
            } else if (pc.timeRange == "recent" && !m_playerRecentCache.empty()) {
                for (const auto& e : m_playerRecentCache) {
                    if (rank > 1) chatMsg += " | ";
                    chatMsg += "#" + std::to_string(rank) + " " + e.displayName
                             + " (" + std::to_string(e.points) + (pc.valueLabel.empty() ? "" : " " + pc.valueLabel) + ")";
                    if (++rank > 5) break;
                }
                msg = chatMsg;
            } else if (pc.timeRange == "round" && !m_playerRoundCache.empty()) {
                for (const auto& [name, score] : m_playerRoundCache) {
                    if (rank > 1) chatMsg += " | ";
                    chatMsg += "#" + std::to_string(rank) + " " + name
                             + " (" + std::to_string(score) + (pc.valueLabel.empty() ? "" : " " + pc.valueLabel) + ")";
                    if (++rank > 5) break;
                }
                msg = chatMsg;
            }
        }
        if (!msg.empty()) break; // post first group with data
    }

    if (msg.empty()) return;

    for (const auto& chId : m_config.channelIds) {
        cm.sendMessageToChannel(chId, msg);
    }

    spdlog::debug("[Stream '{}'] Sent scoreboard to chat", m_config.name);
}

// ── Platform info update (Twitch / YouTube) ───────────────────────────────────

/// Replace placeholders like {game_name}, {game_id}, {stream_name}, {player_count}
static std::string resolvePlaceholders(const std::string& text,
                                        const std::string& gameId,
                                        const std::string& gameName,
                                        const std::string& streamName,
                                        int playerCount) {
    if (text.empty()) return text;
    std::string result = text;
    auto replaceAll = [&](const std::string& from, const std::string& to) {
        size_t pos = 0;
        while ((pos = result.find(from, pos)) != std::string::npos) {
            result.replace(pos, from.length(), to);
            pos += to.length();
        }
    };
    replaceAll("{game_name}", gameName);
    replaceAll("{game_id}", gameId);
    replaceAll("{stream_name}", streamName);
    replaceAll("{player_count}", std::to_string(playerCount));
    return result;
}

void StreamInstance::updatePlatformInfo(const std::string& gameId) {
    // Resolve game display name for placeholders
    auto* activeGame = m_gameManager->activeGame();
    std::string gameName = activeGame ? activeGame->displayName() : gameId;
    int playerCount = activeGame
        ? static_cast<int>(activeGame->getLeaderboard().size())
        : 0;

    // Look up per-game platform names from config
    std::string twitchCategory, twitchTitle, youtubeTitle;
    {
        auto it = m_config.gameTwitchCategories.find(gameId);
        if (it != m_config.gameTwitchCategories.end()) twitchCategory = it->second;
    }
    {
        auto it = m_config.gameTwitchTitles.find(gameId);
        if (it != m_config.gameTwitchTitles.end()) twitchTitle = it->second;
    }
    {
        auto it = m_config.gameYoutubeTitles.find(gameId);
        if (it != m_config.gameYoutubeTitles.end()) youtubeTitle = it->second;
    }

    // Fall back to the stream's general title when no per-game title is configured
    if (twitchTitle.empty() && !m_config.title.empty()) twitchTitle = m_config.title;
    if (youtubeTitle.empty() && !m_config.title.empty()) youtubeTitle = m_config.title;

    // Resolve YouTube description: per-game description > stream description
    std::string youtubeDescription;
    {
        auto it = m_config.gameDescriptions.find(gameId);
        if (it != m_config.gameDescriptions.end()) youtubeDescription = it->second;
    }
    if (youtubeDescription.empty() && !m_config.description.empty())
        youtubeDescription = m_config.description;

    // Resolve placeholders in all text fields
    twitchTitle = resolvePlaceholders(twitchTitle, gameId, gameName, m_config.name, playerCount);
    youtubeTitle = resolvePlaceholders(youtubeTitle, gameId, gameName, m_config.name, playerCount);
    youtubeDescription = resolvePlaceholders(youtubeDescription, gameId, gameName, m_config.name, playerCount);

    // If no platform names configured, nothing to do
    if (twitchCategory.empty() && twitchTitle.empty() && youtubeTitle.empty()) {
        spdlog::debug("[Stream '{}'] No Twitch/YouTube category or title configured for game '{}' – "
                      "skipping platform info update. Configure these in the stream settings.",
                      m_config.name, gameId);
        return;
    }

    auto& cm = Application::instance().channelManager();

    // Update Twitch channels subscribed to this stream
    if (!twitchCategory.empty() || !twitchTitle.empty()) {
        for (const auto& chId : m_config.channelIds) {
            const auto* cfg = cm.getChannelConfig(chId);
            if (!cfg || cfg->platform != "twitch") continue;

            // Read client_id from channel settings, fallback to global config
            std::string clientId = cfg->settings.value("client_id", "");
            if (clientId.empty()) {
                clientId = Application::instance().config().get<std::string>("twitch.client_id", "");
            }

            std::string token = cfg->settings.value("oauth_token", "");
            if (token.empty() || clientId.empty()) {
                spdlog::warn("[Stream '{}'] Twitch channel '{}' missing oauth_token or client_id – "
                             "cannot update stream info. Set client_id in channel settings.",
                             m_config.name, chId);
                continue;
            }

            // Check cached broadcaster ID (avoid blocking main thread for cache hit)
            std::string cachedBroadcasterId;
            auto brIt = m_twitchBroadcasterIdCache.find(chId);
            if (brIt != m_twitchBroadcasterIdCache.end()) {
                cachedBroadcasterId = brIt->second;
            }

            std::string cachedGameId;
            if (!twitchCategory.empty()) {
                auto gmIt = m_twitchGameIdCache.find(twitchCategory);
                if (gmIt != m_twitchGameIdCache.end()) {
                    cachedGameId = gmIt->second;
                }
            }

            // Fire-and-forget – ALL curl calls run in a detached thread to avoid
            // blocking the game loop (getBroadcasterId, getGameId, updateChannelInfo).
            std::string sName = m_config.name;
            std::string cat   = twitchCategory;
            std::string ttl   = twitchTitle;
            std::string channelIdCopy = chId;

            // Capture cache pointers safely via a shared_ptr to this instance's maps
            // We'll write back to the caches on the main thread later – for now the
            // detached thread just resolves IDs if not cached.
            std::thread([this, sName, token, clientId, channelIdCopy, cat, ttl,
                         cachedBroadcasterId, cachedGameId]() {
                // Resolve broadcaster ID
                std::string broadcasterId = cachedBroadcasterId;
                if (broadcasterId.empty()) {
                    spdlog::info("[Stream '{}'] Resolving Twitch broadcaster ID for channel '{}'...",
                                 sName, channelIdCopy);
                    broadcasterId = platform::TwitchApi::getBroadcasterId(token, clientId);
                    if (broadcasterId.empty()) {
                        spdlog::error("[Stream '{}'] Could not resolve broadcaster ID for Twitch channel '{}'. "
                                      "Check that your OAuth token is valid and client_id is correct.",
                                      sName, channelIdCopy);
                        return;
                    }
                    // Write back to cache (single writer, safe for our use case)
                    m_twitchBroadcasterIdCache[channelIdCopy] = broadcasterId;
                    spdlog::info("[Stream '{}'] Resolved broadcaster ID: {}", sName, broadcasterId);
                }

                // Resolve game ID
                std::string twitchGameId = cachedGameId;
                if (!cat.empty() && twitchGameId.empty()) {
                    spdlog::info("[Stream '{}'] Looking up Twitch game ID for category '{}'...",
                                 sName, cat);
                    twitchGameId = platform::TwitchApi::getGameId(token, clientId, cat);
                    if (twitchGameId.empty()) {
                        spdlog::warn("[Stream '{}'] Twitch category '{}' not found – title will still be updated.",
                                     sName, cat);
                    } else {
                        m_twitchGameIdCache[cat] = twitchGameId;
                        spdlog::info("[Stream '{}'] Resolved game ID: {} for '{}'",
                                     sName, twitchGameId, cat);
                    }
                }

                // Update channel info
                spdlog::info("[Stream '{}'] Updating Twitch channel info (title='{}', category='{}', gameId={})...",
                             sName, ttl, cat, twitchGameId);
                bool ok = platform::TwitchApi::updateChannelInfo(
                    token, clientId, broadcasterId, ttl, twitchGameId);
                if (ok) {
                    spdlog::info("[Stream '{}'] Twitch channel updated successfully (title='{}', gameId={}).",
                                 sName, ttl, twitchGameId);
                } else {
                    spdlog::error("[Stream '{}'] Failed to update Twitch channel info.", sName);
                }
            }).detach();
        }
    }

    // Update YouTube channels subscribed to this stream
    // Rate-limit YouTube API calls to conserve quota (min 10 min between updates)
    if (!youtubeTitle.empty() || !youtubeDescription.empty()) {
        // Skip if title+description are identical to what we last sent
        if (youtubeTitle == m_lastYoutubeTitle && youtubeDescription == m_lastYoutubeDescription) {
            spdlog::debug("[Stream '{}'] Skipping YouTube broadcast update — "
                          "title and description unchanged.",
                          m_config.name);
        } else {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - m_lastYoutubeUpdateTime).count();
        if (m_lastYoutubeUpdateTime.time_since_epoch().count() != 0 &&
            elapsed < YOUTUBE_UPDATE_MIN_INTERVAL_SEC) {
            spdlog::debug("[Stream '{}'] Skipping YouTube broadcast update — "
                          "last update was {}s ago (min interval: {}s).",
                          m_config.name, elapsed, YOUTUBE_UPDATE_MIN_INTERVAL_SEC);
        } else {
        m_lastYoutubeUpdateTime = now;
        m_lastYoutubeTitle = youtubeTitle;
        m_lastYoutubeDescription = youtubeDescription;
        for (const auto& chId : m_config.channelIds) {
            const auto* cfg = cm.getChannelConfig(chId);
            if (!cfg || cfg->platform != "youtube") continue;

            std::string oauthToken = cfg->settings.value("oauth_token", "");

            // Prefer the live (possibly refreshed) token from the platform instance
            if (auto* plat = cm.getPlatform(chId)) {
                auto liveSettings = plat->getCurrentSettings();
                if (liveSettings.contains("oauth_token") && !liveSettings["oauth_token"].get<std::string>().empty()) {
                    oauthToken = liveSettings["oauth_token"].get<std::string>();
                }
            }

            if (oauthToken.empty()) {
                spdlog::warn("[Stream '{}'] YouTube channel '{}' missing oauth_token – "
                             "cannot update broadcast info. Set an OAuth token in channel settings.",
                             m_config.name, chId);
                continue;
            }

            // Check cached broadcast ID — first from the platform instance
            // (set by YoutubePlatform during chat auto-detection), then from
            // our own local cache.  This avoids a separate getActiveBroadcastId()
            // API call which costs quota.
            std::string cachedBroadcastId;
            if (auto* plat = cm.getPlatform(chId)) {
                auto liveSettings = plat->getCurrentSettings();
                if (liveSettings.contains("broadcast_id")) {
                    cachedBroadcastId = liveSettings["broadcast_id"].get<std::string>();
                    if (!cachedBroadcastId.empty()) {
                        m_youtubeBroadcastIdCache[chId] = cachedBroadcastId;
                    }
                }
            }
            if (cachedBroadcastId.empty()) {
                auto brIt = m_youtubeBroadcastIdCache.find(chId);
                if (brIt != m_youtubeBroadcastIdCache.end()) {
                    cachedBroadcastId = brIt->second;
                }
            }

            // Fire-and-forget in a detached thread (same pattern as Twitch)
            std::string sName = m_config.name;
            std::string ytTitle = youtubeTitle;
            std::string ytDesc  = youtubeDescription;
            std::string channelIdCopy = chId;
            // YouTube Gaming category ID = "20"
            std::string ytCategoryId = "20";

            std::thread([this, sName, oauthToken, channelIdCopy, ytTitle, ytDesc,
                         ytCategoryId, cachedBroadcastId]() {
                // Resolve broadcast ID
                std::string broadcastId = cachedBroadcastId;
                if (broadcastId.empty()) {
                    spdlog::info("[Stream '{}'] Resolving YouTube active broadcast for channel '{}'...",
                                 sName, channelIdCopy);
                    broadcastId = platform::YouTubeApi::getActiveBroadcastId(oauthToken);
                    if (broadcastId.empty()) {
                        spdlog::error("[Stream '{}'] No active YouTube broadcast found for channel '{}'. "
                                      "Make sure you are currently live and the OAuth token has the "
                                      "youtube.force-ssl scope.",
                                      sName, channelIdCopy);
                        return;
                    }
                    // Write back to cache
                    m_youtubeBroadcastIdCache[channelIdCopy] = broadcastId;
                    spdlog::info("[Stream '{}'] Resolved YouTube broadcast ID: {}", sName, broadcastId);
                }

                // Update broadcast info
                spdlog::info("[Stream '{}'] Updating YouTube broadcast (title='{}', description='{}', category={})...",
                             sName, ytTitle, ytDesc.substr(0, 50), ytCategoryId);
                bool ok = platform::YouTubeApi::updateBroadcast(
                    oauthToken, broadcastId, ytTitle, ytDesc, ytCategoryId);
                if (ok) {
                    spdlog::info("[Stream '{}'] YouTube broadcast updated successfully (title='{}').",
                                 sName, ytTitle);
                } else {
                    spdlog::error("[Stream '{}'] Failed to update YouTube broadcast info.", sName);
                    // Clear cached broadcast ID on failure so next attempt re-resolves
                    m_youtubeBroadcastIdCache.erase(channelIdCopy);
                }
            }).detach();
        }
        } // else: rate-limited
        } // else: unchanged
    }
}

// ── Streaming control ────────────────────────────────────────────────────────

bool StreamInstance::isStreaming() const {
    for (const auto& [chId, enc] : m_encoders) {
        if (enc && enc->isRunning()) return true;
    }
    for (const auto& [chId, enc] : m_secondaryEncoders) {
        if (enc && enc->isRunning()) return true;
    }
    return false;
}

void StreamInstance::triggerPlatformInfoUpdate() {
    auto* game = m_gameManager->activeGame();
    std::string currentGameId = game ? game->id() : m_config.fixedGame;
    if (!currentGameId.empty()) {
        updatePlatformInfo(currentGameId);
    }
}

std::string StreamInstance::startStreaming() {
    auto& cm = Application::instance().channelManager();
    bool anyStarted = false;

    if (m_config.channelIds.empty()) {
        return "No channels assigned to this stream.";
    }

    // Helper: build base encoder settings from stream config
    auto buildBaseSettings = [&]() {
        streaming::EncoderSettings es;
        es.fps              = m_config.fps;
        es.bitrate          = m_config.bitrate;
        es.preset           = m_config.preset;
        es.codec            = m_config.codec;
        es.profile          = m_config.profile;
        es.tune             = m_config.tune;
        es.keyframeInterval = m_config.keyframeInterval;
        es.threads          = m_config.threads;
        es.cbr              = m_config.cbr;
        es.maxrateFactor    = m_config.maxrateFactor;
        es.bufsizeFactor    = m_config.bufsizeFactor;
        es.audioBitrate     = m_config.audioBitrate;
        es.audioSampleRate  = m_config.audioSampleRate;
        es.audioCodec       = m_config.audioCodec;
        try {
            es.audioMixer = &Application::instance().audioMixer();
        } catch (...) {
            es.audioMixer = nullptr;
        }
        return es;
    };

    std::string diagnostics;
    for (const auto& chId : m_config.channelIds) {
        const auto* cfg = cm.getChannelConfig(chId);
        if (!cfg) {
            diagnostics += "Channel '" + chId + "' not found. ";
            continue;
        }

        // ── Primary encoder ──────────────────────────────────────────────
        std::string url = cfg->settings.value("stream_url", "");
        std::string key = cfg->settings.value("stream_key", "");
        if (!url.empty()) {
            std::string fullUrl = key.empty() ? url : url + "/" + key;

            auto es       = buildBaseSettings();
            es.outputUrl  = fullUrl;
            es.width      = width();
            es.height     = height();

            auto enc = std::make_unique<streaming::StreamEncoder>(es);
            enc->start();
            spdlog::info("[Stream '{}'] Streaming to {} via channel '{}'",
                         m_config.name, url, chId);
            m_encoders[chId] = std::move(enc);
            anyStarted = true;
        } else {
            spdlog::debug("[Stream '{}'] Channel '{}' ({}) has no stream_url – skipping.",
                          m_config.name, chId, cfg->name);
        }

        // ── Secondary encoder (dual-format) ──────────────────────────────
        if (m_config.dualFormat) {
            std::string secUrl = cfg->settings.value("vertical_stream_url", "");
            std::string secKey = cfg->settings.value("vertical_stream_key", "");
            if (!secUrl.empty()) {
                std::string fullSecUrl = secKey.empty() ? secUrl : secUrl + "/" + secKey;

                auto es       = buildBaseSettings();
                es.outputUrl  = fullSecUrl;
                es.width      = m_config.secondaryWidth();
                es.height     = m_config.secondaryHeight();
                if (m_config.secondaryBitrate > 0)
                    es.bitrate = m_config.secondaryBitrate;
                if (m_config.secondaryFps > 0)
                    es.fps = m_config.secondaryFps;

                auto enc = std::make_unique<streaming::StreamEncoder>(es);
                enc->start();
                spdlog::info("[Stream '{}'] Secondary format streaming to {} via channel '{}' ({}x{})",
                             m_config.name, secUrl, chId, es.width, es.height);
                m_secondaryEncoders[chId] = std::move(enc);
                anyStarted = true;
            }
        }
    }

    if (!anyStarted) {
        std::string msg = "No assigned channels have a stream URL configured.";
        if (!diagnostics.empty()) msg += " " + diagnostics;
        spdlog::warn("[Stream '{}'] Cannot start: {}", m_config.name, msg);
        return msg;
    }

    // Update Twitch/YouTube stream info immediately when going live
    triggerPlatformInfoUpdate();
    return {}; // success
}

void StreamInstance::stopStreaming() {
    for (auto& [chId, enc] : m_encoders) {
        if (enc) enc->stop();
    }
    m_encoders.clear();
    for (auto& [chId, enc] : m_secondaryEncoders) {
        if (enc) enc->stop();
    }
    m_secondaryEncoders.clear();
}

// ── Configuration update ─────────────────────────────────────────────────────

void StreamInstance::updateConfig(const StreamConfig& c) {
    bool resChanged = (c.resolution != m_config.resolution);
    bool secResChanged = (c.secondaryResolution != m_config.secondaryResolution);

    // Detect whether a game switch is needed.  This covers:
    //   1) fixed_game changed while mode is Fixed
    //   2) mode just switched TO Fixed and the currently loaded game differs
    bool needGameSwitch = false;
    if (c.gameMode == GameModeType::Fixed && !c.fixedGame.empty()) {
        if (c.fixedGame != m_config.fixedGame) {
            needGameSwitch = true;   // case 1
        } else if (c.gameMode != m_config.gameMode &&
                   c.fixedGame != m_gameManager->activeGameName()) {
            needGameSwitch = true;   // case 2
        }
    }

    m_config = c;
    if (resChanged) { m_rtReady = false; }
    if (secResChanged) { m_secondaryRtReady = false; }

    // Never call loadGame() directly from the web-API thread – queue the
    // switch so the main loop executes it via checkPendingSwitch().
    if (needGameSwitch) {
        m_gameManager->requestSwitch(m_config.fixedGame, SwitchMode::Immediate);
    }

    // Re-apply font scale to the running game (so dashboard changes take effect)
    auto* game = m_gameManager->activeGame();
    if (game) {
        auto scaleIt = m_config.gameFontScales.find(game->id());
        float scale = (scaleIt != m_config.gameFontScales.end()) ? scaleIt->second : 1.0f;
        game->setFontScale(scale);
    }
}

// ── Serialisation ────────────────────────────────────────────────────────────

nlohmann::json StreamInstance::getState() const {
    nlohmann::json s;
    s["id"]         = m_config.id;
    s["name"]       = m_config.name;
    s["resolution"] = m_config.resolution == ResolutionPreset::Mobile     ? "mobile" :
                       m_config.resolution == ResolutionPreset::Desktop    ? "desktop" :
                       m_config.resolution == ResolutionPreset::Mobile720  ? "mobile720" : "desktop720";
    s["width"]      = width();
    s["height"]     = height();
    s["gameMode"]   = m_config.gameMode == GameModeType::Fixed  ? "fixed" :
                      m_config.gameMode == GameModeType::Vote   ? "vote"  : "random";
    s["streaming"]  = isStreaming();
    s["channelIds"] = m_config.channelIds;
    s["profileId"]  = m_config.profileId;

    // Editable config fields (so the dashboard can show/modify them)
    s["title"]      = m_config.title;
    s["description"]= m_config.description;
    s["fixedGame"]  = m_config.fixedGame;
    s["fps"]        = m_config.fps;
    s["bitrate"]    = m_config.bitrate;
    s["preset"]     = m_config.preset;
    s["codec"]      = m_config.codec;
    s["profile"]    = m_config.profile;
    s["tune"]       = m_config.tune;
    s["keyframeInterval"] = m_config.keyframeInterval;
    s["threads"]    = m_config.threads;
    s["cbr"]        = m_config.cbr;
    s["maxrateFactor"] = m_config.maxrateFactor;
    s["bufsizeFactor"] = m_config.bufsizeFactor;
    s["audioBitrate"]    = m_config.audioBitrate;
    s["audioSampleRate"] = m_config.audioSampleRate;
    s["audioCodec"]      = m_config.audioCodec;
    s["enabled"]    = m_config.enabled;

    // Dual-format fields
    s["dualFormat"] = m_config.dualFormat;
    if (m_config.dualFormat) {
        auto resToStr = [](ResolutionPreset r) -> std::string {
            switch (r) {
                case ResolutionPreset::Mobile:     return "mobile";
                case ResolutionPreset::Desktop:    return "desktop";
                case ResolutionPreset::Mobile720:  return "mobile720";
                case ResolutionPreset::Desktop720: return "desktop720";
                default: return "mobile";
            }
        };
        s["secondaryResolution"] = resToStr(m_config.secondaryResolution);
        s["secondaryWidth"]      = m_config.secondaryWidth();
        s["secondaryHeight"]     = m_config.secondaryHeight();
        s["secondaryBitrate"]    = m_config.secondaryBitrate;
        s["secondaryFps"]        = m_config.secondaryFps;
        s["secondaryStreaming"]  = !m_secondaryEncoders.empty();
    }

    // Per-game descriptions & info messages
    if (!m_config.gameDescriptions.empty())
        s["gameDescriptions"] = m_config.gameDescriptions;
    if (!m_config.gameInfoMessages.empty())
        s["gameInfoMessages"] = m_config.gameInfoMessages;
    if (!m_config.gameInfoIntervals.empty())
        s["gameInfoIntervals"] = m_config.gameInfoIntervals;

    // Per-game font scales
    if (!m_config.gameFontScales.empty())
        s["gameFontScales"] = m_config.gameFontScales;
    // Per-game player limits
    if (!m_config.gamePlayerLimits.empty())
        s["gamePlayerLimits"] = m_config.gamePlayerLimits;

    // Per-game platform names
    if (!m_config.gameTwitchCategories.empty())
        s["gameTwitchCategories"] = m_config.gameTwitchCategories;
    if (!m_config.gameTwitchTitles.empty())
        s["gameTwitchTitles"] = m_config.gameTwitchTitles;
    if (!m_config.gameYoutubeTitles.empty())
        s["gameYoutubeTitles"] = m_config.gameYoutubeTitles;

    // Scoreboard overlay settings
    s["scoreboardTopN"]         = m_config.scoreboardTopN;
    s["scoreboardFontSize"]     = m_config.scoreboardFontSize;
    s["scoreboardAllTimeTitle"] = m_config.scoreboardAllTimeTitle;
    s["scoreboardRecentTitle"]  = m_config.scoreboardRecentTitle;
    s["scoreboardRoundTitle"]   = m_config.scoreboardRoundTitle;
    s["scoreboardRecentHours"]  = m_config.scoreboardRecentHours;
    s["scoreboardCycleSecs"]    = m_config.scoreboardCycleSecs;
    s["scoreboardAllTimeSecs"]  = m_config.scoreboardAllTimeSecs;
    s["scoreboardRecentSecs"]   = m_config.scoreboardRecentSecs;
    s["scoreboardRoundSecs"]    = m_config.scoreboardRoundSecs;
    s["scoreboardFadeSecs"]     = m_config.scoreboardFadeSecs;
    s["scoreboardChatInterval"] = m_config.scoreboardChatInterval;
    s["voteOverlayFontScale"]   = m_config.voteOverlayFontScale;

    // Global scoreboard (all-time)
    {
        nlohmann::json sb = nlohmann::json::array();
        for (const auto& e : m_playerAlltimeCache) {
            sb.push_back({{"name", e.displayName}, {"points", e.points},
                          {"wins", e.wins}});
        }
        s["scoreboard"] = sb;
    }
    // Global scoreboard (recent)
    {
        nlohmann::json sb = nlohmann::json::array();
        for (const auto& e : m_playerRecentCache) {
            sb.push_back({{"name", e.displayName}, {"points", e.points},
                          {"wins", e.wins}});
        }
        s["scoreboardRecent"] = sb;
    }
    // Global scoreboard (current round)
    {
        nlohmann::json sb = nlohmann::json::array();
        for (const auto& [name, score] : m_playerRoundCache) {
            sb.push_back({{"name", name}, {"points", score}});
        }
        s["scoreboardRound"] = sb;
    }

    // Per-stream statistics
    s["stats"] = m_stats.toJson();

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
    j["title"]        = m_config.title;
    j["description"]  = m_config.description;
    j["profile_id"]   = m_config.profileId;
    j["resolution"]   = m_config.resolution == ResolutionPreset::Mobile     ? "mobile" :
                         m_config.resolution == ResolutionPreset::Desktop    ? "desktop" :
                         m_config.resolution == ResolutionPreset::Mobile720  ? "mobile720" : "desktop720";
    j["game_mode"]    = m_config.gameMode == GameModeType::Fixed ? "fixed" :
                        m_config.gameMode == GameModeType::Vote  ? "vote"  : "random";
    j["fixed_game"]   = m_config.fixedGame;
    j["channel_ids"]  = m_config.channelIds;
    j["enabled"]      = m_config.enabled;
    j["fps"]          = m_config.fps;
    j["bitrate_kbps"] = m_config.bitrate;
    j["preset"]       = m_config.preset;
    j["codec"]        = m_config.codec;
    j["profile"]      = m_config.profile;
    j["tune"]         = m_config.tune;
    j["keyframe_interval"] = m_config.keyframeInterval;
    j["threads"]      = m_config.threads;
    j["cbr"]          = m_config.cbr;
    j["maxrate_factor"] = m_config.maxrateFactor;
    j["bufsize_factor"] = m_config.bufsizeFactor;
    j["audio_bitrate"]  = m_config.audioBitrate;
    j["audio_sample_rate"] = m_config.audioSampleRate;
    j["audio_codec"]    = m_config.audioCodec;

    // Dual-format streaming
    j["dual_format"] = m_config.dualFormat;
    {
        auto resToStr = [](ResolutionPreset r) -> std::string {
            switch (r) {
                case ResolutionPreset::Mobile:     return "mobile";
                case ResolutionPreset::Desktop:    return "desktop";
                case ResolutionPreset::Mobile720:  return "mobile720";
                case ResolutionPreset::Desktop720: return "desktop720";
                default: return "mobile";
            }
        };
        j["secondary_resolution"] = resToStr(m_config.secondaryResolution);
    }
    j["secondary_bitrate_kbps"] = m_config.secondaryBitrate;
    j["secondary_fps"]          = m_config.secondaryFps;

    // Per-game descriptions
    if (!m_config.gameDescriptions.empty())
        j["game_descriptions"] = m_config.gameDescriptions;
    // Per-game info messages
    if (!m_config.gameInfoMessages.empty())
        j["game_info_messages"] = m_config.gameInfoMessages;
    if (!m_config.gameInfoIntervals.empty())
        j["game_info_intervals"] = m_config.gameInfoIntervals;

    // Per-game font scales
    if (!m_config.gameFontScales.empty())
        j["game_font_scales"] = m_config.gameFontScales;
    // Per-game player limits
    if (!m_config.gamePlayerLimits.empty())
        j["game_player_limits"] = m_config.gamePlayerLimits;

    // Per-game platform names
    if (!m_config.gameTwitchCategories.empty())
        j["game_twitch_categories"] = m_config.gameTwitchCategories;
    if (!m_config.gameTwitchTitles.empty())
        j["game_twitch_titles"] = m_config.gameTwitchTitles;
    if (!m_config.gameYoutubeTitles.empty())
        j["game_youtube_titles"] = m_config.gameYoutubeTitles;

    // Scoreboard settings
    j["scoreboard_top_n"]          = m_config.scoreboardTopN;
    j["scoreboard_font_size"]      = m_config.scoreboardFontSize;
    j["scoreboard_alltime_title"]  = m_config.scoreboardAllTimeTitle;
    j["scoreboard_recent_title"]   = m_config.scoreboardRecentTitle;
    j["scoreboard_round_title"]    = m_config.scoreboardRoundTitle;
    j["scoreboard_recent_hours"]   = m_config.scoreboardRecentHours;
    j["scoreboard_cycle_secs"]     = m_config.scoreboardCycleSecs;
    j["scoreboard_alltime_secs"]   = m_config.scoreboardAllTimeSecs;
    j["scoreboard_recent_secs"]    = m_config.scoreboardRecentSecs;
    j["scoreboard_round_secs"]     = m_config.scoreboardRoundSecs;
    j["scoreboard_fade_secs"]      = m_config.scoreboardFadeSecs;
    j["scoreboard_chat_interval"]  = m_config.scoreboardChatInterval;

    // Vote overlay font scale
    j["vote_overlay_font_scale"]   = m_config.voteOverlayFontScale;

    // Enabled games list
    if (!m_config.enabledGames.empty())
        j["enabled_games"] = m_config.enabledGames;

    return j;
}

StreamConfig StreamInstance::configFromJson(const nlohmann::json& j) {
    StreamConfig c;
    c.id          = j.value("id", "");
    c.name        = j.value("name", "Stream");
    c.title       = j.value("title", "");
    c.description = j.value("description", "");
    c.profileId   = j.value("profile_id", "");

    std::string res = j.value("resolution", "mobile");
    if      (res == "desktop")    c.resolution = ResolutionPreset::Desktop;
    else if (res == "mobile720")  c.resolution = ResolutionPreset::Mobile720;
    else if (res == "desktop720") c.resolution = ResolutionPreset::Desktop720;
    else                          c.resolution = ResolutionPreset::Mobile;

    std::string mode = j.value("game_mode", "fixed");
    if (mode == "vote")        c.gameMode = GameModeType::Vote;
    else if (mode == "random") c.gameMode = GameModeType::Random;
    else                       c.gameMode = GameModeType::Fixed;

    c.fixedGame = j.value("fixed_game", "chaos_arena");

    if (j.contains("channel_ids") && j["channel_ids"].is_array()) {
        for (const auto& ch : j["channel_ids"])
            c.channelIds.push_back(ch.get<std::string>());
    }

    c.enabled   = j.value("enabled", true);
    c.fps       = j.value("fps", 30);
    c.bitrate   = j.value("bitrate_kbps", 6000);
    c.preset    = j.value("preset", "ultrafast");
    c.codec     = j.value("codec", "libx264");
    c.profile   = j.value("profile", "baseline");
    c.tune      = j.value("tune", "zerolatency");
    c.keyframeInterval = j.value("keyframe_interval", 2);
    c.threads   = j.value("threads", 6);
    c.cbr       = j.value("cbr", true);
    c.maxrateFactor = j.value("maxrate_factor", 1.2f);
    c.bufsizeFactor = j.value("bufsize_factor", 1.0f);
    c.audioBitrate    = j.value("audio_bitrate", 128);
    c.audioSampleRate = j.value("audio_sample_rate", 44100);
    c.audioCodec      = j.value("audio_codec", std::string("aac"));

    // Dual-format streaming
    c.dualFormat = j.value("dual_format", false);
    {
        std::string secRes = j.value("secondary_resolution", "");
        if      (secRes == "mobile")     c.secondaryResolution = ResolutionPreset::Mobile;
        else if (secRes == "desktop")    c.secondaryResolution = ResolutionPreset::Desktop;
        else if (secRes == "mobile720")  c.secondaryResolution = ResolutionPreset::Mobile720;
        else if (secRes == "desktop720") c.secondaryResolution = ResolutionPreset::Desktop720;
        else                             c.secondaryResolution = companionResolution(c.resolution);
    }
    c.secondaryBitrate = j.value("secondary_bitrate_kbps", 0);
    c.secondaryFps     = j.value("secondary_fps", 0);

    // Per-game descriptions
    if (j.contains("game_descriptions") && j["game_descriptions"].is_object()) {
        for (auto& [k, v] : j["game_descriptions"].items())
            c.gameDescriptions[k] = v.get<std::string>();
    }
    if (j.contains("game_info_messages") && j["game_info_messages"].is_object()) {
        for (auto& [k, v] : j["game_info_messages"].items())
            c.gameInfoMessages[k] = v.get<std::string>();
    }
    if (j.contains("game_info_intervals") && j["game_info_intervals"].is_object()) {
        for (auto& [k, v] : j["game_info_intervals"].items())
            c.gameInfoIntervals[k] = v.get<int>();
    }

    // Per-game font scales
    if (j.contains("game_font_scales") && j["game_font_scales"].is_object()) {
        for (auto& [k, v] : j["game_font_scales"].items())
            c.gameFontScales[k] = v.get<float>();
    }
    // Per-game player limits
    if (j.contains("game_player_limits") && j["game_player_limits"].is_object()) {
        for (auto& [k, v] : j["game_player_limits"].items())
            c.gamePlayerLimits[k] = v.get<int>();
    }

    // Per-game platform names
    if (j.contains("game_twitch_categories") && j["game_twitch_categories"].is_object()) {
        for (auto& [k, v] : j["game_twitch_categories"].items())
            c.gameTwitchCategories[k] = v.get<std::string>();
    }
    if (j.contains("game_twitch_titles") && j["game_twitch_titles"].is_object()) {
        for (auto& [k, v] : j["game_twitch_titles"].items())
            c.gameTwitchTitles[k] = v.get<std::string>();
    }
    if (j.contains("game_youtube_titles") && j["game_youtube_titles"].is_object()) {
        for (auto& [k, v] : j["game_youtube_titles"].items())
            c.gameYoutubeTitles[k] = v.get<std::string>();
    }

    // Scoreboard settings
    c.scoreboardTopN       = j.value("scoreboard_top_n", 5);
    c.scoreboardFontSize   = j.value("scoreboard_font_size", 20);
    c.scoreboardAllTimeTitle  = j.value("scoreboard_alltime_title", std::string("ALL TIME"));
    c.scoreboardRecentTitle   = j.value("scoreboard_recent_title", std::string("LAST 24H"));
    c.scoreboardRoundTitle    = j.value("scoreboard_round_title", std::string("CURRENT ROUND"));
    c.scoreboardRecentHours   = j.value("scoreboard_recent_hours", 24);
    c.scoreboardCycleSecs     = j.value("scoreboard_cycle_secs", 10.0);
    c.scoreboardAllTimeSecs   = j.value("scoreboard_alltime_secs", 10.0);
    c.scoreboardRecentSecs    = j.value("scoreboard_recent_secs", 10.0);
    c.scoreboardRoundSecs     = j.value("scoreboard_round_secs", 8.0);
    c.scoreboardFadeSecs      = j.value("scoreboard_fade_secs", 1.0);
    c.scoreboardChatInterval  = j.value("scoreboard_chat_interval", 120);

    // Vote overlay font scale
    c.voteOverlayFontScale = j.value("vote_overlay_font_scale", 1.0f);

    // Enabled games list (for vote/random mode filtering)
    if (j.contains("enabled_games") && j["enabled_games"].is_array()) {
        for (const auto& g : j["enabled_games"])
            c.enabledGames.push_back(g.get<std::string>());
    }

    return c;
}

} // namespace is::core
