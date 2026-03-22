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
  profileId: string;
  resolution: "mobile" | "desktop" | "mobile720" | "desktop720";
  width: number;
  height: number;
  gameMode: "fixed" | "vote" | "random";
  streaming: boolean;
  channelIds: string[];
  // Editable config fields
  fixedGame: string;
  fps: number;
  bitrate: number;
  preset: string;
  codec: string;
  profile: string;
  tune: string;
  keyframeInterval: number;
  threads: number;
  cbr: boolean;
  maxrateFactor: number;
  bufsizeFactor: number;
  audioBitrate: number;
  audioSampleRate: number;
  audioCodec: string;
  enabled: boolean;
  // Dual-format streaming
  dualFormat?: boolean;
  secondaryResolution?: "mobile" | "desktop" | "mobile720" | "desktop720";
  secondaryWidth?: number;
  secondaryHeight?: number;
  secondaryBitrate?: number;
  secondaryFps?: number;
  secondaryStreaming?: boolean;
  // Per-game descriptions and info messages
  gameDescriptions?: Record<string, string>;
  gameInfoMessages?: Record<string, string>;
  gameInfoIntervals?: Record<string, number>;
  // Per-game player limits (0 = unlimited)
  gamePlayerLimits?: Record<string, number>;
  // Per-game platform display names (Twitch / YouTube)
  gameTwitchCategories?: Record<string, string>;
  gameTwitchTitles?: Record<string, string>;
  gameYoutubeTitles?: Record<string, string>;
  // Scoreboard overlay settings
  scoreboardTopN?: number;
  scoreboardAllTimeTitle?: string;
  scoreboardRecentTitle?: string;
  scoreboardRecentHours?: number;
  scoreboardCycleSecs?: number;
  scoreboardFadeSecs?: number;
  scoreboardChatInterval?: number;
  // Live scoreboard data
  scoreboard?: Array<{ name: string; points: number; wins: number }>;
  scoreboardRecent?: Array<{ name: string; points: number; wins: number }>;
  // Per-stream statistics
  stats?: ChannelStatsData;
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
  details?: Record<string, unknown>;
}

export interface GameInfo {
  id: string;
  name: string;
  description: string;
}

export interface TextElementData {
  id: string;
  label: string;
  x: number;
  y: number;
  font_size: number;
  align: "left" | "center" | "right";
  visible: boolean;
  color: string;
  content: string;
}

export interface ScoreEntry {
  userId?: string;
  displayName: string;
  gameName: string;
  points: number;
  wins: number;
  gamesPlayed: number;
  timestamp: number;
}

export interface ScoreboardPanelConfig {
  enabled: boolean;
  title: string;
  duration_secs: number;
  top_n: number;
  font_size: number;
  box_width_pct: number;
  pos_x_pct: number;
  pos_y_pct: number;
  align_x: string;    // "left" | "center" | "right"
  align_y: string;    // "top" | "center" | "bottom"
  opacity: number;
  bg_color: string;
  border_color: string;
  title_color: string;
  name_color: string;
  points_color: string;
  gold_color: string;
  silver_color: string;
  bronze_color: string;
  // Content & grouping
  content_type: string;   // "players" | "countries"
  time_range: string;     // "round" | "recent" | "alltime"
  game_filter: string;    // "" = all games, else game_id
  group: number;          // Same group cycles; different groups render simultaneously
  include_bots: boolean;
  show_flags: boolean;
  flag_shape: string;     // "circle" | "rect"
  flag_size: number;      // multiplier
  show_names: boolean;
  show_codes: boolean;
  value_label: string;    // e.g. "pts", "wins"
}

export interface ScoreboardConfig {
  panels: ScoreboardPanelConfig[];
  recent_hours: number;
  fade_secs: number;
  chat_interval: number;
  hidden_players: string[];
}

export interface PlayerEntry {
  userId: string;
  displayName: string;
  totalPoints: number;
  totalWins: number;
  gamesPlayed: number;
  firstSeen: string;
  lastSeen: string;
  hidden: boolean;
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
  // Extended statistics
  minFps: number;
  maxFps: number;
  medianFps: number;
  minFrameTime: number;
  maxFrameTime: number;
  medianFrameTime: number;
  minMemory: number;
  maxMemory: number;
}

