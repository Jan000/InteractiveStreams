// ═══════════════════════════════════════════════════════════════════════════
// InteractiveStreams – Admin Dashboard JavaScript  (v0.2.0 – multi-stream)
// ═══════════════════════════════════════════════════════════════════════════

const API = '';
const POLL = 1000;

// ─── Phase / command metadata per game ───────────────────────────────────
const GAME_PHASES = {
    chaos_arena:    { names: {0:'Lobby',1:'Countdown',2:'Battle',3:'Round End',4:'Game Over'}, colors:{0:'#58a6ff',1:'#d29922',2:'#3fb950',3:'#bc8cff',4:'#f85149'} },
    color_conquest: { names: {0:'Lobby',1:'Voting',2:'Results',3:'Game Over'},                 colors:{0:'#58a6ff',1:'#3fb950',2:'#bc8cff',3:'#f85149'} },
};
const GAME_QUICK = {
    chaos_arena:    ['!join','!left','!right','!jump','!attack','!special','!dash','!block'],
    color_conquest: ['!join','!join red','!join blue','!join green','!join yellow','!up','!down','!left','!right'],
};
const GAME_RAND = {
    chaos_arena:    ['!left','!right','!jump','!attack','!special','!dash','!block'],
    color_conquest: ['!up','!down','!left','!right'],
};
const PLAYER_COLORS = { Player1:'#58a6ff', Player2:'#3fb950', Player3:'#d29922', Player4:'#bc8cff' };

let availableGames = [];
let autoPlayTimer = null;

// ═══════════════════════════════════════════════════════════════════════════
//  Tab Navigation
// ═══════════════════════════════════════════════════════════════════════════
document.querySelectorAll('.tab').forEach(t => {
    t.addEventListener('click', () => {
        document.querySelectorAll('.tab').forEach(b => b.classList.remove('active'));
        document.querySelectorAll('.tab-content').forEach(s => { s.classList.remove('active'); s.style.display = 'none'; });
        t.classList.add('active');
        const sec = document.getElementById(t.dataset.tab);
        if (sec) { sec.style.display = ''; sec.classList.add('active'); }
    });
});

// ═══════════════════════════════════════════════════════════════════════════
//  API helpers
// ═══════════════════════════════════════════════════════════════════════════
const json = o => JSON.stringify(o);
const hdrs = { 'Content-Type': 'application/json' };
async function api(path, opts) { return fetch(API + path, opts); }
async function apiPost(path, body) { return fetch(API + path, { method:'POST', headers:hdrs, body:json(body) }); }
async function apiPut(path, body)  { return fetch(API + path, { method:'PUT',  headers:hdrs, body:json(body) }); }
async function apiDel(path)        { return fetch(API + path, { method:'DELETE' }); }

function escapeHtml(s) { const d=document.createElement('div'); d.textContent=s; return d.innerHTML; }

// ═══════════════════════════════════════════════════════════════════════════
//  Status Polling
// ═══════════════════════════════════════════════════════════════════════════
async function poll() {
    try {
        const r = await api('/api/status');
        if (!r.ok) throw new Error(r.status);
        const d = await r.json();
        setOnline(true);
        availableGames = d.games || [];
        renderStreams(d.streams || []);
        renderChannels(d.channels || []);
        document.getElementById('last-update').textContent = new Date().toLocaleTimeString();
    } catch { setOnline(false); }
}

function setOnline(ok) {
    document.getElementById('status-indicator').className = 'status-dot ' + (ok?'online':'offline');
    document.getElementById('status-text').textContent = ok?'Online':'Offline';
}

// ═══════════════════════════════════════════════════════════════════════════
//  Streams Tab
// ═══════════════════════════════════════════════════════════════════════════
function renderStreams(streams) {
    const c = document.getElementById('streams-container');
    // Preserve open edit forms by ID
    const existing = {};
    c.querySelectorAll('.stream-card').forEach(el => { existing[el.dataset.sid] = el; });

    const ids = new Set();
    for (const s of streams) {
        ids.add(s.id);
        let card = existing[s.id];
        if (!card) { card = createStreamCard(s); c.appendChild(card); }
        updateStreamCard(card, s);
    }
    // Remove stale cards
    c.querySelectorAll('.stream-card').forEach(el => { if (!ids.has(el.dataset.sid)) el.remove(); });
}

