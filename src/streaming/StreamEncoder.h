#pragma once

#include "core/Config.h"
#include <SFML/Graphics.hpp>
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>

namespace is::streaming {

/// Encodes rendered frames and pipes them to FFmpeg for RTMP streaming.
class StreamEncoder {
public:
    explicit StreamEncoder(core::Config& config);
    ~StreamEncoder();

    /// Start the encoding pipeline.
    bool start();

    /// Stop encoding.
    void stop();

    /// Submit a frame for encoding.
    void encodeFrame(const sf::Uint8* pixelData);

    /// Check if encoder is running.
    bool isRunning() const { return m_running; }

    /// Get current FPS.
    float getFps() const { return m_currentFps; }

    /// Get total frames encoded.
    size_t getFrameCount() const { return m_frameCount; }

private:
    void encoderThread();

    core::Config& m_config;

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

    // Stats
    std::atomic<size_t> m_frameCount{0};
    std::atomic<float>  m_currentFps{0.0f};
};

} // namespace is::streaming
