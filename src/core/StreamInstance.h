#pragma once

#include "core/GameManager.h"
#include "core/PlayerDatabase.h"
#include "core/ScoreboardConfig.h"
#include "core/ChannelStats.h"
#include "platform/ChatMessage.h"

#include <SFML/Graphics.hpp>
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <thread>
#include <chrono>
#include <unordered_map>
#include <utility>
#include <nlohmann/json.hpp>

namespace is::streaming { class StreamEncoder; }

namespace is::core {

/// Game selection mode for a stream.
enum class GameModeType {
    Fixed,   ///< Always play the same game (restarts on game-over)
    Vote,    ///< Viewers vote for the next game after each session
    Random   ///< Random next game after each session
};

/// Resolution preset for a stream.
enum class ResolutionPreset {
    Mobile,      ///< 1080x1920 (9:16)
    Desktop,     ///< 1920x1080 (16:9)
    Mobile720,   ///< 720x1280  (9:16)
    Desktop720   ///< 1280x720  (16:9)
};

/// Serialisable configuration for a single stream instance.
struct StreamConfig {
    std::string id;
    std::string name = "Stream";
    std::string title;                     ///< Stream title (shown on Twitch/YouTube)
    std::string description;               ///< Stream description / about
    std::string profileId;                 ///< Optional parent profile (inheritance)
    ResolutionPreset resolution = ResolutionPreset::Mobile;
    GameModeType gameMode = GameModeType::Fixed;
    std::string fixedGame = "chaos_arena";
    std::vector<std::string> channelIds;   ///< subscribed input/output channels
    bool enabled = true;
    int fps      = 30;
    int bitrate  = 6000;
    std::string preset = "ultrafast";
    std::string codec  = "libx264";
    std::string profile = "baseline";
    std::string tune    = "zerolatency";
    int keyframeInterval = 2;    // GOP size in seconds
    int threads  = 2;            // 0 = auto, low values reduce contention
    bool cbr     = true;         // strict CBR encoding
    float maxrateFactor = 1.2f;  // VBR only
    float bufsizeFactor = 1.0f;  // VBR only

    // Audio encoding
    int audioBitrate    = 128;   // kbps
    int audioSampleRate = 44100;
    std::string audioCodec = "aac";

    int width()  const {
        switch (resolution) {
            case ResolutionPreset::Mobile:     return 1080;
            case ResolutionPreset::Desktop:    return 1920;
            case ResolutionPreset::Mobile720:  return 720;
            case ResolutionPreset::Desktop720: return 1280;
            default: return 1080;
        }
    }
    int height() const {
        switch (resolution) {
            case ResolutionPreset::Mobile:     return 1920;
            case ResolutionPreset::Desktop:    return 1080;
            case ResolutionPreset::Mobile720:  return 1280;
            case ResolutionPreset::Desktop720: return 720;
            default: return 1920;
        }
    }

    // ── Per-game descriptions (optional, overrides stream description) ───
    /// Map: game_id -> description for that game
    std::unordered_map<std::string, std::string> gameDescriptions;

    // ── Periodic info messages (optional, per game) ─────────────────────
    /// Map: game_id -> info message text
    std::unordered_map<std::string, std::string> gameInfoMessages;
    /// Map: game_id -> interval in seconds (0 = disabled)
    std::unordered_map<std::string, int> gameInfoIntervals;

    // ── Per-game font scale (multiplier, default 1.0) ───────────────────
    /// Map: game_id -> font scale factor
    std::unordered_map<std::string, float> gameFontScales;

    // ── Per-game player limits (0 = unlimited) ──────────────────────────
    /// Map: game_id -> max player count
    std::unordered_map<std::string, int> gamePlayerLimits;

    // ── Per-game platform names (Twitch / YouTube) ──────────────────────
    /// Map: game_id -> Twitch category name (e.g. "Just Chatting")
    std::unordered_map<std::string, std::string> gameTwitchCategories;
    /// Map: game_id -> Twitch stream title
    std::unordered_map<std::string, std::string> gameTwitchTitles;
    /// Map: game_id -> YouTube stream title
    std::unordered_map<std::string, std::string> gameYoutubeTitles;

