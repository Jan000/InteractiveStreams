#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <chrono>
#include <deque>
#include <nlohmann/json.hpp>

namespace is::core {

/// A single performance sample.
struct PerfSample {
    double timestamp = 0.0;    ///< epoch seconds
    double fps = 0.0;          ///< frames per second
    double frameTimeMs = 0.0;  ///< frame time in milliseconds
    double cpuPercent = 0.0;   ///< estimated CPU usage (0-100)
    size_t memoryMB = 0;       ///< resident memory in MB
    int activeStreams = 0;     ///< number of active streams
    int activeChannels = 0;    ///< number of connected channels
    int totalPlayers = 0;      ///< total players across all games
};

/// Collects and stores performance metrics in a ring buffer.
/// Thread-safe: all methods lock an internal mutex.
class PerfMonitor {
public:
    PerfMonitor();
    ~PerfMonitor() = default;

    /// Record a new sample. Called once per frame from the main loop.
    void recordSample(double fps, double frameTimeMs,
                      int activeStreams, int activeChannels, int totalPlayers);

    /// Get all samples within the last `seconds` seconds.
    std::vector<PerfSample> getSamples(int seconds = 300) const;

    /// Get the latest sample.
    PerfSample getLatest() const;

    /// Get a JSON summary of recent performance.
    nlohmann::json toJson(int seconds = 300) const;

    /// Get average metrics for the last N seconds.
    nlohmann::json getAverages(int seconds = 60) const;

private:
    size_t getMemoryUsageMB() const;
    double getCpuUsage() const;

    mutable std::mutex m_mutex;
    std::deque<PerfSample> m_samples;

    // Track frame counts for FPS smoothing
    int m_frameCount = 0;
    double m_fpsAccumulator = 0.0;

    static constexpr size_t MAX_SAMPLES = 3600; // ~5 min at 12 samples/sec (sampled every ~5 frames)
    static constexpr int SAMPLE_INTERVAL_FRAMES = 5; // sample every N frames

    int m_framesSinceSample = 0;
};

} // namespace is::core
