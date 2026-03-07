#pragma once

#include <SFML/Graphics.hpp>
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <filesystem>

namespace is::core { class Config; }

namespace is::streaming {

/// Direct settings for creating a StreamEncoder without a Config object.
struct EncoderSettings {
    std::string ffmpegPath = "ffmpeg";
    std::string outputUrl;
    int         width      = 1080;
    int         height     = 1920;
    int         fps        = 30;
    int         bitrate    = 4500;  // kbps
    std::string preset     = "ultrafast";
    std::string codec      = "libx264";
};

/// Encodes rendered frames and pipes them to FFmpeg for RTMP streaming.
class StreamEncoder {
public:
    /// Construct from a Config object (legacy / single-stream path).
    explicit StreamEncoder(core::Config& config);

    /// Construct from explicit settings (multi-stream path).
    explicit StreamEncoder(const EncoderSettings& settings);

    ~StreamEncoder();

    /// Start the encoding pipeline.
    bool start();

    /// Stop encoding.
    void stop();

    /// Submit a frame for encoding.
    void encodeFrame(const sf::Uint8* pixelData);

    /// Check if encoder is running.
    bool isRunning() const { return m_running; }

    /// True if the FFmpeg pipe broke (process exited / crashed).
    bool hasFailed() const { return m_failed; }

    /// Path to the FFmpeg stderr log file (empty if not started).
    const std::string& stderrLogPath() const { return m_stderrLogPath; }

    /// Get current FPS.
    float getFps() const { return m_currentFps; }

    /// Get total frames encoded.
    size_t getFrameCount() const { return m_frameCount; }

private:
    void encoderThread();

    /// Strip RGBA pixels to RGB24 (discards alpha channel).
    /// Reads from `rgba` (width*height*4 bytes), writes to `rgb` (width*height*3 bytes).
    static void stripAlpha(const sf::Uint8* rgba, sf::Uint8* rgb, size_t pixelCount);

    // FFmpeg process
    FILE*         m_pipe     = nullptr;
    std::string   m_ffmpegPath;
    std::string   m_outputUrl;
    int           m_width;
    int           m_height;
    int           m_fps;
    int           m_bitrate;   // kbps
    std::string   m_preset;
    std::string   m_codec;

    // Threading
    std::atomic<bool>       m_running{false};
    std::thread             m_thread;
    std::mutex              m_mutex;
    std::condition_variable m_cv;
    std::vector<sf::Uint8>  m_frameBuffer;
    bool                    m_frameReady = false;

    // Error state
    std::atomic<bool>   m_failed{false};
    std::string         m_stderrLogPath;

    // Stats
    std::atomic<size_t> m_frameCount{0};
    std::atomic<float>  m_currentFps{0.0f};
};

} // namespace is::streaming