    // ── Scoreboard overlay settings ─────────────────────────────────────
    int    scoreboardTopN        = 5;        ///< Entries per scoreboard panel
    int    scoreboardFontSize    = 20;       ///< Base font size for entries
    std::string scoreboardAllTimeTitle  = "ALL TIME";
    std::string scoreboardRecentTitle   = "LAST 24H";
    std::string scoreboardRoundTitle    = "CURRENT ROUND";
    int    scoreboardRecentHours = 24;       ///< Time window for recent scoreboard
    double scoreboardCycleSecs   = 10.0;     ///< Default seconds per panel (fallback)
    double scoreboardAllTimeSecs = 10.0;     ///< Seconds for all-time panel (0=skip)
    double scoreboardRecentSecs  = 10.0;     ///< Seconds for recent panel (0=skip)
    double scoreboardRoundSecs   = 8.0;      ///< Seconds for round panel (0=skip)
    double scoreboardFadeSecs    = 1.0;      ///< Crossfade duration in seconds
    int    scoreboardChatInterval = 120;     ///< Seconds between chat posts (0 = disabled)
    // ── Vote overlay font scale ─────────────────────────────────────────
    float  voteOverlayFontScale  = 1.0f;

    // ── Per-stream enabled games (for vote/random mode) ─────────────────
    /// List of game IDs that can be selected. Empty = all games enabled.
    std::vector<std::string> enabledGames;
};

/// A single stream instance – encapsulates game, off-screen render target,
/// and an optional FFmpeg encoder.  All heavy rendering / OpenGL work is
/// performed lazily on the first render() call (which is always from the
/// main thread) so that StreamInstances can be created from any thread.
class StreamInstance {
public:
    explicit StreamInstance(const StreamConfig& config);
    ~StreamInstance();

    // ── Main-loop methods (called from the main thread) ──────────────────

    void handleChatMessage(const platform::ChatMessage& msg);
    void update(double dt);
    void render(double alpha);
    void encodeFrame();

    // ── Game-mode logic ──────────────────────────────────────────────────

    void updateGameMode(double dt);
    void handleVoteCommand(const std::string& userId, const std::string& gameName);

    // ── Configuration ────────────────────────────────────────────────────

    const StreamConfig& config() const { return m_config; }
    void updateConfig(const StreamConfig& newConfig);

    // ── State access ─────────────────────────────────────────────────────

    GameManager&       gameManager()       { return *m_gameManager; }
    const GameManager& gameManager() const { return *m_gameManager; }
    sf::RenderTexture& renderTexture();
    const sf::Uint8*   getFrameBuffer() const;
    int  width()  const { return m_config.width(); }
    int  height() const { return m_config.height(); }

    /// Get the latest frame encoded as JPEG (thread-safe, for web preview).
    std::vector<uint8_t> getJpegFrame() const;

    /// Per-stream statistics (viewers, interactions, engagement).
    const ChannelStats& stats() const { return m_stats; }
    void resetStats() { m_stats.reset(); }

    // ── Streaming control ────────────────────────────────────────────────

    bool isStreaming() const;
    /// Start streaming. Returns true on success, false if config is invalid.
    /// Start streaming to all assigned channels that have an RTMP URL.
    /// Returns an empty string on success, or an error message on failure.
    std::string startStreaming();
    void stopStreaming();

    /// Immediately push current game title/category to Twitch/YouTube.
    /// Call this after a channel connects or its OAuth token changes.
    void triggerPlatformInfoUpdate();

    // ── Serialisation ────────────────────────────────────────────────────

    nlohmann::json getState() const;
    nlohmann::json toJson() const;
    static StreamConfig configFromJson(const nlohmann::json& j);

private:
    void ensureRenderTexture();
    void renderVoteOverlay();
    void renderGlobalScoreboard();
    void startNextGameFromVote();
    void startRandomGame();
    void restartCurrentGame();
    void updateJpegBuffer();
    void updateScoreboardCache();
    void sendScoreboardToChat();
    void rebuildScoreboardPanels();
    double currentPanelDuration() const;
    void sendPeriodicInfoMessage();
    void updatePlatformInfo(const std::string& gameId);
    std::vector<std::string> getAvailableGameIds() const;