function createStreamCard(s) {
    const card = document.createElement('div');
    card.className = 'card stream-card';
    card.dataset.sid = s.id;
    card.innerHTML = `
        <div class="card-header">
            <h2 class="s-name"></h2>
            <span class="badge s-streaming-badge"></span>
        </div>
        <div class="card-body">
            <div class="stream-meta">
                <span class="badge s-res-badge"></span>
                <span class="badge s-mode-badge"></span>
            </div>
            <div class="stream-game-info">
                <span class="game-name s-game-name">—</span>
                <span class="game-phase s-game-phase"></span>
            </div>
            <div class="s-vote-info" style="display:none"></div>
            <div class="s-pending" style="display:none"></div>
            <div class="stream-controls">
                <select class="select-sm s-game-sel" title="Switch game"></select>
                <select class="select-sm s-switch-mode" title="Switch mode">
                    <option value="immediate">⚡ Sofort</option>
                    <option value="after_round">⏳ Nach Runde</option>
                    <option value="after_game">🏁 Nach Spiel</option>
                </select>
                <button class="btn btn-sm btn-accent s-btn-switch">Wechseln</button>
                <button class="btn btn-sm s-btn-start">▶ Start</button>
                <button class="btn btn-sm btn-danger s-btn-stop">⏹ Stop</button>
                <button class="btn btn-sm btn-danger s-btn-delete">🗑</button>
            </div>
        </div>`;
    // Wire events
    card.querySelector('.s-btn-switch').addEventListener('click', () => {
        const game = card.querySelector('.s-game-sel').value;
        const mode = card.querySelector('.s-switch-mode').value;
        if (game) apiPost(`/api/streams/${s.id}/game`, { game, mode });
    });
    card.querySelector('.s-btn-start').addEventListener('click', () => apiPost(`/api/streams/${s.id}/start`, {}));
    card.querySelector('.s-btn-stop').addEventListener('click',  () => apiPost(`/api/streams/${s.id}/stop`, {}));
    card.querySelector('.s-btn-delete').addEventListener('click', () => {
        if (confirm(`Delete stream "${s.name}"?`)) apiDel(`/api/streams/${s.id}`);
    });
    return card;
}

function updateStreamCard(card, s) {
    card.querySelector('.s-name').textContent = s.name || s.id;

    const stBadge = card.querySelector('.s-streaming-badge');
    stBadge.textContent = s.streaming ? '🔴 LIVE' : 'Idle';
    stBadge.className = 'badge ' + (s.streaming ? 'badge-red' : '');

    card.querySelector('.s-res-badge').textContent = s.resolution === 'desktop' ? '🖥 Desktop' : '📱 Mobile';
    card.querySelector('.s-mode-badge').textContent = { fixed:'🔒 Fixed', vote:'🗳 Vote', random:'🎲 Random' }[s.gameMode] || s.gameMode;

    // Game info
    if (s.game) {
        card.querySelector('.s-game-name').textContent = s.game.name || s.game.id;
        const gp = GAME_PHASES[s.game.id];
        const phase = s.game.state?.phase;
        if (gp && phase !== undefined) {
            const pn = gp.names[phase] || `Phase ${phase}`;
            const pc = gp.colors[phase] || 'var(--text-muted)';
            card.querySelector('.s-game-phase').innerHTML = ` — <span style="color:${pc}">${pn}</span>`;
        } else {
            card.querySelector('.s-game-phase').textContent = '';
        }
    }

    // Vote info
    const vi = card.querySelector('.s-vote-info');
    if (s.vote && s.vote.active) {
        const rem = Math.max(0, Math.ceil(s.vote.duration - s.vote.timer));
        let html = `<div style="color:var(--orange);font-weight:600">🗳 Vote in progress (${rem}s left)</div>`;
        if (s.vote.tallies) {
            for (const [g,c] of Object.entries(s.vote.tallies)) html += `<div style="font-size:12px">${g}: ${c} votes</div>`;
        }
        vi.innerHTML = html; vi.style.display = '';
    } else { vi.style.display = 'none'; }

    // Pending switch
    const pe = card.querySelector('.s-pending');
    if (s.pendingSwitch) {
        pe.innerHTML = `<div class="pending-switch-banner" style="margin:0">⏳ Wechsel zu <strong>${escapeHtml(s.pendingSwitch.game)}</strong> (${s.pendingSwitch.mode}) <button class="btn btn-sm btn-danger s-btn-cancel">Abbrechen</button></div>`;
        pe.querySelector('.s-btn-cancel')?.addEventListener('click', () => apiPost(`/api/streams/${s.id}/cancel-switch`, {}));
        pe.style.display = '';
    } else { pe.style.display = 'none'; }

    // Game selector
    const sel = card.querySelector('.s-game-sel');
    const cv = sel.value;
    if (sel.children.length !== availableGames.length) {
        sel.innerHTML = '';
        for (const g of availableGames) {
            const o = document.createElement('option'); o.value = g.id; o.textContent = g.name; sel.appendChild(o);
        }
    }
    if (cv) sel.value = cv;
}

