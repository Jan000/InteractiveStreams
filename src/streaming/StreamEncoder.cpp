#include "streaming/StreamEncoder.h"
#include "core/Config.h"
#include "core/AudioMixer.h"
#include <spdlog/spdlog.h>
#include <chrono>
#include <sstream>
#include <fstream>
#include <cerrno>
#include <cstring>
#include <random>

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace is::streaming {

StreamEncoder::StreamEncoder(core::Config& config)
{
    m_ffmpegPath       = config.get<std::string>("streaming.ffmpeg_path", "ffmpeg");
    m_outputUrl        = config.get<std::string>("streaming.output_url", "");
    m_width            = config.get<int>("rendering.width", 1080);
    m_height           = config.get<int>("rendering.height", 1920);
    m_fps              = config.get<int>("streaming.fps", 30);
    m_bitrate          = config.get<int>("streaming.bitrate_kbps", 6000);
    m_preset           = config.get<std::string>("streaming.preset", "ultrafast");
    m_codec            = config.get<std::string>("streaming.codec", "libx264");
    m_profile          = config.get<std::string>("streaming.profile", "baseline");
    m_tune             = config.get<std::string>("streaming.tune", "zerolatency");
    m_keyframeInterval = config.get<int>("streaming.keyframe_interval", 2);
    m_threads          = config.get<int>("streaming.threads", 2);
    m_cbr              = config.get<bool>("streaming.cbr", true);
    m_maxrateFactor    = config.get<float>("streaming.maxrate_factor", 1.2f);
    m_bufsizeFactor    = config.get<float>("streaming.bufsize_factor", 1.0f);
    m_audioBitrate     = config.get<int>("streaming.audio_bitrate", 128);
    m_audioSampleRate  = config.get<int>("streaming.audio_sample_rate", 44100);
    m_audioCodec       = config.get<std::string>("streaming.audio_codec", "aac");
    m_audioMixer       = nullptr;

    m_frameBuffer.resize(m_width * m_height * 4);  // RGBA

    spdlog::info("[StreamEncoder] Configured: {}x{} @ {} fps, {} kbps, codec: {}",
        m_width, m_height, m_fps, m_bitrate, m_codec);
}

StreamEncoder::StreamEncoder(const EncoderSettings& s)
{
    m_ffmpegPath       = s.ffmpegPath;
    m_outputUrl        = s.outputUrl;
    m_width            = s.width;
    m_height           = s.height;
    m_fps              = s.fps;
    m_bitrate          = s.bitrate;
    m_preset           = s.preset;
    m_codec            = s.codec;
    m_profile          = s.profile;
    m_tune             = s.tune;
    m_keyframeInterval = s.keyframeInterval;
    m_threads          = s.threads;
    m_cbr              = s.cbr;
    m_maxrateFactor    = s.maxrateFactor;
    m_bufsizeFactor    = s.bufsizeFactor;
    m_audioBitrate     = s.audioBitrate;
    m_audioSampleRate  = s.audioSampleRate;
    m_audioCodec       = s.audioCodec;
    m_audioMixer       = s.audioMixer;

    m_frameBuffer.resize(m_width * m_height * 4);

    spdlog::info("[StreamEncoder] Configured: {}x{} @ {} fps, {} kbps, codec: {}, audio: {}",
        m_width, m_height, m_fps, m_bitrate, m_codec,
        m_audioMixer ? "mixer" : "silent");
}

StreamEncoder::~StreamEncoder() {
    stop();
}

// ── Named pipe for audio ─────────────────────────────────────────────────────

std::string StreamEncoder::createAudioPipe() {
    // Generate a unique pipe name
    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<int> dist(10000, 99999);
    int id = dist(rng);

#ifdef _WIN32
    std::string pipePath = "\\\\.\\pipe\\is_audio_" + std::to_string(id);
    m_audioPipeHandle = CreateNamedPipeA(
        pipePath.c_str(),
        PIPE_ACCESS_OUTBOUND,                    // Write-only
        PIPE_TYPE_BYTE | PIPE_WAIT,              // Byte mode, blocking
        1,                                        // Max instances
        1024 * 1024,                              // Output buffer 1 MB
        0,                                        // Input buffer (unused)
        0,                                        // Default timeout
        nullptr                                   // Default security
    );
    if (m_audioPipeHandle == INVALID_HANDLE_VALUE) {
        spdlog::error("[StreamEncoder] Failed to create named pipe: {} (err={})",
                      pipePath, GetLastError());
        return {};
    }
    spdlog::info("[StreamEncoder] Created audio pipe: {}", pipePath);
#else
    std::string pipePath = "/tmp/is_audio_" + std::to_string(id);
    // Remove stale pipe if it exists
    ::unlink(pipePath.c_str());
    if (::mkfifo(pipePath.c_str(), 0644) != 0) {
        spdlog::error("[StreamEncoder] Failed to create FIFO: {} ({})",
                      pipePath, std::strerror(errno));
        return {};
    }
    spdlog::info("[StreamEncoder] Created audio FIFO: {}", pipePath);
#endif

    m_audioPipePath = pipePath;
    return pipePath;
}

bool StreamEncoder::openAudioPipeForWriting() {
#ifdef _WIN32
    if (m_audioPipeHandle == INVALID_HANDLE_VALUE) return false;
    // Wait for FFmpeg to connect (blocks)
    if (!ConnectNamedPipe(m_audioPipeHandle, nullptr)) {
        DWORD err = GetLastError();
        if (err != ERROR_PIPE_CONNECTED) {
            spdlog::error("[StreamEncoder] ConnectNamedPipe failed: {}", err);
            return false;
        }
    }
    spdlog::info("[StreamEncoder] FFmpeg connected to audio pipe.");
    return true;
#else
    // On Linux, open() on a FIFO blocks until a reader connects (FFmpeg)
    m_audioPipeFile = fopen(m_audioPipePath.c_str(), "wb");
    if (!m_audioPipeFile) {
        spdlog::error("[StreamEncoder] Failed to open FIFO for writing: {} ({})",
                      m_audioPipePath, std::strerror(errno));
        return false;
    }
    setvbuf(m_audioPipeFile, nullptr, _IOFBF, 256 * 1024); // 256 KB buffer
    spdlog::info("[StreamEncoder] FFmpeg connected to audio FIFO.");
    return true;
#endif
}

void StreamEncoder::closeAudioPipe() {
#ifdef _WIN32
    if (m_audioPipeHandle != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(m_audioPipeHandle);
        DisconnectNamedPipe(m_audioPipeHandle);
        CloseHandle(m_audioPipeHandle);
        m_audioPipeHandle = INVALID_HANDLE_VALUE;
    }
#else
    if (m_audioPipeFile) {
        fflush(m_audioPipeFile);
        fclose(m_audioPipeFile);
        m_audioPipeFile = nullptr;
    }
    if (!m_audioPipePath.empty()) {
        ::unlink(m_audioPipePath.c_str());
    }
#endif
    m_audioPipePath.clear();
}

// ── Start / Stop ─────────────────────────────────────────────────────────────

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

    // Create audio named pipe if we have a mixer
    std::string audioPipePath;
    if (m_audioMixer) {
        audioPipePath = createAudioPipe();
        if (audioPipePath.empty()) {
            spdlog::warn("[StreamEncoder] Audio pipe creation failed – falling back to silent audio.");
            m_audioMixer = nullptr; // fallback
        }
    }

    // Build FFmpeg command
    std::ostringstream cmd;
    cmd << m_ffmpegPath
        << " -y"
        << " -loglevel warning"
        // Video input: raw YUV420P frames from stdin
        << " -f rawvideo"
        << " -vcodec rawvideo"
        << " -pix_fmt yuv420p"
        << " -s " << m_width << "x" << m_height
        << " -r " << m_fps
        << " -i -";

    // Audio input: either named pipe (real audio) or anullsrc (silence)
    if (m_audioMixer && !audioPipePath.empty()) {
        cmd << " -thread_queue_size 512"
            << " -f s16le"
            << " -ar " << m_audioSampleRate
            << " -ac 2"
            << " -i \"" << audioPipePath << "\"";
    } else {
        cmd << " -f lavfi -i anullsrc=channel_layout=stereo:sample_rate=" << m_audioSampleRate;
    }

    // Video encoding — input is already yuv420p so no swscale conversion needed
    int gopSize = m_fps * m_keyframeInterval;
    cmd << " -c:v " << m_codec
        << " -preset " << m_preset
        << " -profile:v " << m_profile
        << " -threads " << m_threads
        << " -b:v " << m_bitrate << "k";

    if (m_cbr) {
        // Strict CBR: minrate == maxrate == bitrate, bufsize = 2x, nal-hrd cbr
        cmd << " -minrate " << m_bitrate << "k"
            << " -maxrate " << m_bitrate << "k"
            << " -bufsize " << (m_bitrate * 2) << "k"
            << " -nal-hrd cbr";
    } else {
        // VBR with factor-based rate control
        cmd << " -maxrate " << static_cast<int>(m_bitrate * m_maxrateFactor) << "k"
            << " -bufsize " << static_cast<int>(m_bitrate * m_bufsizeFactor) << "k";
    }

    cmd << " -g " << gopSize
        << " -keyint_min " << gopSize
        << " -tune " << m_tune
        << " -x264-params \"sliced-threads=1\"";

    // Audio encoding
    cmd << " -c:a " << m_audioCodec
        << " -b:a " << m_audioBitrate << "k"
        << " -ar " << m_audioSampleRate;

    // Explicit stream mapping
    cmd << " -map 0:v -map 1:a"
        << " -flush_packets 1"
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
        closeAudioPipe();
        return false;
    }

    // Set a stdio buffer – large enough to batch small writes, small enough
    // to avoid accumulating latency in the pipe.
    setvbuf(m_pipe, nullptr, _IOFBF, 256 * 1024); // 256 KB buffered I/O

    m_running = true;
    m_thread = std::thread(&StreamEncoder::encoderThread, this);

    // Start audio thread if we have a mixer and pipe
    if (m_audioMixer && !m_audioPipePath.empty()) {
        m_audioThread = std::thread(&StreamEncoder::audioThread, this);
    }

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

    // Close audio pipe first (unblocks FFmpeg's audio read → allows clean exit)
    closeAudioPipe();

    if (m_audioThread.joinable()) {
        m_audioThread.join();
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
    spdlog::info("[StreamEncoder] Encoder thread started (frame-paced at {} fps).", m_fps);

    const size_t pixelCount    = static_cast<size_t>(m_width) * m_height;
    const size_t yuvFrameSize  = pixelCount * 3 / 2;  // YUV420P: 1.5 bytes/pixel

    // Local RGBA buffer (fast memcpy under mutex) + YUV420P buffer (written to pipe).
    std::vector<sf::Uint8> localBuffer(m_frameBuffer.size());  // RGBA
    std::vector<sf::Uint8> yuvBuffer(yuvFrameSize);            // YUV420P

    bool hasFrame = false; // true once we've received at least one frame

    // Frame-pacing: deliver frames to FFmpeg at exact intervals.
    // This prevents bursty delivery that causes YouTube/Twitch buffering.
    const auto frameDuration = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(1.0 / m_fps));
    auto nextFrameTime = std::chrono::steady_clock::now() + frameDuration;

    auto lastFpsTime = std::chrono::steady_clock::now();
    size_t fpsFrameCount = 0;
    size_t droppedFrames  = 0;
    size_t dupFrames      = 0;

    while (m_running) {
        // Wait until the next frame deadline (or shutdown).
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait_until(lock, nextFrameTime, [this] { return !m_running; });
            if (!m_running) break;

            if (m_frameReady) {
                // New frame from the game – grab it.
                std::memcpy(localBuffer.data(), m_frameBuffer.data(), localBuffer.size());
                m_frameReady = false;
                hasFrame = true;
            } else if (hasFrame) {
                // No new frame arrived – re-encode the last frame to keep
                // the stream alive at a constant frame rate (duplicate).
                dupFrames++;
            }
        }

        // Advance the deadline. If encoding was slow and we fell behind,
        // snap forward to avoid a burst of catch-up frames.
        nextFrameTime += frameDuration;
        auto now = std::chrono::steady_clock::now();
        if (nextFrameTime < now) {
            auto behind = now - nextFrameTime;
            auto skipped = behind / frameDuration;
            nextFrameTime = now + frameDuration;
            droppedFrames += static_cast<size_t>(skipped);
        }

        if (!hasFrame) continue; // Nothing to encode yet

        // Convert RGBA → YUV420P (outside mutex).
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

        // Calculate FPS every second
        now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration<double>(now - lastFpsTime).count();
        if (elapsed >= 1.0) {
            m_currentFps = static_cast<float>(fpsFrameCount / elapsed);
            if (droppedFrames > 0 || dupFrames > 0) {
                spdlog::debug("[StreamEncoder] {:.1f} fps | {} dup | {} skip in {:.1f}s",
                              m_currentFps.load(), dupFrames, droppedFrames, elapsed);
            }
            fpsFrameCount = 0;
            droppedFrames = 0;
            dupFrames     = 0;
            lastFpsTime   = now;
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

void StreamEncoder::audioThread() {
    spdlog::info("[StreamEncoder] Audio thread started – waiting for FFmpeg to connect…");

    // Wait for FFmpeg to open the named pipe (blocks)
    if (!openAudioPipeForWriting()) {
        spdlog::error("[StreamEncoder] Audio pipe connection failed – no audio in stream.");
        return;
    }

    spdlog::info("[StreamEncoder] Audio thread running.");

    // Write audio in 20 ms chunks at real-time pace
    const int chunkFrames  = m_audioSampleRate / 50;  // 882 frames @ 44100 Hz
    const int chunkSamples = chunkFrames * 2;          // stereo
    const size_t chunkBytes = chunkSamples * sizeof(sf::Int16);
    std::vector<sf::Int16> buffer(chunkSamples);

    auto nextWriteTime = std::chrono::steady_clock::now();
    const auto chunkDuration = std::chrono::microseconds(20000); // 20 ms

    while (m_running) {
        // Pull mixed audio from the AudioMixer
        m_audioMixer->pullSamples(buffer.data(), chunkFrames);

        // Write raw PCM to the audio pipe
#ifdef _WIN32
        DWORD written = 0;
        BOOL ok = WriteFile(m_audioPipeHandle, buffer.data(),
                            static_cast<DWORD>(chunkBytes), &written, nullptr);
        if (!ok || written != chunkBytes) {
            if (m_running) {
                spdlog::warn("[StreamEncoder] Audio pipe write failed (err={}). "
                             "FFmpeg may have exited.", GetLastError());
            }
            break;
        }
#else
        size_t written = fwrite(buffer.data(), 1, chunkBytes, m_audioPipeFile);
        if (written != chunkBytes) {
            if (m_running) {
                spdlog::warn("[StreamEncoder] Audio pipe write failed ({}/{}). "
                             "FFmpeg may have exited.", written, chunkBytes);
            }
            break;
        }
#endif

        // Pace to real-time
        nextWriteTime += chunkDuration;
        std::this_thread::sleep_until(nextWriteTime);
    }

    spdlog::info("[StreamEncoder] Audio thread exiting.");
}

} // namespace is::streaming