export interface StatsUser {
  userId: string;
  displayName: string;
  sessions: number;
  messages: number;
  engagementSeconds: number;
}

export interface ChannelStatsData {
  uniqueViewers: number;
  totalInteractions: number;
  interactionRatio: number;
  avgEngagementSeconds: number;
  medianEngagementSeconds: number;
  totalSessions: number;
  uptimeSeconds: number;
  sessionTimeoutSeconds: number;
  users: StatsUser[];
}

export interface ChannelStatsEntry {
  channelId: string;
  channelName: string;
  platform: string;
  connected: boolean;
  stats: ChannelStatsData;
}

export interface StreamStatsEntry {
  streamId: string;
  streamName: string;
  stats: ChannelStatsData;
}

export interface StatusResponse {
  version: string;
  gitHash: string;
  streams: StreamState[];
  channels: ChannelState[];
  games: GameInfo[];
}

export interface AudioState {
  playing: boolean;
  muted: boolean;
  musicVolume: number;
  sfxVolume: number;
  currentTrack: string;
  trackCount: number;
  fadeInSeconds: number;
  fadeOutSeconds: number;
  crossfadeOverlap: number;
  musicDirectory: string;
  sfxDirectory: string;
}

export interface StreamProfile {
  id: string;
  name: string;
  parent_id: string;
  config: Record<string, unknown>;
}

// ── Generic fetch helpers ─────────────────────────────────────────────────

/** Read the saved API key (set in Settings page and persisted in localStorage). */
function getApiKey(): string {
  if (typeof window === "undefined") return "";
  return localStorage.getItem("is_api_key") ?? "";
}

/** Build auth headers if an API key or session token is configured. */
function authHeaders(): Record<string, string> {
  if (typeof window === "undefined") return {};
  // Prefer session token (from password login), fall back to API key
  const sessionToken = localStorage.getItem("is_session_token") ?? "";
  if (sessionToken) return { Authorization: `Bearer ${sessionToken}` };
  const key = getApiKey();
  if (!key) return {};
  return { Authorization: `Bearer ${key}` };
}

