#include "core/StreamInstance.h"
#include "core/Config.h"
#include "core/Application.h"
#include "core/ChannelManager.h"
#include "rendering/Renderer.h"
#include "games/GameRegistry.h"
#include "streaming/StreamEncoder.h"
#include "platform/twitch/TwitchApi.h"

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
    // Record per-stream statistics
    m_stats.recordMessage(msg.userId, msg.displayName);

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

    // Refresh global scoreboard cache periodically
    m_scoreboardRefreshTimer += dt;
    if (m_scoreboardRefreshTimer >= SCOREBOARD_REFRESH_INTERVAL) {
        m_scoreboardRefreshTimer = 0.0;
        updateScoreboardCache();
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
                m_infoMessageTimer = 0.0;
                sendPeriodicInfoMessage();
            }
        }
    }
}

void StreamInstance::render(double alpha) {
    bool encoderRunning = isStreaming();
    bool windowActive   = !Application::instance().renderer().isHeadless();

    m_jpegFrameCounter++;
    bool needJpeg = (m_jpegFrameCounter >= m_jpegFrameInterval);

    // Skip the entire GPU render when nothing needs a fresh frame
    // (headless mode, no encoder, and JPEG preview not yet due).
    if (!encoderRunning && !windowActive && !needJpeg) {
        return;
    }
    if (needJpeg) m_jpegFrameCounter = 0;

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

    // GPU→CPU readback: encoder needs it every frame, JPEG periodically
    if (encoderRunning || needJpeg) {
        m_frameCapture = m_renderTexture.getTexture().copyToImage();
    }
    if (needJpeg) {
        updateJpegBuffer();
    }
}

void StreamInstance::encodeFrame() {
    if (m_encoders.empty()) return;

    const sf::Uint8* pixels = getFrameBuffer();
    if (!pixels) return;

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
    return games::GameRegistry::instance().list();
}

// ── Vote overlay rendering ───────────────────────────────────────────────────

void StreamInstance::renderVoteOverlay() {
    if (!m_fontLoaded) return;
    float w = static_cast<float>(width());
    float h = static_cast<float>(height());
    float vfs = m_config.voteOverlayFontScale;
    auto fs = [vfs](int base) -> unsigned int {
        return static_cast<unsigned int>(std::max(1.0f, base * vfs));
    };

    // Dim background
    sf::RectangleShape overlay(sf::Vector2f(w, h));
    overlay.setFillColor(sf::Color(0, 0, 0, 180));
    m_renderTexture.draw(overlay);

    // Title
    sf::Text title;
    title.setFont(m_font);
    title.setString("VOTE FOR NEXT GAME!");
    title.setCharacterSize(fs(36));
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
    timer.setCharacterSize(fs(24));
    timer.setFillColor(sf::Color(200, 200, 200));
    auto tmb = timer.getLocalBounds();
    timer.setPosition((w - tmb.width) / 2.0f, h * 0.22f);
    m_renderTexture.draw(timer);

    // Instructions
    sf::Text instr;
    instr.setFont(m_font);
    instr.setString("Type !vote <game_id> in chat");
    instr.setCharacterSize(fs(18));
    instr.setFillColor(sf::Color(150, 150, 150));
    auto ib = instr.getLocalBounds();
    instr.setPosition((w - ib.width) / 2.0f, h * 0.27f);
    m_renderTexture.draw(instr);

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
        m_renderTexture.draw(card);

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
        m_renderTexture.draw(nameT);

        sf::Text voteT;
        voteT.setFont(m_font);
        voteT.setString(std::to_string(votes) + " votes");
        voteT.setCharacterSize(fs(16));
        voteT.setFillColor(sf::Color(88, 166, 255));
        voteT.setPosition(cardX + 15.0f, y + 34.0f);
        m_renderTexture.draw(voteT);

        sf::Text idT;
        idT.setFont(m_font);
        idT.setString("!vote " + gid);
        idT.setCharacterSize(fs(14));
        idT.setFillColor(sf::Color(120, 120, 140));
        auto idb = idT.getLocalBounds();
        idT.setPosition(cardX + cardW - idb.width - 15.0f, y + 20.0f);
        m_renderTexture.draw(idT);

        y += cardH + 10.0f;
    }
}

// ── Global scoreboard overlay ────────────────────────────────────────────────

void StreamInstance::updateScoreboardCache() {
    auto& db = Application::instance().playerDatabase();
    m_scoreboardCache       = db.getTopAllTime(m_config.scoreboardTopN);
    m_scoreboardRecentCache = db.getTopRecent(m_config.scoreboardTopN,
                                              m_config.scoreboardRecentHours);
}

