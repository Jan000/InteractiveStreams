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

// ─── Initialize ──────────────────────────────────────────────────────────
fetchStatus();
setInterval(fetchStatus, POLL_INTERVAL);
