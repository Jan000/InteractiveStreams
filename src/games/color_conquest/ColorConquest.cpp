#include "games/color_conquest/ColorConquest.h"
#include "core/Application.h"
#include "core/PlayerDatabase.h"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <sstream>
#include <cmath>

namespace is::games::color_conquest {

// ═══════════════════════════════════════════════════════════════════════════════
// Auto-registration with GameRegistry
// ═══════════════════════════════════════════════════════════════════════════════
REGISTER_GAME(ColorConquest);

// ─── Constants (9:16 vertical layout, 1080x1920) ────────────────────────────
static constexpr int   GRID_W  = 24;    // Grid width in cells
static constexpr int   GRID_H  = 40;    // Grid height in cells
static constexpr float CELL_PX = 38.0f; // Pixels per grid cell
static constexpr float GRID_OFFSET_X = (1080.0f - GRID_W * CELL_PX) / 2.0f;
static constexpr float GRID_OFFSET_Y = 60.0f;  // Top margin for UI

// ═══════════════════════════════════════════════════════════════════════════════
// Construction / Lifecycle
// ═══════════════════════════════════════════════════════════════════════════════

ColorConquest::ColorConquest() = default;

void ColorConquest::initialize() {
    spdlog::info("[ColorConquest] Initializing...");

    m_grid.initialize(GRID_W, GRID_H);
    m_grid.placeStartingPositions();

    // Initialize team data
    m_teams[0] = {"Red",    0, 0, 0, {}};
    m_teams[1] = {"Blue",   0, 0, 0, {}};
    m_teams[2] = {"Green",  0, 0, 0, {}};
    m_teams[3] = {"Yellow", 0, 0, 0, {}};

    m_phase = Phase::Lobby;
    m_currentRound = 0;
    m_lobbyTimer = 0.0f;
    m_animTime   = 0.0f;

    // Load font
    if (m_font.loadFromFile("assets/fonts/JetBrainsMono-Regular.ttf")) {
        m_fontLoaded = true;
    } else {
        spdlog::warn("[ColorConquest] Font not found, text rendering disabled.");
    }

    spdlog::info("[ColorConquest] Initialized. Grid: {}x{}, {} rounds.",
                 GRID_W, GRID_H, m_maxRounds);
}

void ColorConquest::shutdown() {
    spdlog::info("[ColorConquest] Shutdown.");
    m_playerTeam.clear();
    m_playerNames.clear();
    m_currentVotes.clear();
    m_conquestAnims.clear();
    m_events.clear();
    m_grid.clear();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Chat Command Processing — O(1) per message
// ═══════════════════════════════════════════════════════════════════════════════

void ColorConquest::onChatMessage(const platform::ChatMessage& msg) {
    if (msg.text.empty() || msg.text[0] != '!') return;

    std::istringstream iss(msg.text);
    std::string cmd;
    iss >> cmd;
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);

    if (cmd == "!join" || cmd == "!play") {
        std::string teamArg;
        iss >> teamArg;
        std::transform(teamArg.begin(), teamArg.end(), teamArg.begin(), ::tolower);
        cmdJoin(msg.userId, msg.displayName, teamArg);
    }
    else if (cmd == "!up"    || cmd == "!u" || cmd == "!w" || cmd == "!north") {
        cmdVote(msg.userId, Direction::Up);
    }
    else if (cmd == "!down"  || cmd == "!d" || cmd == "!s" || cmd == "!south") {
        cmdVote(msg.userId, Direction::Down);
    }
    else if (cmd == "!left"  || cmd == "!l" || cmd == "!a" || cmd == "!west") {
        cmdVote(msg.userId, Direction::Left);
    }
    else if (cmd == "!right" || cmd == "!r" || cmd == "!e" || cmd == "!east") {
        cmdVote(msg.userId, Direction::Right);
    }
    else if (cmd == "!emote") {
        std::string emote;
        iss >> emote;
        cmdEmote(msg.userId, emote);
    }
}

void ColorConquest::cmdJoin(const std::string& userId, const std::string& displayName,
                            const std::string& teamName) {
    // Already on a team?
    if (m_playerTeam.count(userId)) {
        return;  // Can't switch teams mid-game
    }

    TeamId team = parseTeamName(teamName);

    // If no team specified or invalid, auto-assign to smallest team
    if (team == TeamId::None) {
        int minPlayers = INT_MAX;
        team = TeamId::Red;
        for (int i = 0; i < 4; i++) {
            if (m_teams[i].playerCount < minPlayers) {
                minPlayers = m_teams[i].playerCount;
                team = static_cast<TeamId>(i + 1);
            }
        }
    }

    m_playerTeam[userId] = team;
    m_playerNames[userId] = displayName;
    int idx = static_cast<int>(team) - 1;
    m_teams[idx].playerCount++;

    m_events.push_back({
        displayName + " joined Team " + getTeamName(team) + "!",
        getTeamColor(team),
        EVENT_DISPLAY_TIME
    });

    int totalPlayers = static_cast<int>(m_playerTeam.size());
    spdlog::info("[ColorConquest] '{}' joined Team {} ({} total players)",
                 displayName, getTeamName(team), totalPlayers);
}

void ColorConquest::cmdVote(const std::string& userId, Direction dir) {
    if (m_phase != Phase::Playing) return;

    auto it = m_playerTeam.find(userId);
    if (it == m_playerTeam.end()) return;  // Not in a team

    TeamId team = it->second;
    int teamIdx = static_cast<int>(team) - 1;

    // Remove previous vote if exists
    auto voteIt = m_currentVotes.find(userId);
    if (voteIt != m_currentVotes.end()) {
        int oldDir = static_cast<int>(voteIt->second);
        m_teams[teamIdx].votes[oldDir]--;
    }

    // Record new vote
    m_currentVotes[userId] = dir;
    m_teams[teamIdx].votes[static_cast<int>(dir)]++;
}

void ColorConquest::cmdEmote(const std::string& userId, const std::string& emote) {
    auto it = m_playerTeam.find(userId);
    if (it == m_playerTeam.end()) return;
    auto nameIt = m_playerNames.find(userId);
    if (nameIt == m_playerNames.end()) return;

    m_events.push_back({
        nameIt->second + ": " + emote,
        getTeamColor(it->second),
        EVENT_DISPLAY_TIME
    });
}

// ═══════════════════════════════════════════════════════════════════════════════
// Game Logic Update
// ═══════════════════════════════════════════════════════════════════════════════

void ColorConquest::update(double dt) {
    float fdt = static_cast<float>(dt);
    m_animTime += fdt;

    // Update event timers
    for (auto& e : m_events) e.timer -= fdt;
    m_events.erase(std::remove_if(m_events.begin(), m_events.end(),
        [](const EventEntry& e) { return e.timer <= 0; }), m_events.end());

    // Update conquest animations
    for (auto& a : m_conquestAnims) a.timer -= fdt;
    m_conquestAnims.erase(std::remove_if(m_conquestAnims.begin(), m_conquestAnims.end(),
        [](const ConquestAnim& a) { return a.timer <= 0; }), m_conquestAnims.end());

    switch (m_phase) {
    case Phase::Lobby: {
        m_lobbyTimer += fdt;
        int totalPlayers = static_cast<int>(m_playerTeam.size());
        if (totalPlayers >= MIN_PLAYERS_TO_START && m_lobbyTimer >= m_lobbyDuration) {
            startGame();
        }
        break;
    }

    case Phase::Playing: {
        m_roundTimer -= fdt;
        if (m_roundTimer <= 0.0f) {
            executeRound();
        }
        break;
    }

    case Phase::RoundEnd: {
        m_roundTimer -= fdt;
        if (m_roundTimer <= 0.0f) {
            checkGameOver();
        }
        break;
    }

    case Phase::GameOver: {
        m_roundTimer -= fdt;
        if (m_roundTimer <= 0.0f) {
            resetForNewGame();
        }
        break;
    }
    }

    // Update team cell counts (O(grid) but grid is small: 40×24 = 960 cells)
    for (int i = 0; i < 4; i++) {
        m_teams[i].cellCount = m_grid.countCells(static_cast<TeamId>(i + 1));
    }
}

void ColorConquest::startGame() {
    m_phase = Phase::Playing;
    m_currentRound = 1;
    m_roundTimer = m_roundDuration;

    for (auto& t : m_teams) t.clearVotes();
    m_currentVotes.clear();

    m_events.push_back({
        "Game started! Vote with !up !down !left !right",
        sf::Color::White,
        EVENT_DISPLAY_TIME
    });

    spdlog::info("[ColorConquest] Game started! {} players across 4 teams.",
                 m_playerTeam.size());
}

void ColorConquest::executeRound() {
    spdlog::info("[ColorConquest] Round {} executing...", m_currentRound);

    // For each team with players, find winning vote and expand
    for (int i = 0; i < 4; i++) {
        auto team = static_cast<TeamId>(i + 1);
        if (m_teams[i].playerCount == 0) continue;
        if (m_teams[i].totalVotes() == 0) continue;

        Direction winDir = m_teams[i].topVote();
        expandTeam(team, winDir);

        // Direction names
        static const char* dirNames[] = {"?", "Up", "Down", "Left", "Right"};
        m_events.push_back({
            "Team " + getTeamName(team) + " expands " +
            dirNames[static_cast<int>(winDir)] + "!",
            getTeamColor(team),
            EVENT_DISPLAY_TIME
        });
    }

    // Clear votes for next round
    for (auto& t : m_teams) t.clearVotes();
    m_currentVotes.clear();

    m_phase = Phase::RoundEnd;
    m_roundTimer = m_resultDuration;
}

void ColorConquest::expandTeam(TeamId team, Direction dir) {
    auto frontier = m_grid.getFrontier(team, dir);

    // Expand into all frontier cells
    for (auto [x, y] : frontier) {
        TeamId prev = m_grid.getCell(x, y);
        m_grid.setCell(x, y, team);

        // Create conquest animation
        m_conquestAnims.push_back({x, y, team, CONQUEST_ANIM_TIME});

        // If conquering an enemy cell, log it
        if (prev != TeamId::None && prev != team) {
            m_events.push_back({
                "Team " + getTeamName(team) + " conquered a " +
                getTeamName(prev) + " cell!",
                getTeamColor(team),
                EVENT_DISPLAY_TIME * 0.6f
            });
        }
    }

    spdlog::debug("[ColorConquest] Team {} expanded {} cells in direction {}.",
                  getTeamName(team), frontier.size(), static_cast<int>(dir));
}

void ColorConquest::checkGameOver() {
    if (m_currentRound >= m_maxRounds) {
        m_phase = Phase::GameOver;
        m_roundTimer = 10.0f;  // Show results for 10s

        // Determine winner
        int maxCells = 0;
        TeamId winnerTeam = TeamId::None;
        std::string winner = "Nobody";
        for (int i = 0; i < 4; i++) {
            if (m_teams[i].cellCount > maxCells) {
                maxCells = m_teams[i].cellCount;
                winnerTeam = static_cast<TeamId>(i + 1);
                winner = getTeamName(winnerTeam);
            }
        }

        // Record scores to persistent database
        try {
            auto& db = is::core::Application::instance().playerDatabase();
            for (const auto& [userId, team] : m_playerTeam) {
                const auto& name = m_playerNames.count(userId) ? m_playerNames.at(userId) : userId;
                bool isWin = (team == winnerTeam);
                int points = isWin ? 50 : 5;  // 50 for winners, 5 for participation
                db.recordResult(userId, name, "color_conquest", points, isWin);
            }
        } catch (...) {}

        m_events.push_back({
            "GAME OVER! Team " + winner + " wins with " +
            std::to_string(maxCells) + " cells!",
            sf::Color::White,
            10.0f
        });

        spdlog::info("[ColorConquest] Game over! Winner: {} ({} cells)", winner, maxCells);
    } else {
        m_currentRound++;
        m_phase = Phase::Playing;
        m_roundTimer = m_roundDuration;
    }
}

void ColorConquest::resetForNewGame() {
    m_phase = Phase::Lobby;
    m_currentRound = 0;
    m_lobbyTimer = 0.0f;
    m_grid.clear();
    m_grid.placeStartingPositions();
    m_playerTeam.clear();
    m_playerNames.clear();
    m_currentVotes.clear();
    m_conquestAnims.clear();
    m_events.clear();
    for (auto& t : m_teams) {
        t.playerCount = 0;
        t.cellCount   = 0;
        t.roundsWon   = 0;
        t.clearVotes();
    }

    spdlog::info("[ColorConquest] Reset for new game.");
}

// ═══════════════════════════════════════════════════════════════════════════════
// Rendering
// ═══════════════════════════════════════════════════════════════════════════════

void ColorConquest::render(sf::RenderTarget& target, double /*alpha*/) {
    // Dark background (9:16 vertical)
    sf::RectangleShape bg(sf::Vector2f(1080.f, 1920.f));
    bg.setFillColor(sf::Color(12, 12, 20));
    target.draw(bg);

    renderGrid(target);
    renderCellConquestAnimation(target);
    renderTeamStats(target);
    renderRoundInfo(target);

    if (m_phase == Phase::Playing) {
        renderVoteArrows(target);
    }

    if (m_phase == Phase::Lobby) {
        renderLobby(target);
    }

    if (m_phase == Phase::GameOver) {
        renderGameOver(target);
    }

    // Event feed (bottom area)
    if (m_fontLoaded) {
        float ey = 1920.0f - 30.0f;
        for (int i = static_cast<int>(m_events.size()) - 1; i >= 0 && ey > 1700.0f; i--) {
            auto& ev = m_events[i];
            float alpha = std::min(1.0f, ev.timer / 1.0f);
            sf::Text t;
            t.setFont(m_font);
            t.setString(ev.text);
            t.setCharacterSize(16);
            t.setFillColor(sf::Color(ev.color.r, ev.color.g, ev.color.b,
                                     static_cast<uint8_t>(alpha * 255)));
            t.setPosition(20.0f, ey);
            target.draw(t);
            ey -= 22.0f;
        }
    }
}

void ColorConquest::renderGrid(sf::RenderTarget& target) {
    sf::RectangleShape cell(sf::Vector2f(CELL_PX - 1.0f, CELL_PX - 1.0f));

    for (int y = 0; y < m_grid.height(); y++) {
        for (int x = 0; x < m_grid.width(); x++) {
            TeamId owner = m_grid.getCell(x, y);
            float px = GRID_OFFSET_X + x * CELL_PX;
            float py = GRID_OFFSET_Y + y * CELL_PX;

            if (owner == TeamId::None) {
                // Empty cell: subtle grid
                cell.setFillColor(sf::Color(25, 25, 35));
                cell.setOutlineThickness(0);
            } else {
                sf::Color col = getTeamColor(owner);
                // Slight variation for visual interest
                float pulse = 0.9f + 0.1f * std::sin(m_animTime * 2.0f +
                              static_cast<float>(x + y) * 0.5f);
                cell.setFillColor(sf::Color(
                    static_cast<uint8_t>(col.r * pulse),
                    static_cast<uint8_t>(col.g * pulse),
                    static_cast<uint8_t>(col.b * pulse),
                    220));
                cell.setOutlineThickness(1.0f);
                cell.setOutlineColor(sf::Color(
                    static_cast<uint8_t>(std::min(255, col.r + 40)),
                    static_cast<uint8_t>(std::min(255, col.g + 40)),
                    static_cast<uint8_t>(std::min(255, col.b + 40)),
                    100));
            }

            cell.setPosition(px, py);
            target.draw(cell);
        }
    }
}

void ColorConquest::renderCellConquestAnimation(sf::RenderTarget& target) {
    for (const auto& anim : m_conquestAnims) {
        float progress = 1.0f - (anim.timer / CONQUEST_ANIM_TIME);
        float scale = 1.0f + 0.3f * std::sin(progress * 3.14159f);
        float alpha = std::max(0.0f, 1.0f - progress);

        float px = GRID_OFFSET_X + anim.x * CELL_PX + CELL_PX * 0.5f;
        float py = GRID_OFFSET_Y + anim.y * CELL_PX + CELL_PX * 0.5f;

        sf::CircleShape glow(CELL_PX * 0.6f * scale);
        glow.setOrigin(glow.getRadius(), glow.getRadius());
        glow.setPosition(px, py);
        sf::Color col = getTeamColor(anim.newTeam);
        glow.setFillColor(sf::Color(col.r, col.g, col.b,
                          static_cast<uint8_t>(alpha * 120)));
        target.draw(glow);
    }
}

void ColorConquest::renderTeamStats(sf::RenderTarget& target) {
    if (!m_fontLoaded) return;

    // Team stats panels – 4 panels below the grid in a horizontal row (vertical layout)
    float gridBottom = GRID_OFFSET_Y + GRID_H * CELL_PX;
    float panelAreaY = gridBottom + 12.0f;
    float gridPixelW = GRID_W * CELL_PX;
    float gap = 8.0f;
    float panelW = (gridPixelW - 3.0f * gap) / 4.0f;
    float panelH = 100.0f;

    for (int i = 0; i < 4; i++) {
        auto team = static_cast<TeamId>(i + 1);
        const auto& td = m_teams[i];
        float px = GRID_OFFSET_X + i * (panelW + gap);
        float py = panelAreaY;

        // Panel background
        sf::RectangleShape panel(sf::Vector2f(panelW, panelH));
        panel.setPosition(px, py);
        panel.setFillColor(sf::Color(20, 20, 30, 200));
        panel.setOutlineThickness(2.0f);
        panel.setOutlineColor(sf::Color(getTeamColor(team).r,
                                         getTeamColor(team).g,
                                         getTeamColor(team).b, 150));
        target.draw(panel);

        // Team name (centered)
        sf::Text nameTxt;
        nameTxt.setFont(m_font);
        nameTxt.setString(getTeamName(team));
        nameTxt.setCharacterSize(14);
        nameTxt.setFillColor(getTeamColor(team));
        nameTxt.setStyle(sf::Text::Bold);
        auto nb = nameTxt.getLocalBounds();
        nameTxt.setPosition(px + panelW / 2.0f - nb.width / 2.0f, py + 4.0f);
        target.draw(nameTxt);

        // Player count
        sf::Text playersTxt;
        playersTxt.setFont(m_font);
        playersTxt.setString(std::to_string(td.playerCount) + "P");
        playersTxt.setCharacterSize(11);
        playersTxt.setFillColor(sf::Color(180, 180, 200));
        auto ppb = playersTxt.getLocalBounds();
        playersTxt.setPosition(px + panelW / 2.0f - ppb.width / 2.0f, py + 24.0f);
        target.draw(playersTxt);

        // Cell count + percentage
        float totalCells = static_cast<float>(m_grid.totalCells());
        float pct = totalCells > 0 ? static_cast<float>(td.cellCount) / totalCells : 0.0f;

        sf::Text cellsTxt;
        cellsTxt.setFont(m_font);
        cellsTxt.setString(std::to_string(td.cellCount) + " (" +
                           std::to_string(static_cast<int>(pct * 100)) + "%)");
        cellsTxt.setCharacterSize(11);
        cellsTxt.setFillColor(sf::Color(180, 180, 200));
        auto cb = cellsTxt.getLocalBounds();
        cellsTxt.setPosition(px + panelW / 2.0f - cb.width / 2.0f, py + 40.0f);
        target.draw(cellsTxt);

        // Cell bar
        float barW = panelW - 12.0f;
        sf::RectangleShape barBg(sf::Vector2f(barW, 6.0f));
        barBg.setPosition(px + 6.0f, py + 60.0f);
        barBg.setFillColor(sf::Color(40, 40, 50));
        target.draw(barBg);

        sf::RectangleShape barFill(sf::Vector2f(barW * pct, 6.0f));
        barFill.setPosition(px + 6.0f, py + 60.0f);
        barFill.setFillColor(getTeamColor(team));
        target.draw(barFill);

        // Vote info (during playing)
        if (m_phase == Phase::Playing) {
            static const char* arrows[] = {"", "U", "D", "L", "R"};
            std::string voteStr;
            for (int d = 1; d <= 4; d++) {
                if (td.votes[d] > 0)
                    voteStr += std::string(arrows[d]) +
                               std::to_string(td.votes[d]) + " ";
            }
            if (voteStr.empty()) voteStr = "-";

            sf::Text voteTxt;
            voteTxt.setFont(m_font);
            voteTxt.setString(voteStr);
            voteTxt.setCharacterSize(10);
            voteTxt.setFillColor(sf::Color(140, 140, 160));
            auto vb = voteTxt.getLocalBounds();
            voteTxt.setPosition(px + panelW / 2.0f - vb.width / 2.0f, py + 76.0f);
            target.draw(voteTxt);
        }
    }

    // Total players (below team panels)
    sf::Text totalTxt;
    totalTxt.setFont(m_font);
    totalTxt.setString("Total Players: " + std::to_string(m_playerTeam.size()));
    totalTxt.setCharacterSize(13);
    totalTxt.setFillColor(sf::Color(120, 120, 140));
    auto tb = totalTxt.getLocalBounds();
    totalTxt.setPosition(GRID_OFFSET_X + gridPixelW / 2.0f - tb.width / 2.0f,
                         panelAreaY + panelH + 8.0f);
    target.draw(totalTxt);
}

void ColorConquest::renderVoteArrows(sf::RenderTarget& target) {
    if (!m_fontLoaded) return;

    // Show directional arrows around the grid
    float centerX = GRID_OFFSET_X + (GRID_W * CELL_PX) / 2.0f;
    float centerY = GRID_OFFSET_Y + (GRID_H * CELL_PX) / 2.0f;

    float pulse = 0.6f + 0.4f * std::sin(m_animTime * 3.0f);

    struct ArrowInfo {
        const char* text;
        float x, y;
    };

    ArrowInfo arrows[] = {
        {"!up",     centerX, GRID_OFFSET_Y - 28.0f},
        {"!down",   centerX, GRID_OFFSET_Y + GRID_H * CELL_PX + 8.0f},
        {"!left",   GRID_OFFSET_X - 50.0f, centerY},
        {"!right",  GRID_OFFSET_X + GRID_W * CELL_PX + 10.0f, centerY},
    };

    for (auto& a : arrows) {
        sf::Text t;
        t.setFont(m_font);
        t.setString(a.text);
        t.setCharacterSize(16);
        t.setFillColor(sf::Color(200, 200, 220,
                       static_cast<uint8_t>(pulse * 200)));
        auto bounds = t.getLocalBounds();
        t.setOrigin(bounds.width / 2.0f, bounds.height / 2.0f);
        t.setPosition(a.x, a.y);
        target.draw(t);
    }
}

void ColorConquest::renderRoundInfo(sf::RenderTarget& target) {
    if (!m_fontLoaded) return;

    // Title
    sf::Text title;
    title.setFont(m_font);
    title.setString("COLOR CONQUEST");
    title.setCharacterSize(22);
    title.setFillColor(sf::Color(200, 200, 220));
    title.setStyle(sf::Text::Bold);
    title.setPosition(20.0f, 16.0f);
    target.draw(title);

    // Round / Phase info (top-right)
    std::string phaseStr;
    switch (m_phase) {
        case Phase::Lobby:    phaseStr = "LOBBY"; break;
        case Phase::Playing:  phaseStr = "ROUND " + std::to_string(m_currentRound) +
                                         "/" + std::to_string(m_maxRounds); break;
        case Phase::RoundEnd: phaseStr = "RESULTS"; break;
        case Phase::GameOver: phaseStr = "GAME OVER"; break;
    }

    sf::Text phaseTxt;
    phaseTxt.setFont(m_font);
    phaseTxt.setString(phaseStr);
    phaseTxt.setCharacterSize(18);
    phaseTxt.setFillColor(sf::Color(180, 180, 200));
    auto pb = phaseTxt.getLocalBounds();
    phaseTxt.setPosition(GRID_OFFSET_X + GRID_W * CELL_PX - pb.width, 16.0f);
    target.draw(phaseTxt);

    // Timer bar (below title area)
    if (m_phase == Phase::Playing) {
        float pct = m_roundTimer / m_roundDuration;
        pct = std::max(0.0f, std::min(1.0f, pct));

        sf::RectangleShape timerBg(sf::Vector2f(GRID_W * CELL_PX, 4.0f));
        timerBg.setPosition(GRID_OFFSET_X, GRID_OFFSET_Y - 6.0f);
        timerBg.setFillColor(sf::Color(40, 40, 50));
        target.draw(timerBg);

        sf::Color timerColor = pct > 0.3f ? sf::Color(100, 180, 255) :
                                             sf::Color(255, 100, 80);
        sf::RectangleShape timerFill(sf::Vector2f(GRID_W * CELL_PX * pct, 4.0f));
        timerFill.setPosition(GRID_OFFSET_X, GRID_OFFSET_Y - 6.0f);
        timerFill.setFillColor(timerColor);
        target.draw(timerFill);

        // Timer text
        sf::Text timerTxt;
        timerTxt.setFont(m_font);
        timerTxt.setString(std::to_string(static_cast<int>(m_roundTimer + 0.5f)) + "s");
        timerTxt.setCharacterSize(14);
        timerTxt.setFillColor(timerColor);
        float tw = timerTxt.getLocalBounds().width;
        timerTxt.setPosition(GRID_OFFSET_X + GRID_W * CELL_PX / 2.0f - tw / 2.0f,
                            GRID_OFFSET_Y - 24.0f);
        target.draw(timerTxt);
    }
}

void ColorConquest::renderLobby(sf::RenderTarget& target) {
    if (!m_fontLoaded) return;

    float centerX = GRID_OFFSET_X + (GRID_W * CELL_PX) / 2.0f;
    float centerY = GRID_OFFSET_Y + (GRID_H * CELL_PX) / 2.0f;

    // Semi-transparent overlay (vertical layout)
    float overlayW = std::min(GRID_W * CELL_PX + 40.0f, 960.0f);
    sf::RectangleShape overlay(sf::Vector2f(overlayW, 240.0f));
    overlay.setFillColor(sf::Color(10, 10, 20, 220));
    overlay.setOutlineThickness(2.0f);
    overlay.setOutlineColor(sf::Color(100, 100, 180, 180));
    overlay.setOrigin(overlay.getSize().x / 2.0f, overlay.getSize().y / 2.0f);
    overlay.setPosition(centerX, centerY);
    target.draw(overlay);

    // "Join a team!" text
    sf::Text joinTxt;
    joinTxt.setFont(m_font);
    joinTxt.setString("JOIN A TEAM!");
    joinTxt.setCharacterSize(26);
    joinTxt.setFillColor(sf::Color::White);
    joinTxt.setStyle(sf::Text::Bold);
    auto jb = joinTxt.getLocalBounds();
    joinTxt.setPosition(centerX - jb.width / 2.0f, centerY - 100.0f);
    target.draw(joinTxt);

    // Team join instructions (2x2 grid for vertical layout)
    std::string teams[] = {"!join red", "!join blue", "!join green", "!join yellow"};
    sf::Color teamColors[] = {getTeamColor(TeamId::Red), getTeamColor(TeamId::Blue),
                              getTeamColor(TeamId::Green), getTeamColor(TeamId::Yellow)};

    float colSpacing = 200.0f;
    float rowSpacing = 32.0f;
    for (int i = 0; i < 4; i++) {
        int col = i % 2;
        int row = i / 2;
        sf::Text t;
        t.setFont(m_font);
        t.setString(teams[i]);
        t.setCharacterSize(18);
        t.setFillColor(teamColors[i]);
        auto tb = t.getLocalBounds();
        float tx = centerX + (col == 0 ? -colSpacing / 2.0f - tb.width / 2.0f
                                        :  colSpacing / 2.0f - tb.width / 2.0f);
        t.setPosition(tx, centerY - 50.0f + row * rowSpacing);
        target.draw(t);
    }

    // "or !join for auto-assign"
    sf::Text autoTxt;
    autoTxt.setFont(m_font);
    autoTxt.setString("or just !join for auto-assign");
    autoTxt.setCharacterSize(14);
    autoTxt.setFillColor(sf::Color(140, 140, 160));
    auto ab = autoTxt.getLocalBounds();
    autoTxt.setPosition(centerX - ab.width / 2.0f, centerY + 20.0f);
    target.draw(autoTxt);

    // Player count + waiting message
    int total = static_cast<int>(m_playerTeam.size());
    std::string waitStr = std::to_string(total) + " / " +
                          std::to_string(MIN_PLAYERS_TO_START) + " players";
    if (total >= MIN_PLAYERS_TO_START) {
        int remaining = static_cast<int>(m_lobbyDuration - m_lobbyTimer);
        if (remaining < 0) remaining = 0;
        waitStr += "  -  Starting in " + std::to_string(remaining) + "s";
    } else {
        waitStr += "  -  Waiting for players...";
    }

    sf::Text waitTxt;
    waitTxt.setFont(m_font);
    waitTxt.setString(waitStr);
    waitTxt.setCharacterSize(16);
    waitTxt.setFillColor(sf::Color(180, 180, 200));
    auto wb = waitTxt.getLocalBounds();
    waitTxt.setPosition(centerX - wb.width / 2.0f, centerY + 60.0f);
    target.draw(waitTxt);
}

void ColorConquest::renderGameOver(sf::RenderTarget& target) {
    if (!m_fontLoaded) return;

    float centerX = GRID_OFFSET_X + (GRID_W * CELL_PX) / 2.0f;
    float centerY = GRID_OFFSET_Y + (GRID_H * CELL_PX) / 2.0f;

    // Overlay (fits vertical 1080 width)
    float overlayW = std::min(560.0f, GRID_W * CELL_PX);
    sf::RectangleShape overlay(sf::Vector2f(overlayW, 300.0f));
    overlay.setFillColor(sf::Color(10, 10, 20, 230));
    overlay.setOutlineThickness(3.0f);
    overlay.setOutlineColor(sf::Color(255, 215, 0, 200));
    overlay.setOrigin(overlayW / 2.0f, 150.0f);
    overlay.setPosition(centerX, centerY);
    target.draw(overlay);

    sf::Text goTxt;
    goTxt.setFont(m_font);
    goTxt.setString("GAME OVER");
    goTxt.setCharacterSize(32);
    goTxt.setFillColor(sf::Color(255, 215, 0));
    goTxt.setStyle(sf::Text::Bold);
    auto gb = goTxt.getLocalBounds();
    goTxt.setPosition(centerX - gb.width / 2.0f, centerY - 120.0f);
    target.draw(goTxt);

    // Ranked results
    struct Result { TeamId team; int cells; };
    std::array<Result, 4> results;
    for (int i = 0; i < 4; i++) {
        results[i] = {static_cast<TeamId>(i + 1), m_teams[i].cellCount};
    }
    std::sort(results.begin(), results.end(),
              [](const Result& a, const Result& b) { return a.cells > b.cells; });

    static const char* medals[] = {"1st", "2nd", "3rd", "4th"};
    for (int i = 0; i < 4; i++) {
        sf::Text t;
        t.setFont(m_font);
        t.setString(std::string(medals[i]) + "  " +
                    getTeamName(results[i].team) + "  " +
                    std::to_string(results[i].cells) + " cells");
        t.setCharacterSize(18);
        t.setFillColor(i == 0 ? sf::Color(255, 215, 0) : getTeamColor(results[i].team));
        auto tb = t.getLocalBounds();
        t.setPosition(centerX - tb.width / 2.0f, centerY - 60.0f + i * 36.0f);
        target.draw(t);
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// State & Commands (for Web Dashboard)
// ═══════════════════════════════════════════════════════════════════════════════

bool ColorConquest::isRoundComplete() const {
    return m_phase == Phase::RoundEnd || m_phase == Phase::GameOver
        || m_phase == Phase::Lobby;
}

bool ColorConquest::isGameOver() const {
    return m_phase == Phase::GameOver;
}

nlohmann::json ColorConquest::getState() const {
    nlohmann::json state;
    state["phase"] = static_cast<int>(m_phase);
    state["round"] = m_currentRound;
    state["maxRounds"] = m_maxRounds;
    state["roundTimer"] = m_roundTimer;
    state["playerCount"] = m_playerTeam.size();

    nlohmann::json teamsJson = nlohmann::json::array();
    for (int i = 0; i < 4; i++) {
        teamsJson.push_back({
            {"name", m_teams[i].name},
            {"players", m_teams[i].playerCount},
            {"cells", m_teams[i].cellCount},
            {"votes", {
                {"up", m_teams[i].votes[1]},
                {"down", m_teams[i].votes[2]},
                {"left", m_teams[i].votes[3]},
                {"right", m_teams[i].votes[4]}
            }}
        });
    }
    state["teams"] = teamsJson;

    return state;
}

nlohmann::json ColorConquest::getCommands() const {
    return nlohmann::json::array({
        {{"command", "!join [team]"}, {"description", "Join a team (red/blue/green/yellow) or auto-assign"},
         {"aliases", nlohmann::json::array({"!play"})}},
        {{"command", "!up"}, {"description", "Vote to expand upward"},
         {"aliases", nlohmann::json::array({"!u", "!w", "!north"})}},
        {{"command", "!down"}, {"description", "Vote to expand downward"},
         {"aliases", nlohmann::json::array({"!d", "!s", "!south"})}},
        {{"command", "!left"}, {"description", "Vote to expand left"},
         {"aliases", nlohmann::json::array({"!l", "!a", "!west"})}},
        {{"command", "!right"}, {"description", "Vote to expand right"},
         {"aliases", nlohmann::json::array({"!r", "!e", "!east"})}},
        {{"command", "!emote [text]"}, {"description", "Send a team emote"},
         {"aliases", nlohmann::json::array()}}
    });
}

// ═══════════════════════════════════════════════════════════════════════════════
// Helpers
// ═══════════════════════════════════════════════════════════════════════════════

TeamId ColorConquest::parseTeamName(const std::string& name) const {
    if (name == "red"    || name == "r" || name == "rot")   return TeamId::Red;
    if (name == "blue"   || name == "b" || name == "blau")  return TeamId::Blue;
    if (name == "green"  || name == "g" || name == "gruen") return TeamId::Green;
    if (name == "yellow" || name == "y" || name == "gelb")  return TeamId::Yellow;
    return TeamId::None;
}

Direction ColorConquest::parseDirection(const std::string& cmd) const {
    if (cmd == "up"    || cmd == "u" || cmd == "w" || cmd == "north") return Direction::Up;
    if (cmd == "down"  || cmd == "d" || cmd == "s" || cmd == "south") return Direction::Down;
    if (cmd == "left"  || cmd == "l" || cmd == "a" || cmd == "west")  return Direction::Left;
    if (cmd == "right" || cmd == "r" || cmd == "e" || cmd == "east")  return Direction::Right;
    return Direction::None;
}

sf::Color ColorConquest::getTeamColor(TeamId team) const {
    switch (team) {
        case TeamId::Red:    return sf::Color(220, 60,  60);
        case TeamId::Blue:   return sf::Color(60,  120, 220);
        case TeamId::Green:  return sf::Color(60,  200, 80);
        case TeamId::Yellow: return sf::Color(230, 200, 50);
        default:             return sf::Color(80,  80,  80);
    }
}

sf::Color ColorConquest::getTeamColorDark(TeamId team) const {
    switch (team) {
        case TeamId::Red:    return sf::Color(120, 30,  30);
        case TeamId::Blue:   return sf::Color(30,  60,  120);
        case TeamId::Green:  return sf::Color(30,  100, 40);
        case TeamId::Yellow: return sf::Color(120, 100, 25);
        default:             return sf::Color(40,  40,  40);
    }
}

std::string ColorConquest::getTeamName(TeamId team) const {
    switch (team) {
        case TeamId::Red:    return "Red";
        case TeamId::Blue:   return "Blue";
        case TeamId::Green:  return "Green";
        case TeamId::Yellow: return "Yellow";
        default:             return "None";
    }
}

Direction ColorConquest::getWinningVote(TeamId team) const {
    int idx = static_cast<int>(team) - 1;
    if (idx < 0 || idx >= 4) return Direction::None;
    return m_teams[idx].topVote();
}

} // namespace is::games::color_conquest