void StreamInstance::renderGlobalScoreboard() {
    if (!m_fontLoaded) return;
    if (m_scoreboardCache.empty() && m_scoreboardRecentCache.empty()) return;

    float w = static_cast<float>(width());

    int baseFontSize = m_config.scoreboardFontSize;
    int headerFontSize = baseFontSize + 2;
    float lineH  = baseFontSize * 1.6f;
    float headerH = headerFontSize * 1.8f;
    float padX   = 12.0f;
    float padY   = 8.0f;
    float panelW = w * 0.30f;

    // Helper lambda to draw one scoreboard panel
    auto drawPanel = [&](float panelX, float panelY,
                         const std::string& title,
                         const std::vector<ScoreEntry>& entries) -> float {
        int count = static_cast<int>(entries.size());
        float panelH = headerH + static_cast<float>(count) * lineH + padY * 2.0f;

        // Background
        sf::RectangleShape bg(sf::Vector2f(panelW, panelH));
        bg.setPosition(panelX, panelY);
        bg.setFillColor(sf::Color(10, 10, 20, 180));
        bg.setOutlineColor(sf::Color(80, 130, 200, 150));
        bg.setOutlineThickness(1.5f);
        m_renderTexture.draw(bg);

        // Header
        sf::Text header;
        header.setFont(m_font);
        header.setString(title);
        header.setCharacterSize(static_cast<unsigned int>(headerFontSize));
        header.setFillColor(sf::Color(255, 215, 0));
        header.setStyle(sf::Text::Bold);
        auto hb = header.getLocalBounds();
        header.setPosition(panelX + (panelW - hb.width) / 2.0f, panelY + padY);
        m_renderTexture.draw(header);

        // Entries
        float y = panelY + padY + headerH;
        int rank = 1;
        for (const auto& entry : entries) {
            std::string line = std::to_string(rank) + ". " + entry.displayName;

            sf::Text nameText;
            nameText.setFont(m_font);
            nameText.setString(line);
            nameText.setCharacterSize(static_cast<unsigned int>(baseFontSize));
            nameText.setFillColor(rank == 1 ? sf::Color(255, 215, 0) :
                                  rank == 2 ? sf::Color(200, 200, 200) :
                                  rank == 3 ? sf::Color(205, 127, 50) :
                                              sf::Color(170, 170, 190));
            nameText.setPosition(panelX + padX, y);
            m_renderTexture.draw(nameText);

            sf::Text ptsText;
            ptsText.setFont(m_font);
            ptsText.setString(std::to_string(entry.points) + " pts");
            ptsText.setCharacterSize(static_cast<unsigned int>(baseFontSize));
            ptsText.setFillColor(sf::Color(88, 166, 255));
            auto pb = ptsText.getLocalBounds();
            ptsText.setPosition(panelX + panelW - pb.width - padX, y);
            m_renderTexture.draw(ptsText);

            y += lineH;
            rank++;
        }
        return panelH;
    };

    float panelX = w - panelW - 12.0f;
    float panelY = 12.0f;

    // Draw ALL TIME scoreboard
    if (!m_scoreboardCache.empty()) {
        float h1 = drawPanel(panelX, panelY,
                             m_config.scoreboardAllTimeTitle,
                             m_scoreboardCache);
        panelY += h1 + 8.0f;
    }

    // Draw RECENT scoreboard below
    if (!m_scoreboardRecentCache.empty()) {
        drawPanel(panelX, panelY,
                  m_config.scoreboardRecentTitle,
                  m_scoreboardRecentCache);
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

// ── Platform info update (Twitch / YouTube) ───────────────────────────────────

void StreamInstance::updatePlatformInfo(const std::string& gameId) {
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

    // If no platform names configured, nothing to do
    if (twitchCategory.empty() && twitchTitle.empty() && youtubeTitle.empty()) {
        spdlog::debug("[Stream '{}'] No Twitch/YouTube category or title configured for game '{}' – "
                      "skipping platform info update. Configure these in the stream settings.",
                      m_config.name, gameId);
        return;
    }

    auto& cm = Application::instance().channelManager();
    std::string clientId = Application::instance().config().get<std::string>("twitch.client_id", "");

    // Update Twitch channels subscribed to this stream
    if (!twitchCategory.empty() || !twitchTitle.empty()) {
        for (const auto& chId : m_config.channelIds) {
            const auto* cfg = cm.getChannelConfig(chId);
            if (!cfg || cfg->platform != "twitch") continue;

            std::string token = cfg->settings.value("oauth_token", "");
            if (token.empty() || clientId.empty()) {
                spdlog::warn("[Stream '{}'] Twitch channel '{}' missing oauth_token or client_id – "
                             "cannot update stream info. Set client_id in Settings page.",
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

    // YouTube: future implementation (requires YouTube OAuth & Data API v3)
    if (!youtubeTitle.empty()) {
        spdlog::debug("[Stream '{}'] YouTube title update not yet implemented (title='{}').",
                      m_config.name, youtubeTitle);
    }
}

// ── Streaming control ────────────────────────────────────────────────────────

bool StreamInstance::isStreaming() const {
    for (const auto& [chId, enc] : m_encoders) {
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

bool StreamInstance::startStreaming() {
    auto& cm = Application::instance().channelManager();
    bool anyStarted = false;

    for (const auto& chId : m_config.channelIds) {
        const auto* cfg = cm.getChannelConfig(chId);
        if (!cfg) continue;

        std::string url = cfg->settings.value("stream_url", "");
        std::string key = cfg->settings.value("stream_key", "");
        if (url.empty()) continue;

        std::string fullUrl = key.empty() ? url : url + "/" + key;

        streaming::EncoderSettings es;
        es.outputUrl  = fullUrl;
        es.width      = width();
        es.height     = height();
        es.fps        = m_config.fps;
        es.bitrate    = m_config.bitrate;
        es.preset     = m_config.preset;
        es.codec      = m_config.codec;

        auto enc = std::make_unique<streaming::StreamEncoder>(es);
        enc->start();
        spdlog::info("[Stream '{}'] Streaming to {} via channel '{}'",
                     m_config.name, url, chId);
        m_encoders[chId] = std::move(enc);
        anyStarted = true;
    }

    if (!anyStarted) {
        spdlog::warn("[Stream '{}'] Cannot start: no channels with stream URLs configured.",
                     m_config.name);
        return false;
    }

    // Update Twitch/YouTube stream info immediately when going live
    triggerPlatformInfoUpdate();
    return true;
}

void StreamInstance::stopStreaming() {
    for (auto& [chId, enc] : m_encoders) {
        if (enc) enc->stop();
    }
    m_encoders.clear();
}

// ── Configuration update ─────────────────────────────────────────────────────

void StreamInstance::updateConfig(const StreamConfig& c) {
    bool resChanged  = (c.resolution != m_config.resolution);
    bool gameChanged = (c.fixedGame != m_config.fixedGame &&
                        c.gameMode == GameModeType::Fixed);
    m_config = c;
    if (resChanged) { m_rtReady = false; }
    if (gameChanged) { m_gameManager->loadGame(m_config.fixedGame); }

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
    s["resolution"] = m_config.resolution == ResolutionPreset::Mobile ? "mobile" : "desktop";
    s["width"]      = width();
    s["height"]     = height();
    s["gameMode"]   = m_config.gameMode == GameModeType::Fixed  ? "fixed" :
                      m_config.gameMode == GameModeType::Vote   ? "vote"  : "random";
    s["streaming"]  = isStreaming();
    s["channelIds"] = m_config.channelIds;

    // Editable config fields (so the dashboard can show/modify them)
    s["title"]      = m_config.title;
    s["description"]= m_config.description;
    s["fixedGame"]  = m_config.fixedGame;
    s["fps"]        = m_config.fps;
    s["bitrate"]    = m_config.bitrate;
    s["preset"]     = m_config.preset;
    s["codec"]      = m_config.codec;
    s["enabled"]    = m_config.enabled;

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
    s["scoreboardRecentHours"]  = m_config.scoreboardRecentHours;
    s["voteOverlayFontScale"]   = m_config.voteOverlayFontScale;

    // Global scoreboard (all-time)
    {
        nlohmann::json sb = nlohmann::json::array();
        for (const auto& e : m_scoreboardCache) {
            sb.push_back({{"name", e.displayName}, {"points", e.points},
                          {"wins", e.wins}});
        }
        s["scoreboard"] = sb;
    }
    // Global scoreboard (recent)
    {
        nlohmann::json sb = nlohmann::json::array();
        for (const auto& e : m_scoreboardRecentCache) {
            sb.push_back({{"name", e.displayName}, {"points", e.points},
                          {"wins", e.wins}});
        }
        s["scoreboardRecent"] = sb;
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
    j["resolution"]   = m_config.resolution == ResolutionPreset::Mobile ? "mobile" : "desktop";
    j["game_mode"]    = m_config.gameMode == GameModeType::Fixed ? "fixed" :
                        m_config.gameMode == GameModeType::Vote  ? "vote"  : "random";
    j["fixed_game"]   = m_config.fixedGame;
    j["channel_ids"]  = m_config.channelIds;
    j["enabled"]      = m_config.enabled;
    j["fps"]          = m_config.fps;
    j["bitrate_kbps"] = m_config.bitrate;
    j["preset"]       = m_config.preset;
    j["codec"]        = m_config.codec;

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
    j["scoreboard_recent_hours"]   = m_config.scoreboardRecentHours;

    // Vote overlay font scale
    j["vote_overlay_font_scale"]   = m_config.voteOverlayFontScale;

    return j;
}

StreamConfig StreamInstance::configFromJson(const nlohmann::json& j) {
    StreamConfig c;
    c.id          = j.value("id", "");
    c.name        = j.value("name", "Stream");
    c.title       = j.value("title", "");
    c.description = j.value("description", "");

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

    c.enabled   = j.value("enabled", true);
    c.fps       = j.value("fps", 30);
    c.bitrate   = j.value("bitrate_kbps", 4500);
    c.preset    = j.value("preset", "fast");
    c.codec     = j.value("codec", "libx264");

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
    c.scoreboardRecentHours   = j.value("scoreboard_recent_hours", 24);

    // Vote overlay font scale
    c.voteOverlayFontScale = j.value("vote_overlay_font_scale", 1.0f);

    return c;
}

} // namespace is::core
