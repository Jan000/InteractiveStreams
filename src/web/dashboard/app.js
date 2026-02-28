// ═══════════════════════════════════════════════════════════════════════════
// InteractiveStreams – Admin Dashboard JavaScript
// ═══════════════════════════════════════════════════════════════════════════

const API_BASE = '';
const POLL_INTERVAL = 1000; // ms

// ─── Phase definitions per game ──────────────────────────────────────────
const GAME_PHASES = {
    chaos_arena: {
        names:  { 0: 'Lobby', 1: 'Countdown', 2: 'Battle', 3: 'Round End', 4: 'Game Over' },
        colors: { 0: '#58a6ff', 1: '#d29922', 2: '#3fb950', 3: '#bc8cff', 4: '#f85149' },
        battlePhase: 2,
        maxRoundTime: 120,
    },
    color_conquest: {
        names:  { 0: 'Lobby', 1: 'Voting', 2: 'Results', 3: 'Game Over' },
        colors: { 0: '#58a6ff', 1: '#3fb950', 2: '#bc8cff', 3: '#f85149' },
        battlePhase: 1,
        maxRoundTime: 8,
    }
};

// ─── Quick-command definitions per game ──────────────────────────────────
const GAME_QUICK_CMDS = {
    chaos_arena: ['!join', '!left', '!right', '!jump', '!attack', '!special', '!dash', '!block'],
    color_conquest: ['!join', '!join red', '!join blue', '!join green', '!join yellow', '!up', '!down', '!left', '!right'],
};

const GAME_RANDOM_CMDS = {
    chaos_arena: ['!left', '!right', '!jump', '!attack', '!special', '!dash', '!block'],
    color_conquest: ['!up', '!down', '!left', '!right'],
};

const TEAM_COLORS = {
    Red: '#dc3c3c', Blue: '#3c78dc', Green: '#3cc850', Yellow: '#e6c832'
};

let currentGameId = '';

// ─── DOM References ──────────────────────────────────────────────────────
const el = {
    statusDot:       document.getElementById('status-indicator'),
    statusText:      document.getElementById('status-text'),
    roundTimer:      document.getElementById('round-timer'),
    gameStatsGrid:   document.getElementById('game-stats-grid'),
    streamStatus:    document.getElementById('stream-status'),
    streamFps:       document.getElementById('stream-fps'),
    streamFrames:    document.getElementById('stream-frames'),
    streamTargets:   document.getElementById('stream-targets'),
    platformsList:   document.getElementById('platforms-list'),
    commandsTable:   document.getElementById('commands-table').querySelector('tbody'),
    gameDetailTitle: document.getElementById('game-detail-title'),
    gameDetailBody:  document.getElementById('game-detail-body'),
    leaderboardBody: document.getElementById('leaderboard-body'),
    leaderboardTable:document.getElementById('leaderboard-table'),
    playerCountBadge:document.getElementById('player-count-badge'),
    lastUpdate:      document.getElementById('last-update'),
    gameSelector:    document.getElementById('game-selector'),
    switchMode:      document.getElementById('switch-mode'),
    btnSwitchGame:   document.getElementById('btn-switch-game'),
    btnCancelSwitch: document.getElementById('btn-cancel-switch'),
    pendingBanner:   document.getElementById('pending-switch-banner'),
    pendingGameName: document.getElementById('pending-game-name'),
    btnShutdown:     document.getElementById('btn-shutdown'),
    chatMessages:    document.getElementById('chat-messages'),
    chatInput:       document.getElementById('chat-input'),
    chatUsername:     document.getElementById('chat-username'),
    btnSendChat:     document.getElementById('btn-send-chat'),
    chatQuickButtons:document.getElementById('chat-quick-buttons'),
};

// ─── API Calls ───────────────────────────────────────────────────────────
async function fetchStatus() {
    try {
        const res = await fetch(`${API_BASE}/api/status`);
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        const data = await res.json();
        updateDashboard(data);
        setOnline(true);
    } catch (e) {
        setOnline(false);
        console.warn('Failed to fetch status:', e.message);
    }
}

