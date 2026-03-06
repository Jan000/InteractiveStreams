#include "streaming/StreamEncoder.h"
#include "core/Config.h"
#include <spdlog/spdlog.h>
#include <chrono>
#include <sstream>
#include <fstream>
#include <cerrno>
#include <cstring>

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

namespace is::streaming {

StreamEncoder::StreamEncoder(core::Config& config)
{
    m_ffmpegPath = config.get<std::string>("streaming.ffmpeg_path", "ffmpeg");
    m_outputUrl  = config.get<std::string>("streaming.output_url", "");
    m_width      = config.get<int>("rendering.width", 1080);
    m_height     = config.get<int>("rendering.height", 1920);
    m_fps        = config.get<int>("streaming.fps", 30);
    m_bitrate    = config.get<int>("streaming.bitrate_kbps", 4500);
    m_preset     = config.get<std::string>("streaming.preset", "ultrafast");
    m_codec      = config.get<std::string>("streaming.codec", "libx264");

    m_frameBuffer.resize(m_width * m_height * 4);  // RGBA

    spdlog::info("[StreamEncoder] Configured: {}x{} @ {} fps, {} kbps, codec: {}",
        m_width, m_height, m_fps, m_bitrate, m_codec);
}

StreamEncoder::StreamEncoder(const EncoderSettings& s)
{
    m_ffmpegPath = s.ffmpegPath;
    m_outputUrl  = s.outputUrl;
    m_width      = s.width;
    m_height     = s.height;
    m_fps        = s.fps;
    m_bitrate    = s.bitrate;
    m_preset     = s.preset;
    m_codec      = s.codec;

    m_frameBuffer.resize(m_width * m_height * 4);

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

    // Quick check: can we find the FFmpeg binary?
    {
        std::string checkCmd = m_ffmpegPath + " -version";
#ifdef _WIN32
        checkCmd += " >nul 2>nul";
#else
        checkCmd += " >/dev/null 2>/dev/null";
#endif
        int rc = std::system(checkCmd.c_str());
        if (rc != 0) {
            spdlog::error("[StreamEncoder] FFmpeg not found at '{}'. "
                          "Install FFmpeg or set the correct path in Settings.",
                          m_ffmpegPath);
            m_failed = true;
            return false;
        }
    }

    // Build stderr log path next to the executable
    {
        namespace fs = std::filesystem;
        auto logDir = fs::current_path();
        m_stderrLogPath = (logDir / "ffmpeg_stderr.log").string();
    }

    // Build FFmpeg command (redirect stderr to a log file for diagnostics)
    std::ostringstream cmd;
    cmd << m_ffmpegPath
        << " -y"
        << " -loglevel warning"
        // Video input: raw RGBA frames from stdin
        << " -f rawvideo"
        << " -vcodec rawvideo"
        << " -pix_fmt rgba"
        << " -s " << m_width << "x" << m_height
        << " -r " << m_fps
        << " -i -"
        // Silent audio input (many platforms like YouTube reject video-only RTMP)
        << " -f lavfi -i anullsrc=channel_layout=stereo:sample_rate=44100"
        // Video encoding
        << " -c:v " << m_codec
        << " -pix_fmt yuv420p"
        << " -preset " << m_preset
        << " -threads 0"         // Use all available CPU cores for encoding
        << " -b:v " << m_bitrate << "k"
        << " -maxrate " << static_cast<int>(m_bitrate * 1.5) << "k"
        << " -bufsize " << m_bitrate * 2 << "k"
        << " -g " << m_fps * 2  // Keyframe interval
        << " -tune zerolatency"  // Low-latency encoding for streaming
        << " -x264-params \"sliced-threads=1:rc-lookahead=0\""  // Threaded slices for speed
        // Audio encoding (silence)
        << " -c:a aac -b:a 128k -ar 44100"
        << " -shortest"  // Stop when the shortest input (video) ends
        << " -f flv"
        << " \"" << m_outputUrl << "\""
        << " 2>\"" << m_stderrLogPath << "\"";

    std::string cmdStr = cmd.str();
    spdlog::info("[StreamEncoder] Starting FFmpeg: {}", cmdStr);
    spdlog::info("[StreamEncoder] FFmpeg stderr log: {}", m_stderrLogPath);

    // popen mode: Windows _popen needs "wb" for binary; POSIX popen only
    // accepts "r"/"w"/"re"/"we" (glibc 2.38+ rejects "wb" with EINVAL).
#ifdef _WIN32
    const char* popenMode = "wb";
#else
    const char* popenMode = "w";
#endif
    errno = 0;
    m_pipe = popen(cmdStr.c_str(), popenMode);
    if (!m_pipe) {
        spdlog::error("[StreamEncoder] Failed to start FFmpeg process: {} (errno={})",
                      std::strerror(errno), errno);
        return false;
    }

    m_running = true;
    m_thread = std::thread(&StreamEncoder::encoderThread, this);

    spdlog::info("[StreamEncoder] Encoding started.");
    return true;
}

void StreamEncoder::stop() {
    // Guard against double-stop (destructor after explicit stop)
    bool wasRunning = m_running.exchange(false);
    if (!wasRunning && !m_pipe && !m_thread.joinable()) return;

    m_cv.notify_all();

    if (m_thread.joinable()) {
        m_thread.join();
    }

    if (m_pipe) {
        pclose(m_pipe);
        m_pipe = nullptr;
    }

    spdlog::info("[StreamEncoder] Encoding stopped. Total frames: {}", m_frameCount.load());

    // If it failed, try to read FFmpeg's stderr log for diagnostics
    if (m_failed && !m_stderrLogPath.empty()) {
        std::ifstream logFile(m_stderrLogPath);
        if (logFile.is_open()) {
            std::string contents((std::istreambuf_iterator<char>(logFile)),
                                  std::istreambuf_iterator<char>());
            if (!contents.empty()) {
                spdlog::error("[StreamEncoder] FFmpeg stderr output:\n{}", contents);
            }
        }
    }
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
            size_t frameSize = m_frameBuffer.size();
            size_t written = fwrite(m_frameBuffer.data(), 1, frameSize, m_pipe);

            if (written != frameSize) {
                // Check if the pipe is broken (FFmpeg exited)
                if (written == 0 || ferror(m_pipe)) {
                    spdlog::error("[StreamEncoder] FFmpeg pipe broken (wrote {}/{} bytes). "
                                  "FFmpeg likely exited – check {}.",
                                  written, frameSize, m_stderrLogPath);
                    m_failed = true;
                    m_running = false;
                    break;
                }
                spdlog::warn("[StreamEncoder] Incomplete frame write: {}/{}", written, frameSize);
            } else {
                fflush(m_pipe);
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
