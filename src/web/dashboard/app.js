// ═══════════════════════════════════════════════════════════════════════════
// InteractiveStreams – Admin Dashboard JavaScript
// ═══════════════════════════════════════════════════════════════════════════

const API_BASE = '';
const POLL_INTERVAL = 1000; // ms

// ─── Phase names ─────────────────────────────────────────────────────────
const PHASE_NAMES = {
    0: 'Lobby',
    1: 'Countdown',
    2: 'Battle',
    3: 'Round End',
    4: 'Game Over'
};

const PHASE_COLORS = {
    0: '#58a6ff',
    1: '#d29922',
    2: '#3fb950',
    3: '#bc8cff',
    4: '#f85149'
};

// ─── DOM References ──────────────────────────────────────────────────────
const el = {
    statusDot:       document.getElementById('status-indicator'),
    statusText:      document.getElementById('status-text'),
    gamePhase:       document.getElementById('game-phase'),
    gameRound:       document.getElementById('game-round'),
    gamePlayers:     document.getElementById('game-players'),
    gameParticles:   document.getElementById('game-particles'),
    roundTimer:      document.getElementById('round-timer'),
    streamStatus:    document.getElementById('stream-status'),
    streamFps:       document.getElementById('stream-fps'),
    streamFrames:    document.getElementById('stream-frames'),
    streamTargets:   document.getElementById('stream-targets'),
    platformsList:   document.getElementById('platforms-list'),
    commandsTable:   document.getElementById('commands-table').querySelector('tbody'),
    playersTable:    document.getElementById('players-table').querySelector('tbody'),
    leaderboardTable:document.getElementById('leaderboard-table').querySelector('tbody'),
    playerCountBadge:document.getElementById('player-count-badge'),
    lastUpdate:      document.getElementById('last-update'),
    gameSelector:    document.getElementById('game-selector'),
    btnShutdown:     document.getElementById('btn-shutdown'),
    chatMessages:    document.getElementById('chat-messages'),
    chatInput:       document.getElementById('chat-input'),
    chatUsername:    document.getElementById('chat-username'),
    btnSendChat:     document.getElementById('btn-send-chat'),
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
    // Game state
    if (data.game && data.game.state) {
        const state = data.game.state;
        const phase = state.phase ?? 0;

        el.gamePhase.textContent = PHASE_NAMES[phase] || 'Unknown';
        el.gamePhase.style.color = PHASE_COLORS[phase] || '#e6edf3';
        el.gameRound.textContent = `${state.round || 0}/${state.maxRounds || 5}`;
        el.gamePlayers.textContent = state.playerCount || 0;
        el.gameParticles.textContent = state.particles || 0;

        // Round timer
        if (state.roundTimer != null && phase === 2) {
            const maxTime = 120; // match roundDuration
            const pct = Math.max(0, Math.min(100, (state.roundTimer / maxTime) * 100));
            el.roundTimer.style.width = `${pct}%`;
            el.roundTimer.style.background = pct > 30 ? 'var(--accent)' : 'var(--red)';
        } else {
            el.roundTimer.style.width = '0%';
        }

        // Players table
        updatePlayersTable(state.players || []);

        // Leaderboard
        updateLeaderboard(state.leaderboard || []);

        // Commands
        if (data.game.commands) {
            updateCommands(data.game.commands);
        }
    }

    // Streaming
    if (data.streaming) {
        const streaming = data.streaming;
        const isEncoding = streaming.encoding;
        el.streamStatus.textContent = isEncoding ? 'Live' : 'Offline';
        el.streamStatus.className = `badge ${isEncoding ? 'badge-green' : 'badge-red'}`;
        el.streamFps.textContent = (streaming.fps || 0).toFixed(1);
        el.streamFrames.textContent = formatNumber(streaming.frames || 0);

        // Targets
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

function updatePlayersTable(players) {
    el.playerCountBadge.textContent = players.length;

    // Sort by score descending
    players.sort((a, b) => (b.score || 0) - (a.score || 0));

    el.playersTable.innerHTML = players.map(p => {
        const healthPct = Math.max(0, Math.min(100, (p.health / 100) * 100));
        const healthClass = healthPct > 50 ? 'high' : (healthPct > 25 ? 'mid' : 'low');
        return `
            <tr>
                <td><strong>${escapeHtml(p.name)}</strong></td>
                <td>
                    <div class="health-bar">
                        <div class="health-fill ${healthClass}" style="width:${healthPct}%"></div>
                    </div>
                    <span style="font-size:11px;color:var(--text-muted);margin-left:6px">
                        ${Math.round(p.health)}
                    </span>
                </td>
                <td>${p.kills || 0}</td>
                <td>${p.deaths || 0}</td>
                <td><strong>${p.score || 0}</strong></td>
                <td>
                    <span class="${p.alive ? 'status-alive' : 'status-dead'}">
                        ${p.alive ? '● Alive' : '✕ Dead'}
                    </span>
                </td>
            </tr>
        `;
    }).join('');
}

function updateLeaderboard(leaderboard) {
    leaderboard.sort((a, b) => (b.score || 0) - (a.score || 0));

    el.leaderboardTable.innerHTML = leaderboard.map((entry, i) => `
        <tr>
            <td>${i === 0 ? '🥇' : i === 1 ? '🥈' : i === 2 ? '🥉' : i + 1}</td>
            <td><strong>${escapeHtml(entry.name)}</strong></td>
            <td>${entry.kills || 0}</td>
            <td>${entry.wins || 0}</td>
            <td><strong>${entry.score || 0}</strong></td>
        </tr>
    `).join('');
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
el.gameSelector.addEventListener('change', (e) => {
    loadGame(e.target.value);
});

el.btnShutdown.addEventListener('click', shutdownServer);

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

// Quick-command buttons
document.querySelectorAll('.chat-quick').forEach(btn => {
    btn.addEventListener('click', () => {
        sendChatMessage(el.chatUsername.value || activePlayer, btn.dataset.cmd);
    });
});

// ─── Bulk actions ────────────────────────────────────────────────────
function getAllPlayerNames() {
    return Array.from(document.querySelectorAll('.player-tab[data-player]'))
        .map(t => t.dataset.player);
}

document.getElementById('btn-join-all').addEventListener('click', async () => {
    const players = getAllPlayerNames();
    for (const p of players) {
        await sendChatMessage(p, '!join');
        await new Promise(r => setTimeout(r, 100));
    }
});

const RANDOM_CMDS = ['!left', '!right', '!jump', '!attack', '!special', '!dash', '!block'];

document.getElementById('btn-random-actions').addEventListener('click', async () => {
    const players = getAllPlayerNames();
    for (const p of players) {
        const cmd = RANDOM_CMDS[Math.floor(Math.random() * RANDOM_CMDS.length)];
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
            // Each tick, 1-3 random players perform a random action
            const count = Math.min(players.length, 1 + Math.floor(Math.random() * 3));
            const shuffled = [...players].sort(() => Math.random() - 0.5);
            for (let i = 0; i < count; i++) {
                const cmd = RANDOM_CMDS[Math.floor(Math.random() * RANDOM_CMDS.length)];
                await sendChatMessage(shuffled[i], cmd);
            }
        }, 500);
    }
});

// ─── Initialize ──────────────────────────────────────────────────────────
fetchStatus();
setInterval(fetchStatus, POLL_INTERVAL);