// ── Add stream ───────────────────────────────────────────────────────────
document.getElementById('btn-add-stream').addEventListener('click', () => {
    const name = prompt('Stream name:', 'New Stream');
    if (!name) return;
    apiPost('/api/streams', { name, resolution:'mobile', game_mode:'fixed', fixed_game:'chaos_arena', channel_ids:['local'], enabled:true });
});

// ═══════════════════════════════════════════════════════════════════════════
//  Channels Tab
// ═══════════════════════════════════════════════════════════════════════════
function renderChannels(channels) {
    const c = document.getElementById('channels-container');
    const existing = {};
    c.querySelectorAll('.channel-card').forEach(el => { existing[el.dataset.cid] = el; });
    const ids = new Set();
    for (const ch of channels) {
        ids.add(ch.id);
        let card = existing[ch.id];
        if (!card) { card = createChannelCard(ch); c.appendChild(card); }
        updateChannelCard(card, ch);
    }
    c.querySelectorAll('.channel-card').forEach(el => { if (!ids.has(el.dataset.cid)) el.remove(); });
}

function createChannelCard(ch) {
    const card = document.createElement('div');
    card.className = 'card channel-card';
    card.dataset.cid = ch.id;
    card.innerHTML = `
        <div class="card-header">
            <h2 class="ch-name"></h2>
            <span class="badge ch-conn-badge"></span>
        </div>
        <div class="card-body">
            <div class="channel-platform ch-platform"></div>
            <div class="channel-status ch-status"></div>
            <div class="channel-actions">
                <button class="btn btn-sm ch-btn-connect">Connect</button>
                <button class="btn btn-sm ch-btn-disconnect">Disconnect</button>
                <button class="btn btn-sm btn-danger ch-btn-delete">🗑</button>
            </div>
        </div>`;
    card.querySelector('.ch-btn-connect').addEventListener('click',    () => apiPost(`/api/channels/${ch.id}/connect`, {}));
    card.querySelector('.ch-btn-disconnect').addEventListener('click', () => apiPost(`/api/channels/${ch.id}/disconnect`, {}));
    card.querySelector('.ch-btn-delete').addEventListener('click',     () => {
        if (ch.id === 'local') { alert('Cannot delete local channel.'); return; }
        if (confirm(`Delete channel "${ch.name}"?`)) apiDel(`/api/channels/${ch.id}`);
    });
    return card;
}