    StreamConfig                               m_config;
    std::unique_ptr<GameManager>               m_gameManager;

    // Rendering (lazily initialised)
    sf::RenderTexture  m_renderTexture;
    sf::Image          m_frameCapture;
    sf::Font           m_font;
    bool               m_fontLoaded = false;
    bool               m_rtReady    = false;

    // Frame-pacing: only perform GPU readback + encode at the configured encoder FPS
    int  m_encodeFrameCounter   = 0;
    bool m_frameReadyForEncoder = false;

    // Encoding – one encoder per output channel (channelId → encoder)
    std::unordered_map<std::string, std::unique_ptr<streaming::StreamEncoder>> m_encoders;

    // JPEG frame buffer for web preview (thread-safe)
    mutable std::mutex     m_jpegMutex;
    std::vector<uint8_t>   m_jpegBuffer;
    int                    m_jpegFrameCounter  = 0;
    int                    m_jpegFrameInterval = 30; // encode JPEG every Nth frame (~2fps at 60fps)

    // Vote state
    struct VoteState {
        bool   active   = false;
        double timer    = 0.0;
        double duration = 20.0;
        std::unordered_map<std::string, std::string> userVotes; // userId → gameId
        std::unordered_map<std::string, int>         tallies;   // gameId → count
    };
    VoteState m_voteState;

    // Cached game display names for vote overlay (avoid creating game objects per frame)
    std::unordered_map<std::string, std::string> m_voteDisplayNames;

    // Game-mode transition state
    bool   m_waitingForTransition = false;
    double m_transitionTimer      = 0.0;
    double m_transitionDelay      = 3.0; // seconds before next game

    // Global scoreboard overlay (cached from PlayerDatabase)
    std::vector<ScoreEntry> m_scoreboardCache;
    std::vector<ScoreEntry> m_scoreboardRecentCache;
    std::vector<std::pair<std::string, int>> m_scoreboardRoundCache;
    double m_scoreboardRefreshTimer = 0.0;
    static constexpr double SCOREBOARD_REFRESH_INTERVAL = 5.0; // seconds

    // Cached global scoreboard config (copied from Application every refresh)
    GlobalScoreboardConfig m_sbConfig;

    // Scoreboard panel cycling (animated fade)
    // Panel indices: built dynamically from enabled panels (duration > 0)
    double m_scoreboardCycleTimer = 0.0;
    int    m_scoreboardPanelIndex = 0;       ///< current panel (0,1,2 mapped to enabled panels)
    enum class ScoreboardPanel { AllTime, Recent, Round };
    std::vector<ScoreboardPanel> m_scoreboardPanels; ///< ordered enabled panels

    // Periodic scoreboard chat posting
    double m_scoreboardChatTimer  = 0.0;

    // Periodic info message state
    double m_infoMessageTimer = 0.0;
    int    m_chatMessagesSinceLastInfo = 0;  ///< suppresses info spam in quiet chat
    static constexpr int INFO_MSG_MIN_CHAT_ACTIVITY = 5;
    std::string m_lastGameId;

    // Per-stream interaction statistics
    ChannelStats m_stats;

    // Twitch API cache (broadcaster IDs, game IDs)
    std::unordered_map<std::string, std::string> m_twitchBroadcasterIdCache;
    std::unordered_map<std::string, std::string> m_twitchGameIdCache;

    // YouTube API cache (channel -> broadcast ID)
    std::unordered_map<std::string, std::string> m_youtubeBroadcastIdCache;

    // YouTube update rate limiting (min 2 min between updateBroadcast calls)
    std::chrono::steady_clock::time_point m_lastYoutubeUpdateTime;
    static constexpr int YOUTUBE_UPDATE_MIN_INTERVAL_SEC = 600; // 10 min — each update costs 51 quota units
};

} // namespace is::core