async function request<T>(path: string, init?: RequestInit): Promise<T> {
  const res = await fetch(`${BASE}${path}`, {
    ...init,
    headers: { "Content-Type": "application/json", ...authHeaders(), ...init?.headers },
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
  triggerMetadataUpdate: (id: string) =>
    post<{ success: boolean }>(`/api/streams/${id}/update-metadata`),

  // Games
  getGames: () => get<GameInfo[]>("/api/games"),
  getGameSettings: () => get<Record<string, Record<string, unknown>>>("/api/games/settings"),
  updateGameSettings: (data: Record<string, Record<string, unknown>>) =>
    put<{ success: boolean }>("/api/games/settings", data),
  getTextElements: () => get<Record<string, TextElementData[]>>("/api/games/text-elements"),

  // Settings
  getSettings: () => get<Record<string, unknown>>("/api/settings"),
  updateSettings: (data: Record<string, unknown>) =>
    put<{ success: boolean }>("/api/settings", data),
  saveConfig: () => post<{ success: boolean }>("/api/config/save"),
  exportConfig: () => get<Record<string, unknown>>("/api/config/export"),
  importConfig: (data: Record<string, unknown>) =>
    post<{ success: boolean }>("/api/config/import", data),

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
  getScoreboardConfig: () =>
    get<ScoreboardConfig>("/api/scoreboard/config"),
  updateScoreboardConfig: (data: ScoreboardConfig) =>
    put<{ success: boolean }>("/api/scoreboard/config", data),
  getScoreboardPlayers: () =>
    get<{ players: PlayerEntry[] }>("/api/scoreboard/players"),
  updatePlayer: (userId: string, data: { total_points: number }) =>
    put<{ success: boolean }>(`/api/scoreboard/players/${encodeURIComponent(userId)}`, data),
  deletePlayer: (userId: string) =>
    del<{ success: boolean }>(`/api/scoreboard/players/${encodeURIComponent(userId)}`),

  // Twitch OAuth
  getTwitchAuthUrl: (channelId: string) =>
    get<{ url: string; channelId: string }>(`/api/auth/twitch/url?channel_id=${channelId}`),
  storeTwitchToken: (channelId: string, accessToken: string) =>
    post<{ success: boolean }>("/api/auth/twitch/token", { channelId, accessToken }),

  // YouTube OAuth
  getYouTubeAuthUrl: (channelId: string) =>
    get<{ url: string; channelId: string }>(`/api/auth/youtube/url?channel_id=${channelId}`),
  exchangeYouTubeCode: (channelId: string, code: string) =>
    post<{ success: boolean; has_refresh_token?: boolean; expires_in?: number }>(
      "/api/auth/youtube/token", { channelId, code }),
  refreshYouTubeToken: (channelId: string) =>
    post<{ success: boolean; expires_in?: number }>(
      `/api/auth/youtube/refresh/${encodeURIComponent(channelId)}`),

  // YouTube Quota & Detection
  detectYouTubeBroadcast: (channelId: string) =>
    post<{ success: boolean; message: string }>(
      `/api/youtube/detect/${encodeURIComponent(channelId)}`),
  getYouTubeQuota: () =>
    get<{ used: number; budget: number; remaining: number }>("/api/youtube/quota"),
  updateYouTubeQuota: (data: { budget?: number; reset?: boolean }) =>
    put<{ used: number; budget: number; remaining: number }>("/api/youtube/quota", data),

  // Performance
  getPerf: (seconds = 60) =>
    get<PerfData>(`/api/perf?seconds=${seconds}`),
  getPerfHistory: (seconds = 300) =>
    get<{ samples: PerfSample[] }>(`/api/perf/history?seconds=${seconds}`),

  // Audio
  getAudio: () =>
    get<AudioState>("/api/audio"),
  updateAudio: (data: Partial<AudioState>) =>
    put<{ success: boolean }>("/api/audio", data),
  nextTrack: () =>
    post<{ success: boolean }>("/api/audio/next"),
  pauseMusic: () =>
    post<{ success: boolean }>("/api/audio/pause"),
  resumeMusic: () =>
    post<{ success: boolean }>("/api/audio/resume"),
  rescanAudio: () =>
    post<{ success: boolean; trackCount: number }>("/api/audio/rescan"),

  // Profiles (config inheritance)
  getProfiles: () =>
    get<StreamProfile[]>("/api/profiles"),
  createProfile: (data: Partial<StreamProfile>) =>
    post<{ success: boolean; id: string }>("/api/profiles", data),
  updateProfile: (id: string, data: Partial<StreamProfile>) =>
    put<{ success: boolean }>(`/api/profiles/${id}`, data),
  deleteProfile: (id: string) =>
    del<{ success: boolean }>(`/api/profiles/${id}`),
  getResolvedProfile: (id: string) =>
    get<Record<string, unknown>>(`/api/profiles/${id}/resolved`),

  // Statistics
  getChannelStats: () => get<ChannelStatsEntry[]>("/api/stats/channels"),
  getStreamStats: () => get<StreamStatsEntry[]>("/api/stats/streams"),
  resetChannelStats: (id: string) =>
    post<{ success: boolean }>(`/api/stats/channels/${id}/reset`),
  resetStreamStats: (id: string) =>
    post<{ success: boolean }>(`/api/stats/streams/${id}/reset`),
  resetAllStats: () => post<{ success: boolean }>("/api/stats/reset"),

  // Auth
  getAuthStatus: () => get<{ passwordSet: boolean; authenticated: boolean }>("/api/auth/status"),
  authSetup: (password: string) =>
    post<{ success: boolean; token: string }>("/api/auth/setup", { password }),
  authLogin: (password: string) =>
    post<{ success: boolean; token: string }>("/api/auth/login", { password }),
  authLogout: () =>
    post<{ success: boolean }>("/api/auth/logout"),
  authChangePassword: (oldPassword: string, newPassword: string) =>
    post<{ success: boolean }>("/api/auth/change-password", { oldPassword, newPassword }),

  // Frame preview URL (not JSON – returns JPEG binary)
  frameUrl: (streamId: string) => `${BASE}/api/streams/${streamId}/frame`,
};
