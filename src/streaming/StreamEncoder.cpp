#include "streaming/StreamEncoder.h"
#include <spdlog/spdlog.h>
#include <chrono>
#include <sstream>

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

namespace is::streaming {

StreamEncoder::StreamEncoder(core::Config& config)
    : m_config(config)
{
    m_ffmpegPath = config.get<std::string>("streaming.ffmpeg_path", "ffmpeg");
    m_outputUrl  = config.get<std::string>("streaming.output_url", "");
    m_width      = config.get<int>("rendering.width", 1080);
    m_height     = config.get<int>("rendering.height", 1920);
    m_fps        = config.get<int>("streaming.fps", 30);
    m_bitrate    = config.get<int>("streaming.bitrate_kbps", 4500);
    m_preset     = config.get<std::string>("streaming.preset", "fast");
    m_codec      = config.get<std::string>("streaming.codec", "libx264");

    m_frameBuffer.resize(m_width * m_height * 4);  // RGBA

    spdlog::info("[StreamEncoder] Configured: {}x{} @ {} fps, {} kbps, codec: {}",
        m_width, m_height, m_fps, m_bitrate, m_codec);
}

StreamEncoder::~StreamEncoder() {
    stop();
}

bool StreamEncoder::start() {
    if (m_outputUrl.empty()) {
        spdlog::info("[StreamEncoder] No output URL configured – encoding disabled.");
        return false;
    }

    // Build FFmpeg command
    std::ostringstream cmd;
    cmd << m_ffmpegPath
        << " -y"
        << " -f rawvideo"
        << " -vcodec rawvideo"
        << " -pix_fmt rgba"
        << " -s " << m_width << "x" << m_height
        << " -r " << m_fps
        << " -i -"  // Read from stdin
        << " -c:v " << m_codec
        << " -pix_fmt yuv420p"
        << " -preset " << m_preset
        << " -b:v " << m_bitrate << "k"
        << " -maxrate " << static_cast<int>(m_bitrate * 1.5) << "k"
        << " -bufsize " << m_bitrate * 2 << "k"
        << " -g " << m_fps * 2  // Keyframe interval
        << " -f flv"
        << " \"" << m_outputUrl << "\"";

    std::string cmdStr = cmd.str();
    spdlog::info("[StreamEncoder] Starting FFmpeg: {}", cmdStr);

    m_pipe = popen(cmdStr.c_str(), "wb");
    if (!m_pipe) {
        spdlog::error("[StreamEncoder] Failed to start FFmpeg process.");
        return false;
    }

    m_running = true;
    m_thread = std::thread(&StreamEncoder::encoderThread, this);

    spdlog::info("[StreamEncoder] Encoding started.");
    return true;
}

void StreamEncoder::stop() {
    m_running = false;
    m_cv.notify_all();

    if (m_thread.joinable()) {
        m_thread.join();
    }

    if (m_pipe) {
        pclose(m_pipe);
        m_pipe = nullptr;
    }

    spdlog::info("[StreamEncoder] Encoding stopped. Total frames: {}", m_frameCount.load());
}

void StreamEncoder::encodeFrame(const sf::Uint8* pixelData) {
    if (!m_running || !pixelData) return;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::memcpy(m_frameBuffer.data(), pixelData, m_frameBuffer.size());
        m_frameReady = true;
    }
    m_cv.notify_one();
}

void StreamEncoder::encoderThread() {
    spdlog::info("[StreamEncoder] Encoder thread started.");

    auto lastFpsTime = std::chrono::steady_clock::now();
    size_t fpsFrameCount = 0;

    while (m_running) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this] { return m_frameReady || !m_running; });

        if (!m_running) break;
        if (!m_frameReady) continue;

        // Write frame to FFmpeg's stdin
        if (m_pipe) {
            // SFML gives us RGBA pixels top-to-bottom, which is what we told FFmpeg to expect
            size_t frameSize = m_frameBuffer.size();
            size_t written = fwrite(m_frameBuffer.data(), 1, frameSize, m_pipe);

            if (written != frameSize) {
                spdlog::warn("[StreamEncoder] Incomplete frame write: {}/{}", written, frameSize);
            }
        }

        m_frameReady = false;
        lock.unlock();

        m_frameCount++;
        fpsFrameCount++;

        // Calculate FPS every second
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration<double>(now - lastFpsTime).count();
        if (elapsed >= 1.0) {
            m_currentFps = static_cast<float>(fpsFrameCount / elapsed);
            fpsFrameCount = 0;
            lastFpsTime = now;
        }
    }

    spdlog::info("[StreamEncoder] Encoder thread exiting.");
}

} // namespace is::streaming
