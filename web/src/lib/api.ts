// API base URL – in production (static export served by C++ server) this is
// the same origin.  During development, Next.js rewrites proxy to :8080.
const BASE = typeof window !== "undefined" && window.location.port === "8080"
  ? ""
  : "";

export interface StreamState {
  id: string;
  name: string;
  title: string;
  description: string;
  resolution: "mobile" | "desktop";
  width: number;
  height: number;
  gameMode: "fixed" | "vote" | "random";
  streaming: boolean;
  channelIds: string[];
  // Editable config fields
  fixedGame: string;
  streamUrl: string;
  streamKey: string;
  fps: number;
  bitrate: number;
  preset: string;
  codec: string;
  enabled: boolean;
  game?: {
    id: string;
    name: string;
    state: Record<string, unknown>;
    commands: Record<string, unknown>;
    isRoundComplete: boolean;
    isGameOver: boolean;
  };
  pendingSwitch?: {
    game: string;
    mode: string;
  };
  vote?: {
    active: boolean;
    timer: number;
    duration: number;
    tallies: Record<string, number>;
  };
}

export interface ChannelState {
  id: string;
  platform: string;
  name: string;
  enabled: boolean;
  connected: boolean;
  settings?: Record<string, unknown>;
}

export interface GameInfo {
  id: string;
  name: string;
  description: string;
}

export interface ScoreEntry {
  displayName: string;
  gameName: string;
  points: number;
  wins: number;
  gamesPlayed: number;
  timestamp: number;
}

export interface PerfSample {
  time: number;
  fps: number;
  frameTimeMs: number;
  cpuPercent: number;
  memoryMB: number;
  activeStreams: number;
  activeChannels: number;
  totalPlayers: number;
}

export interface PerfData {
  avgFps: number;
  avgFrameTime: number;
  avgMemory: number;
  peakMemory: number;
  activeStreams: number;
  activeChannels: number;
  totalPlayers: number;
  samples: PerfSample[];
}

export interface StatusResponse {
  version: string;
  streams: StreamState[];
  channels: ChannelState[];
  games: GameInfo[];
}

// ── Generic fetch helpers ─────────────────────────────────────────────────

async function request<T>(path: string, init?: RequestInit): Promise<T> {
  const res = await fetch(`${BASE}${path}`, {
    ...init,
    headers: { "Content-Type": "application/json", ...init?.headers },
  });
  if (!res.ok) {
    const body = await res.json().catch(() => ({}));
    throw new Error(body.error ?? `HTTP ${res.status}`);
  }
  return res.json();
}

function get<T>(path: string) {
  return request<T>(path);
}

function post<T>(path: string, body?: unknown) {
  return request<T>(path, {
    method: "POST",
    body: body ? JSON.stringify(body) : undefined,
  });
}

function put<T>(path: string, body?: unknown) {
  return request<T>(path, {
    method: "PUT",
    body: body ? JSON.stringify(body) : undefined,
  });
}

function del<T>(path: string) {
  return request<T>(path, { method: "DELETE" });
}

// ── API functions ─────────────────────────────────────────────────────────

export const api = {
  // Status
  getStatus: () => get<StatusResponse>("/api/status"),
  shutdown: () => post<{ success: boolean }>("/api/shutdown"),

  // Channels
  getChannels: () => get<ChannelState[]>("/api/channels"),
  createChannel: (data: Partial<ChannelState>) =>
    post<{ success: boolean; id: string }>("/api/channels", data),
  updateChannel: (id: string, data: Partial<ChannelState>) =>
    put<{ success: boolean }>(`/api/channels/${id}`, data),
  deleteChannel: (id: string) =>
    del<{ success: boolean }>(`/api/channels/${id}`),
  connectChannel: (id: string) =>
    post<{ success: boolean }>(`/api/channels/${id}/connect`),
  disconnectChannel: (id: string) =>
    post<{ success: boolean }>(`/api/channels/${id}/disconnect`),

  // Streams
  getStreams: () => get<StreamState[]>("/api/streams"),
  createStream: (data: Record<string, unknown>) =>
    post<{ success: boolean; id: string }>("/api/streams", data),
  updateStream: (id: string, data: Record<string, unknown>) =>
    put<{ success: boolean }>(`/api/streams/${id}`, data),
  deleteStream: (id: string) =>
    del<{ success: boolean }>(`/api/streams/${id}`),
  startStreaming: (id: string) =>
    post<{ success: boolean }>(`/api/streams/${id}/start`),
  stopStreaming: (id: string) =>
    post<{ success: boolean }>(`/api/streams/${id}/stop`),
  switchGame: (id: string, game: string, mode: string) =>
    post<{ success: boolean }>(`/api/streams/${id}/game`, { game, mode }),
  cancelSwitch: (id: string) =>
    post<{ success: boolean }>(`/api/streams/${id}/cancel-switch`),

  // Games
  getGames: () => get<GameInfo[]>("/api/games"),

  // Settings
  getSettings: () => get<Record<string, unknown>>("/api/settings"),
  updateSettings: (data: Record<string, unknown>) =>
    put<{ success: boolean }>("/api/settings", data),
  saveConfig: () => post<{ success: boolean }>("/api/config/save"),

  // Chat
  sendChat: (username: string, text: string) =>
    post<{ success: boolean }>("/api/chat", { username, text }),
  getChatLog: () => get<string[]>("/api/chat/log"),
  sendToChannel: (channelId: string, text: string) =>
    post<{ success: boolean }>(`/api/channels/${channelId}/send`, { text }),
  broadcastChat: (text: string) =>
    post<{ success: boolean; sent_to: number }>("/api/chat/broadcast", { text }),

  // Scoreboard
  getScoreboardRecent: (limit = 10, hours = 24) =>
    get<{ leaderboard: ScoreEntry[] }>(`/api/scoreboard/recent?limit=${limit}&hours=${hours}`),
  getScoreboardAllTime: (limit = 5) =>
    get<{ leaderboard: ScoreEntry[] }>(`/api/scoreboard/alltime?limit=${limit}`),
  getPlayerStats: (userId: string) =>
    get<ScoreEntry>(`/api/scoreboard/player/${encodeURIComponent(userId)}`),

  // Performance
  getPerf: (seconds = 60) =>
    get<PerfData>(`/api/perf?seconds=${seconds}`),
  getPerfHistory: (seconds = 300) =>
    get<{ samples: PerfSample[] }>(`/api/perf/history?seconds=${seconds}`),

  // Frame preview URL (not JSON – returns JPEG binary)
  frameUrl: (streamId: string) => `${BASE}/api/streams/${streamId}/frame`,
};
