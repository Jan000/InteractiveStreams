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
        // Video input: raw YUV420P frames from stdin (converted by encoder thread)
        << " -f rawvideo"
        << " -vcodec rawvideo"
        << " -pix_fmt yuv420p"
        << " -s " << m_width << "x" << m_height
        << " -r " << m_fps
        << " -i -"
        // Silent audio input (many platforms like YouTube reject video-only RTMP)
        << " -f lavfi -i anullsrc=channel_layout=stereo:sample_rate=44100"
        // Video encoding — input is already yuv420p so no swscale conversion needed
        << " -c:v " << m_codec
        << " -preset " << m_preset
        << " -profile:v baseline"    // Simplest/fastest H.264 profile
        << " -threads 0"             // Use all available CPU cores
        << " -b:v " << m_bitrate << "k"
        << " -maxrate " << static_cast<int>(m_bitrate * 1.2) << "k"
        << " -bufsize " << m_bitrate << "k"   // Tight VBV for consistent bitrate
        << " -g " << m_fps * 2        // Keyframe interval
        << " -tune zerolatency"       // Low-latency encoding
        << " -x264-params \"sliced-threads=1\""  // Force slice-based multi-core encoding
        // Audio encoding (silence)
        << " -c:a aac -b:a 128k -ar 44100"
        // Explicitly map video from stdin and audio from anullsrc,
        // so both are guaranteed to appear in the FLV output.
        << " -map 0:v -map 1:a"
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

    // Set a large stdio buffer to reduce syscall overhead.
    // Each YUV420P frame is ~3.1 MB at 1080x1920 (vs 6.2 MB RGB24, 8.3 MB RGBA).
    setvbuf(m_pipe, nullptr, _IOFBF, 2 * 1024 * 1024); // 2 MB buffered I/O

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

    const size_t pixelCount    = static_cast<size_t>(m_width) * m_height;
    const size_t yuvFrameSize  = pixelCount * 3 / 2;  // YUV420P: 1.5 bytes/pixel

    // Local RGBA buffer (fast memcpy under mutex) + YUV420P buffer (written to pipe).
    std::vector<sf::Uint8> localBuffer(m_frameBuffer.size());  // RGBA
    std::vector<sf::Uint8> yuvBuffer(yuvFrameSize);            // YUV420P

    auto lastFpsTime = std::chrono::steady_clock::now();
    size_t fpsFrameCount = 0;
    size_t droppedFrames  = 0;

    while (m_running) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this] { return m_frameReady || !m_running; });

        if (!m_running) break;
        if (!m_frameReady) continue;

        // Grab the RGBA frame and release the lock immediately.
        std::memcpy(localBuffer.data(), m_frameBuffer.data(), localBuffer.size());
        m_frameReady = false;
        lock.unlock();

        // Convert RGBA → YUV420P (outside mutex).
        // This replaces both the old stripAlpha AND FFmpeg's internal swscale,
        // and the output is 3.1 MB instead of 6.2 MB (RGB24) or 8.3 MB (RGBA).
        rgbaToYuv420p(localBuffer.data(), yuvBuffer.data(), m_width, m_height);

        // Write YUV420P frame to FFmpeg's stdin (outside lock)
        if (m_pipe) {
            size_t written = fwrite(yuvBuffer.data(), 1, yuvFrameSize, m_pipe);

            if (written != yuvFrameSize) {
                if (written == 0 || ferror(m_pipe)) {
                    spdlog::error("[StreamEncoder] FFmpeg pipe broken (wrote {}/{} bytes). "
                                  "FFmpeg likely exited – check {}.",
                                  written, yuvFrameSize, m_stderrLogPath);
                    m_failed = true;
                    m_running = false;
                    break;
                }
                spdlog::warn("[StreamEncoder] Incomplete frame write: {}/{}", written, yuvFrameSize);
            }
        }

        m_frameCount++;
        fpsFrameCount++;

        // Track frame drops (main thread overwrote buffer while we were writing)
        {
            std::lock_guard<std::mutex> g(m_mutex);
            if (m_frameReady) droppedFrames++;
        }

        // Calculate FPS every second
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration<double>(now - lastFpsTime).count();
        if (elapsed >= 1.0) {
            m_currentFps = static_cast<float>(fpsFrameCount / elapsed);
            if (droppedFrames > 0) {
                spdlog::debug("[StreamEncoder] {:.1f} encode-fps, {} frame(s) dropped in last {:.1f}s",
                              m_currentFps.load(), droppedFrames, elapsed);
                droppedFrames = 0;
            }
            fpsFrameCount = 0;
            lastFpsTime = now;
        }
    }

    // Flush remaining buffered data before closing the pipe
    if (m_pipe) fflush(m_pipe);

    spdlog::info("[StreamEncoder] Encoder thread exiting.");
}