function updateChannelCard(card, ch) {
    card.querySelector('.ch-name').textContent = ch.name || ch.id;
    const badge = card.querySelector('.ch-conn-badge');
    badge.textContent = ch.connected ? 'Connected' : 'Disconnected';
    badge.className = 'badge ' + (ch.connected ? 'badge-green' : 'badge-red');
    card.querySelector('.ch-platform').textContent = ch.platform?.toUpperCase() || '';
    card.querySelector('.ch-status').innerHTML = ch.enabled ? '<span style="color:var(--green)">Enabled</span>' : '<span style="color:var(--text-muted)">Disabled</span>';
}

// ── Add channel form toggle ──────────────────────────────────────────────
const addChForm = document.getElementById('add-channel-form');
document.getElementById('btn-add-channel').addEventListener('click', () => { addChForm.style.display = addChForm.style.display === 'none' ? '' : 'none'; });
document.getElementById('btn-cancel-add-channel').addEventListener('click', () => { addChForm.style.display = 'none'; });

// Show/hide platform-specific settings
document.getElementById('new-ch-platform').addEventListener('change', e => {
    document.getElementById('new-ch-twitch-settings').style.display = e.target.value === 'twitch' ? '' : 'none';
    document.getElementById('new-ch-youtube-settings').style.display = e.target.value === 'youtube' ? '' : 'none';
});

document.getElementById('btn-create-channel').addEventListener('click', async () => {
    const platform = document.getElementById('new-ch-platform').value;
    const name = document.getElementById('new-ch-name').value || platform;
    const enabled = document.getElementById('new-ch-enabled').checked;
    let settings = {};
    if (platform === 'twitch') {
        settings = {
            channel: document.getElementById('new-ch-twitch-channel').value,
            oauth_token: document.getElementById('new-ch-twitch-oauth').value,
            bot_username: document.getElementById('new-ch-twitch-bot').value || 'InteractiveStreamsBot',
            server: 'irc.chat.twitch.tv', port: 6667
        };
    } else if (platform === 'youtube') {
        settings = {
            api_key: document.getElementById('new-ch-youtube-apikey').value,
            live_chat_id: document.getElementById('new-ch-youtube-chatid').value
        };
    }
    await apiPost('/api/channels', { platform, name, enabled, settings });
    addChForm.style.display = 'none';
});

// ═══════════════════════════════════════════════════════════════════════════
//  Settings Tab  (simple)
// ═══════════════════════════════════════════════════════════════════════════
// Loaded once from /api/settings and applied to inputs; user presses global Save.

// ═══════════════════════════════════════════════════════════════════════════
//  Save config / Shutdown
// ═══════════════════════════════════════════════════════════════════════════
document.getElementById('btn-save-config').addEventListener('click', async () => {
    await apiPost('/api/config/save', {});
    alert('Configuration saved to disk.');
});

document.getElementById('btn-shutdown').addEventListener('click', async () => {
    if (!confirm('Shutdown the server?')) return;
    await apiPost('/api/shutdown', {});
});

// ═══════════════════════════════════════════════════════════════════════════
//  Chat Test Tab  (mostly preserved from v0.1)
// ═══════════════════════════════════════════════════════════════════════════
const chatEl = {
    messages: document.getElementById('chat-messages'),
    input:    document.getElementById('chat-input'),
    username: document.getElementById('chat-username'),
    quickBox: document.getElementById('chat-quick-buttons'),
};

async function sendChat(user, text) {
    if (!text.trim()) return;
    try {
        const r = await apiPost('/api/chat', { username: user, text });
        if (r.ok) {
            appendChat(user, text);
            chatEl.input.value = '';
            chatEl.input.focus();
        }
    } catch {}
}

function appendChat(sender, text) {
    const col = PLAYER_COLORS[sender] || 'var(--accent)';
    const d = document.createElement('div');
    d.className = 'chat-msg';
    d.innerHTML = `<span class="chat-sender" style="color:${col}">${escapeHtml(sender)}:</span> <span class="chat-text">${escapeHtml(text)}</span>`;
    chatEl.messages.appendChild(d);
    chatEl.messages.scrollTop = chatEl.messages.scrollHeight;
    while (chatEl.messages.children.length > 201) chatEl.messages.removeChild(chatEl.messages.children[1]);
}