async function fetchGames() {
    try {
        const res = await fetch(`${API_BASE}/api/games`);
        if (!res.ok) return;
        const games = await res.json();
        populateGameSelector(games);
    } catch (e) {
        console.warn('Failed to fetch games:', e.message);
    }
}

function populateGameSelector(games) {
    const current = el.gameSelector.value;
    el.gameSelector.innerHTML = '';
    for (const g of games) {
        const opt = document.createElement('option');
        opt.value = g.id;
        opt.textContent = g.name;
        opt.title = g.description || '';
        el.gameSelector.appendChild(opt);
    }
    if (current) el.gameSelector.value = current;
}

async function switchGame() {
    const gameName = el.gameSelector.value;
    const mode = el.switchMode.value;
    if (!gameName) return;

    try {
        await fetch(`${API_BASE}/api/games/switch`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ game: gameName, mode })
        });
    } catch (e) {
        console.error('Failed to switch game:', e);
    }
}

async function cancelSwitch() {
    try {
        await fetch(`${API_BASE}/api/games/cancel-switch`, { method: 'POST' });
    } catch (e) {
        console.error('Failed to cancel switch:', e);
    }
}

async function loadGame(gameName) {
    try {
        await fetch(`${API_BASE}/api/games/load`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ game: gameName })
        });
    } catch (e) {
        console.error('Failed to load game:', e);
    }
}

async function shutdownServer() {
    if (!confirm('Are you sure you want to shut down the server?')) return;
    try {
        await fetch(`${API_BASE}/api/shutdown`, { method: 'POST' });
    } catch (e) {
        console.error('Shutdown request failed:', e);
    }
}

async function sendChatMessage(username, text) {
    if (!text.trim()) return;
    try {
        const res = await fetch(`${API_BASE}/api/chat`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ username, text })
        });
        if (res.ok) {
            appendChatMessage(username, text, false);
            el.chatInput.value = '';
            el.chatInput.focus();
        }
    } catch (e) {
        console.error('Failed to send chat message:', e);
    }
}

function appendChatMessage(sender, text, outgoing = false) {
    const color = PLAYER_COLORS[sender] || 'var(--accent)';
    const div = document.createElement('div');
    div.className = `chat-msg ${outgoing ? 'outgoing' : ''}`;
    div.innerHTML = `<span class="chat-sender" style="color:${color}">${escapeHtml(sender)}:</span> <span class="chat-text">${escapeHtml(text)}</span>`;
    el.chatMessages.appendChild(div);
    el.chatMessages.scrollTop = el.chatMessages.scrollHeight;

    // Keep max 200 messages
    while (el.chatMessages.children.length > 201) {
        el.chatMessages.removeChild(el.chatMessages.children[1]); // keep hint
    }
}

// ─── Update Functions ────────────────────────────────────────────────────
function setOnline(online) {
    el.statusDot.className = `status-dot ${online ? 'online' : 'offline'}`;
    el.statusText.textContent = online ? 'Online' : 'Offline';
}