void StreamEncoder::rgbaToYuv420p(const sf::Uint8* rgba, sf::Uint8* yuv,
                                  int width, int height)
{
    // BT.601 full-range → limited-range conversion using fixed-point arithmetic.
    // Y  = ((66*R + 129*G + 25*B + 128) >> 8) + 16
    // Cb = ((-38*R - 74*G + 112*B + 128) >> 8) + 128
    // Cr = ((112*R - 94*G - 18*B + 128) >> 8) + 128
    //
    // Layout: Y plane (w*h), then U plane (w/2 * h/2), then V plane (w/2 * h/2).
    // U and V are subsampled 2×2 by averaging the 4 contributing pixels.

    const int w = width;
    const int h = height;
    sf::Uint8* yPlane = yuv;
    sf::Uint8* uPlane = yuv + w * h;
    sf::Uint8* vPlane = uPlane + (w / 2) * (h / 2);

    for (int row = 0; row < h; row += 2) {
        const sf::Uint8* row0 = rgba + row * w * 4;
        const sf::Uint8* row1 = row0 + w * 4;
        sf::Uint8* yRow0 = yPlane + row * w;
        sf::Uint8* yRow1 = yRow0 + w;
        sf::Uint8* uPtr  = uPlane + (row / 2) * (w / 2);
        sf::Uint8* vPtr  = vPlane + (row / 2) * (w / 2);

        for (int col = 0; col < w; col += 2) {
            // Read 4 pixels in a 2×2 block
            int r00 = row0[0], g00 = row0[1], b00 = row0[2];
            int r01 = row0[4], g01 = row0[5], b01 = row0[6];
            int r10 = row1[0], g10 = row1[1], b10 = row1[2];
            int r11 = row1[4], g11 = row1[5], b11 = row1[6];

            // Y for each pixel
            yRow0[0] = static_cast<sf::Uint8>(((66 * r00 + 129 * g00 + 25 * b00 + 128) >> 8) + 16);
            yRow0[1] = static_cast<sf::Uint8>(((66 * r01 + 129 * g01 + 25 * b01 + 128) >> 8) + 16);
            yRow1[0] = static_cast<sf::Uint8>(((66 * r10 + 129 * g10 + 25 * b10 + 128) >> 8) + 16);
            yRow1[1] = static_cast<sf::Uint8>(((66 * r11 + 129 * g11 + 25 * b11 + 128) >> 8) + 16);

            // Average the 2×2 block for chroma subsampling
            int rAvg = (r00 + r01 + r10 + r11 + 2) >> 2;
            int gAvg = (g00 + g01 + g10 + g11 + 2) >> 2;
            int bAvg = (b00 + b01 + b10 + b11 + 2) >> 2;

            // U (Cb) and V (Cr)
            *uPtr++ = static_cast<sf::Uint8>(((-38 * rAvg - 74 * gAvg + 112 * bAvg + 128) >> 8) + 128);
            *vPtr++ = static_cast<sf::Uint8>(((112 * rAvg - 94 * gAvg - 18 * bAvg + 128) >> 8) + 128);

            row0 += 8;  // advance 2 pixels × 4 bytes
            row1 += 8;
            yRow0 += 2;
            yRow1 += 2;
        }
    }
}

} // namespace is::streaming
