#pragma once

#include <SFML/Graphics.hpp>
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#endif

namespace is::core { class Config; class AudioMixer; }

namespace is::streaming {

/// Direct settings for creating a StreamEncoder without a Config object.
struct EncoderSettings {
    std::string ffmpegPath = "ffmpeg";
    std::string outputUrl;
    int         width          = 1080;
    int         height         = 1920;
    int         fps            = 30;
    int         bitrate        = 6000;  // kbps (YouTube recommends ~6800 for 1080p@30fps)
    std::string preset         = "ultrafast";
    std::string codec          = "libx264";
    std::string profile        = "baseline"; // H.264 profile: baseline, main, high
    std::string tune           = "zerolatency";
    int         keyframeInterval = 2;        // GOP size in seconds
    int         threads        = 0;          // 0 = auto
    float       maxrateFactor  = 1.2f;       // maxrate = bitrate * factor
    float       bufsizeFactor  = 1.0f;       // bufsize = bitrate * factor

    // Audio settings
    int         audioBitrate   = 128;        // kbps
    int         audioSampleRate = 44100;
    std::string audioCodec     = "aac";

    /// If non-null, real game audio is piped from this mixer instead of silence.
    core::AudioMixer* audioMixer = nullptr;
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
    void audioThread();

    /// Convert RGBA pixels to YUV420P planar format (BT.601).
    static void rgbaToYuv420p(const sf::Uint8* rgba, sf::Uint8* yuv,
                              int width, int height);

    /// Create a platform-specific named pipe for audio.
    /// Returns the pipe path that FFmpeg should open as input.
    std::string createAudioPipe();
    /// Open the audio named pipe for writing (blocks until FFmpeg connects).
    bool openAudioPipeForWriting();
    /// Close and clean up the audio pipe.
    void closeAudioPipe();

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
    std::string   m_profile;
    std::string   m_tune;
    int           m_keyframeInterval;
    int           m_threads;
    float         m_maxrateFactor;
    float         m_bufsizeFactor;

    // Audio settings
    int           m_audioBitrate;
    int           m_audioSampleRate;
    std::string   m_audioCodec;
    core::AudioMixer* m_audioMixer = nullptr;

    // Audio pipe
    std::string   m_audioPipePath;
    std::thread   m_audioThread;
#ifdef _WIN32
    HANDLE        m_audioPipeHandle = INVALID_HANDLE_VALUE;
#else
    FILE*         m_audioPipeFile   = nullptr;
#endif

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
