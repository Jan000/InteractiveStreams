#pragma once

#include "core/GameManager.h"
#include "platform/ChatMessage.h"

#include <SFML/Graphics.hpp>
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <unordered_map>
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
    Mobile,  ///< 1080x1920 (9:16)
    Desktop  ///< 1920x1080 (16:9)
};

/// Serialisable configuration for a single stream instance.
struct StreamConfig {
    std::string id;
    std::string name = "Stream";
    std::string title;                     ///< Stream title (shown on Twitch/YouTube)
    std::string description;               ///< Stream description / about
    ResolutionPreset resolution = ResolutionPreset::Mobile;
    GameModeType gameMode = GameModeType::Fixed;
    std::string fixedGame = "chaos_arena";
    std::vector<std::string> channelIds;   ///< subscribed input channels
    std::string streamUrl;                 ///< RTMP output URL
    std::string streamKey;
    bool enabled = true;
    int fps      = 30;
    int bitrate  = 4500;
    std::string preset = "fast";
    std::string codec  = "libx264";

    int width()  const { return resolution == ResolutionPreset::Mobile ? 1080 : 1920; }
    int height() const { return resolution == ResolutionPreset::Mobile ? 1920 : 1080; }

    std::string getFullStreamUrl() const {
        if (streamKey.empty()) return streamUrl;
        return streamUrl + "/" + streamKey;
    }
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

    // ── Streaming control ────────────────────────────────────────────────

    bool isStreaming() const;
    /// Start streaming. Returns true on success, false if config is invalid.
    bool startStreaming();
    void stopStreaming();

    // ── Serialisation ────────────────────────────────────────────────────

    nlohmann::json getState() const;
    nlohmann::json toJson() const;
    static StreamConfig configFromJson(const nlohmann::json& j);

private:
    void ensureRenderTexture();
    void renderVoteOverlay();
    void startNextGameFromVote();
    void startRandomGame();
    void restartCurrentGame();
    void updateJpegBuffer();
    std::vector<std::string> getAvailableGameIds() const;

    StreamConfig                               m_config;
    std::unique_ptr<GameManager>               m_gameManager;

    // Rendering (lazily initialised)
    sf::RenderTexture  m_renderTexture;
    sf::Image          m_frameCapture;
    sf::Font           m_font;
    bool               m_fontLoaded = false;
    bool               m_rtReady    = false;

    // Encoding
    std::unique_ptr<streaming::StreamEncoder>  m_encoder;

    // JPEG frame buffer for web preview (thread-safe)
    mutable std::mutex     m_jpegMutex;
    std::vector<uint8_t>   m_jpegBuffer;
    int                    m_jpegFrameCounter  = 0;
    int                    m_jpegFrameInterval = 6;  // encode JPEG every Nth frame (~10fps at 60fps)

    // Vote state
    struct VoteState {
        bool   active   = false;
        double timer    = 0.0;
        double duration = 20.0;
        std::unordered_map<std::string, std::string> userVotes; // userId → gameId
        std::unordered_map<std::string, int>         tallies;   // gameId → count
    };
    VoteState m_voteState;

    // Game-mode transition state
    bool   m_waitingForTransition = false;
    double m_transitionTimer      = 0.0;
    double m_transitionDelay      = 3.0; // seconds before next game
};

} // namespace is::core