function updateDashboard(data) {
    // Game state – adaptive to current game
    if (data.game) {
        const gameId = data.game.id || '';
        const gameName = data.game.name || gameId;
        const state = data.game.state || {};
        const phase = state.phase ?? 0;

        // Update selector to highlight active game
        if (el.gameSelector.value !== gameId) {
            el.gameSelector.value = gameId;
        }

        // Detect game change and update UI
        if (gameId !== currentGameId) {
            currentGameId = gameId;
            updateQuickButtons(gameId);
        }

        const phases = GAME_PHASES[gameId] || {
            names: { 0: 'Phase ' + phase }, colors: {}, battlePhase: 0, maxRoundTime: 10
        };

        // Stats grid – dynamically build based on game
        updateGameStats(gameId, state, phase, phases);

        // Round timer
        const isBattle = phase === phases.battlePhase;
        if (state.roundTimer != null && isBattle) {
            const maxTime = phases.maxRoundTime || 10;
            const pct = Math.max(0, Math.min(100, (state.roundTimer / maxTime) * 100));
            el.roundTimer.style.width = `${pct}%`;
            el.roundTimer.style.background = pct > 30 ? 'var(--accent)' : 'var(--red)';
        } else {
            el.roundTimer.style.width = '0%';
        }

        // Game detail section (players/teams)
        updateGameDetail(gameId, state);

        // Leaderboard
        updateLeaderboard(gameId, state);

        // Commands
        if (data.game.commands) {
            updateCommands(data.game.commands);
        }
    }

    // Pending switch banner
    if (data.pendingSwitch) {
        el.pendingBanner.style.display = 'flex';
        el.pendingGameName.textContent = data.pendingSwitch.game;
    } else {
        el.pendingBanner.style.display = 'none';
    }

    // Streaming
    if (data.streaming) {
        const streaming = data.streaming;
        const isEncoding = streaming.encoding;
        el.streamStatus.textContent = isEncoding ? 'Live' : 'Offline';
        el.streamStatus.className = `badge ${isEncoding ? 'badge-green' : 'badge-red'}`;
        el.streamFps.textContent = (streaming.fps || 0).toFixed(1);
        el.streamFrames.textContent = formatNumber(streaming.frames || 0);

        if (streaming.targets) {
            el.streamTargets.innerHTML = streaming.targets.map(t => `
                <div class="target-item">
                    <span class="target-name">${t.name} (${t.platform})</span>
                    <span class="badge ${t.enabled ? 'badge-green' : 'badge-red'}">
                        ${t.enabled ? 'Active' : 'Disabled'}
                    </span>
                </div>
            `).join('');
        }
    }

    // Platforms
    if (data.platforms) {
        updatePlatforms(data.platforms);
    }

    // Timestamp
    el.lastUpdate.textContent = new Date().toLocaleTimeString();
}

function updateGameStats(gameId, state, phase, phases) {
    const phaseName = phases.names[phase] || `Phase ${phase}`;
    const phaseColor = phases.colors[phase] || '#e6edf3';

    let statsHtml = `
        <div class="stat">
            <span class="stat-value" style="color:${phaseColor}">${phaseName}</span>
            <span class="stat-label">Phase</span>
        </div>
        <div class="stat">
            <span class="stat-value">${state.round || 0}/${state.maxRounds || '?'}</span>
            <span class="stat-label">Round</span>
        </div>
        <div class="stat">
            <span class="stat-value">${state.playerCount || 0}</span>
            <span class="stat-label">Players</span>
        </div>
    `;

    if (gameId === 'chaos_arena') {
        statsHtml += `
            <div class="stat">
                <span class="stat-value">${state.particles || 0}</span>
                <span class="stat-label">Particles</span>
            </div>
        `;
    } else if (gameId === 'color_conquest' && state.teams) {
        const totalCells = state.teams.reduce((s, t) => s + (t.cells || 0), 0);
        statsHtml += `
            <div class="stat">
                <span class="stat-value">${totalCells}</span>
                <span class="stat-label">Cells Claimed</span>
            </div>
        `;
    }

    el.gameStatsGrid.innerHTML = statsHtml;
}

function updateGameDetail(gameId, state) {
    if (gameId === 'chaos_arena') {
        el.gameDetailTitle.textContent = '👥 Players';
        updateChaosArenaPlayers(state.players || []);
        el.playerCountBadge.textContent = (state.players || []).length;
        document.getElementById('card-leaderboard').style.display = '';
    } else if (gameId === 'color_conquest') {
        el.gameDetailTitle.textContent = '🗺 Teams';
        updateColorConquestTeams(state.teams || [], state.playerCount || 0);
        el.playerCountBadge.textContent = state.playerCount || 0;
        document.getElementById('card-leaderboard').style.display = 'none';
    } else {
        el.gameDetailTitle.textContent = '📊 Game Details';
        el.gameDetailBody.innerHTML = `<pre>${JSON.stringify(state, null, 2)}</pre>`;
        el.playerCountBadge.textContent = state.playerCount || 0;
    }
}