// Quick buttons – refresh whenever first stream's game changes
let lastQuickGame = '';
function refreshQuickButtons(streams) {
    const gid = streams[0]?.game?.id || '';
    if (gid === lastQuickGame) return;
    lastQuickGame = gid;
    const cmds = GAME_QUICK[gid] || ['!join'];
    chatEl.quickBox.innerHTML = '';
    for (const cmd of cmds) {
        const b = document.createElement('button');
        b.className = 'btn btn-sm chat-quick'; b.textContent = cmd;
        b.addEventListener('click', () => sendChat(chatEl.username.value, cmd));
        chatEl.quickBox.appendChild(b);
    }
}

document.getElementById('btn-send-chat').addEventListener('click', () => sendChat(chatEl.username.value, chatEl.input.value));
chatEl.input.addEventListener('keydown', e => { if (e.key === 'Enter') sendChat(chatEl.username.value, chatEl.input.value); });

// Player tabs
document.getElementById('player-tabs').addEventListener('click', e => {
    const tab = e.target.closest('.player-tab');
    if (!tab || tab.id === 'btn-add-player') return;
    document.querySelectorAll('.player-tab').forEach(t => t.classList.remove('active'));
    tab.classList.add('active');
    chatEl.username.value = tab.dataset.player;
});

document.getElementById('btn-add-player').addEventListener('click', () => {
    const name = prompt('Player name:'); if (!name) return;
    const colors = ['#ff6b6b','#48dbfb','#feca57','#ff9ff3','#54a0ff','#00d2d3'];
    const col = colors[document.querySelectorAll('.player-tab').length % colors.length];
    PLAYER_COLORS[name] = col;
    const btn = document.createElement('button');
    btn.className = 'player-tab'; btn.dataset.player = name;
    btn.style.setProperty('--tab-color', col);
    btn.innerHTML = `<span class="tab-dot" style="background:${col}"></span>${escapeHtml(name)}`;
    document.getElementById('btn-add-player').before(btn);
});

// Bulk actions
document.getElementById('btn-join-all').addEventListener('click', async () => {
    for (const t of document.querySelectorAll('.player-tab:not(.add-player-tab)')) {
        await sendChat(t.dataset.player, '!join');
    }
});

document.getElementById('btn-random-actions').addEventListener('click', async () => {
    const cmds = GAME_RAND[lastQuickGame] || ['!join'];
    for (const t of document.querySelectorAll('.player-tab:not(.add-player-tab)')) {
        const cmd = cmds[Math.floor(Math.random()*cmds.length)];
        await sendChat(t.dataset.player, cmd);
    }
});

document.getElementById('btn-auto-play').addEventListener('click', () => {
    const label = document.getElementById('auto-play-status');
    if (autoPlayTimer) {
        clearInterval(autoPlayTimer); autoPlayTimer = null; label.textContent = 'OFF'; return;
    }
    label.textContent = 'ON';
    autoPlayTimer = setInterval(async () => {
        const cmds = GAME_RAND[lastQuickGame] || ['!join'];
        const tabs = [...document.querySelectorAll('.player-tab:not(.add-player-tab)')];
        const t = tabs[Math.floor(Math.random()*tabs.length)];
        if (t) await sendChat(t.dataset.player, cmds[Math.floor(Math.random()*cmds.length)]);
    }, 500);
});

// ═══════════════════════════════════════════════════════════════════════════
//  Extended poll – also refresh chat quick buttons
// ═══════════════════════════════════════════════════════════════════════════
async function pollFull() {
    try {
        const r = await api('/api/status');
        if (!r.ok) throw 0;
        const d = await r.json();
        setOnline(true);
        availableGames = d.games || [];
        renderStreams(d.streams || []);
        renderChannels(d.channels || []);
        refreshQuickButtons(d.streams || []);
        document.getElementById('last-update').textContent = new Date().toLocaleTimeString();
    } catch { setOnline(false); }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Init
// ═══════════════════════════════════════════════════════════════════════════
pollFull();
setInterval(pollFull, POLL);