function updateChaosArenaPlayers(players) {
    players.sort((a, b) => (b.score || 0) - (a.score || 0));

    el.gameDetailBody.innerHTML = `
        <table class="players-table">
            <thead>
                <tr><th>Name</th><th>Health</th><th>Kills</th><th>Deaths</th><th>Score</th><th>Status</th></tr>
            </thead>
            <tbody>
                ${players.map(p => {
                    const healthPct = Math.max(0, Math.min(100, (p.health / 100) * 100));
                    const healthClass = healthPct > 50 ? 'high' : (healthPct > 25 ? 'mid' : 'low');
                    return `
                        <tr>
                            <td><strong>${escapeHtml(p.name)}</strong></td>
                            <td>
                                <div class="health-bar">
                                    <div class="health-fill ${healthClass}" style="width:${healthPct}%"></div>
                                </div>
                                <span style="font-size:11px;color:var(--text-muted);margin-left:6px">${Math.round(p.health)}</span>
                            </td>
                            <td>${p.kills || 0}</td>
                            <td>${p.deaths || 0}</td>
                            <td><strong>${p.score || 0}</strong></td>
                            <td><span class="${p.alive ? 'status-alive' : 'status-dead'}">${p.alive ? '● Alive' : '✕ Dead'}</span></td>
                        </tr>
                    `;
                }).join('')}
            </tbody>
        </table>
    `;
}

function updateColorConquestTeams(teams, totalPlayers) {
    const totalCells = teams.reduce((s, t) => s + (t.cells || 0), 0) || 1;
    const teamNames = ['Red', 'Blue', 'Green', 'Yellow'];

    // Territory bar
    let barHtml = '<div class="team-bar">';
    for (let i = 0; i < teams.length; i++) {
        const pct = ((teams[i].cells || 0) / totalCells * 100).toFixed(1);
        const color = TEAM_COLORS[teamNames[i]] || '#888';
        barHtml += `<div class="team-bar-segment" style="width:${pct}%;background:${color}"></div>`;
    }
    barHtml += '</div>';

    // Team cards
    let cardsHtml = '';
    for (let i = 0; i < teams.length; i++) {
        const t = teams[i];
        const color = TEAM_COLORS[teamNames[i]] || '#888';
        const pct = ((t.cells || 0) / totalCells * 100).toFixed(0);

        let voteStr = '';
        if (t.votes) {
            const arrows = { up: '↑', down: '↓', left: '←', right: '→' };
            for (const [dir, count] of Object.entries(t.votes)) {
                if (count > 0) voteStr += ` ${arrows[dir] || dir}${count}`;
            }
        }

        cardsHtml += `
            <div class="team-card" style="border-left-color:${color}">
                <div>
                    <div class="team-name" style="color:${color}">${t.name || teamNames[i]}</div>
                    <div class="team-stats">
                        ${t.players || 0} players · ${t.cells || 0} cells (${pct}%)
                        ${voteStr ? `· Votes:${voteStr}` : ''}
                    </div>
                </div>
            </div>
        `;
    }

    el.gameDetailBody.innerHTML = barHtml + cardsHtml;
}

function updateLeaderboard(gameId, state) {
    if (gameId === 'chaos_arena') {
        const leaderboard = state.leaderboard || [];
        leaderboard.sort((a, b) => (b.score || 0) - (a.score || 0));

        const thead = el.leaderboardTable.querySelector('thead tr');
        thead.innerHTML = '<th>#</th><th>Name</th><th>Kills</th><th>Wins</th><th>Score</th>';

        const tbody = el.leaderboardTable.querySelector('tbody');
        tbody.innerHTML = leaderboard.map((entry, i) => `
            <tr>
                <td>${i === 0 ? '🥇' : i === 1 ? '🥈' : i === 2 ? '🥉' : i + 1}</td>
                <td><strong>${escapeHtml(entry.name)}</strong></td>
                <td>${entry.kills || 0}</td>
                <td>${entry.wins || 0}</td>
                <td><strong>${entry.score || 0}</strong></td>
            </tr>
        `).join('');
    }
    // Color Conquest doesn't have a separate leaderboard
}

function updatePlatforms(platforms) {
    const icons = { twitch: '📺', youtube: '▶️' };

    el.platformsList.innerHTML = platforms.map(p => `
        <div class="platform-item">
            <div class="platform-info">
                <div class="platform-icon">${icons[p.platform] || '🌐'}</div>
                <div>
                    <div class="platform-name">${p.displayName}</div>
                    <div class="platform-detail">
                        ${p.channel || p.channelId || '—'} ·
                        ${p.messagesReceived || 0} msgs received
                    </div>
                </div>
            </div>
            <span class="badge ${p.connected ? 'badge-green' : 'badge-red'}">
                ${p.connected ? 'Connected' : 'Disconnected'}
            </span>
        </div>
    `).join('');
}

function updateCommands(commands) {
    el.commandsTable.innerHTML = commands.map(cmd => `
        <tr>
            <td><code style="color:var(--accent)">${cmd.command}</code></td>
            <td>${cmd.description}</td>
            <td style="color:var(--text-muted)">
                ${(cmd.aliases || []).map(a => `<code>${a}</code>`).join(', ')}
            </td>
        </tr>
    `).join('');
}

// ─── Utilities ───────────────────────────────────────────────────────────
function escapeHtml(text) {
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}

function formatNumber(n) {
    if (n >= 1000000) return (n / 1000000).toFixed(1) + 'M';
    if (n >= 1000) return (n / 1000).toFixed(1) + 'K';
    return n.toString();
}

// ─── Event Listeners ─────────────────────────────────────────────────────
el.btnSwitchGame.addEventListener('click', switchGame);
el.btnCancelSwitch.addEventListener('click', cancelSwitch);
el.btnShutdown.addEventListener('click', shutdownServer);

// ─── Quick-command buttons (dynamic per game) ────────────────────────────
function updateQuickButtons(gameId) {
    const cmds = GAME_QUICK_CMDS[gameId] || ['!join'];
    el.chatQuickButtons.innerHTML = cmds.map(cmd =>
        `<button class="btn btn-sm chat-quick" data-cmd="${escapeHtml(cmd)}">${escapeHtml(cmd)}</button>`
    ).join('');

    // Re-attach listeners
    el.chatQuickButtons.querySelectorAll('.chat-quick').forEach(btn => {
        btn.addEventListener('click', () => {
            sendChatMessage(el.chatUsername.value || activePlayer, btn.dataset.cmd);
        });
    });
}

// ─── Chat controls ───────────────────────────────────────────────────────
const PLAYER_COLORS = {
    'Player1': '#58a6ff',
    'Player2': '#3fb950',
    'Player3': '#d29922',
    'Player4': '#bc8cff'
};

let activePlayer = 'Player1';
let autoPlayInterval = null;

function setActivePlayer(name) {
    activePlayer = name;
    el.chatUsername.value = name;
    el.chatUsername.style.color = PLAYER_COLORS[name] || 'var(--accent)';

    // Update tab styling
    document.querySelectorAll('.player-tab').forEach(tab => {
        tab.classList.toggle('active', tab.dataset.player === name);
    });
}

// Player tab clicks
document.querySelectorAll('.player-tab[data-player]').forEach(tab => {
    tab.addEventListener('click', () => {
        setActivePlayer(tab.dataset.player);
    });
});

// Add custom player
document.getElementById('btn-add-player').addEventListener('click', () => {
    const name = prompt('Enter player name:');
    if (!name || !name.trim()) return;
    const cleanName = name.trim().substring(0, 20);

    // Check if tab already exists
    if (document.querySelector(`.player-tab[data-player="${cleanName}"]`)) {
        setActivePlayer(cleanName);
        return;
    }

    // Generate random color
    const hue = Math.floor(Math.random() * 360);
    const color = `hsl(${hue}, 70%, 60%)`;
    PLAYER_COLORS[cleanName] = color;

    // Create new tab
    const btn = document.createElement('button');
    btn.className = 'player-tab';
    btn.dataset.player = cleanName;
    btn.style.setProperty('--tab-color', color);
    btn.innerHTML = `<span class="tab-dot" style="background:${color}"></span>${escapeHtml(cleanName)}`;
    btn.addEventListener('click', () => setActivePlayer(cleanName));

    // Insert before the "+" button
    const addBtn = document.getElementById('btn-add-player');
    addBtn.parentNode.insertBefore(btn, addBtn);

    setActivePlayer(cleanName);
});

el.btnSendChat.addEventListener('click', () => {
    sendChatMessage(el.chatUsername.value || activePlayer, el.chatInput.value);
});

el.chatInput.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') {
        e.preventDefault();
        sendChatMessage(el.chatUsername.value || activePlayer, el.chatInput.value);
    }
});

// ─── Bulk actions ────────────────────────────────────────────────────
function getAllPlayerNames() {
    return Array.from(document.querySelectorAll('.player-tab[data-player]'))
        .map(t => t.dataset.player);
}

document.getElementById('btn-join-all').addEventListener('click', async () => {
    const players = getAllPlayerNames();
    for (const p of players) {
        // For Color Conquest, auto-assign teams
        if (currentGameId === 'color_conquest') {
            const teams = ['red', 'blue', 'green', 'yellow'];
            const idx = players.indexOf(p) % teams.length;
            await sendChatMessage(p, `!join ${teams[idx]}`);
        } else {
            await sendChatMessage(p, '!join');
        }
        await new Promise(r => setTimeout(r, 100));
    }
});

document.getElementById('btn-random-actions').addEventListener('click', async () => {
    const players = getAllPlayerNames();
    const cmds = GAME_RANDOM_CMDS[currentGameId] || ['!left', '!right'];
    for (const p of players) {
        const cmd = cmds[Math.floor(Math.random() * cmds.length)];
        await sendChatMessage(p, cmd);
        await new Promise(r => setTimeout(r, 50));
    }
});

document.getElementById('btn-auto-play').addEventListener('click', () => {
    const statusEl = document.getElementById('auto-play-status');
    if (autoPlayInterval) {
        clearInterval(autoPlayInterval);
        autoPlayInterval = null;
        statusEl.textContent = 'OFF';
        statusEl.style.color = '';
    } else {
        statusEl.textContent = 'ON';
        statusEl.style.color = 'var(--green)';
        autoPlayInterval = setInterval(async () => {
            const players = getAllPlayerNames();
            const cmds = GAME_RANDOM_CMDS[currentGameId] || ['!left', '!right'];
            // Each tick, 1-3 random players perform a random action
            const count = Math.min(players.length, 1 + Math.floor(Math.random() * 3));
            const shuffled = [...players].sort(() => Math.random() - 0.5);
            for (let i = 0; i < count; i++) {
                const cmd = cmds[Math.floor(Math.random() * cmds.length)];
                await sendChatMessage(shuffled[i], cmd);
            }
        }, 500);
    }
});

// ─── Initialize ──────────────────────────────────────────────────────────
fetchGames();
fetchStatus();
setInterval(fetchStatus, POLL_INTERVAL);
// Refresh game list every 30s in case new games are registered
setInterval(fetchGames, 30000);
