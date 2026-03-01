#include "core/PerfMonitor.h"

#include <spdlog/spdlog.h>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif

namespace is::core {

PerfMonitor::PerfMonitor() = default;

void PerfMonitor::recordSample(double fps, double frameTimeMs,
                                int activeStreams, int activeChannels,
                                int totalPlayers) {
    m_framesSinceSample++;
    if (m_framesSinceSample < SAMPLE_INTERVAL_FRAMES) return;
    m_framesSinceSample = 0;

    std::lock_guard<std::mutex> lock(m_mutex);

    PerfSample sample;
    sample.timestamp = std::chrono::duration<double>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    sample.fps = fps;
    sample.frameTimeMs = frameTimeMs;
    sample.cpuPercent = getCpuUsage();
    sample.memoryMB = getMemoryUsageMB();
    sample.activeStreams = activeStreams;
    sample.activeChannels = activeChannels;
    sample.totalPlayers = totalPlayers;

    m_samples.push_back(sample);
    while (m_samples.size() > MAX_SAMPLES) {
        m_samples.pop_front();
    }
}

std::vector<PerfSample> PerfMonitor::getSamples(int seconds) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    double cutoff = std::chrono::duration<double>(
        std::chrono::system_clock::now().time_since_epoch()).count() - seconds;

    std::vector<PerfSample> result;
    for (const auto& s : m_samples) {
        if (s.timestamp >= cutoff) {
            result.push_back(s);
        }
    }
    return result;
}

PerfSample PerfMonitor::getLatest() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_samples.empty()) return {};
    return m_samples.back();
}

nlohmann::json PerfMonitor::toJson(int seconds) const {
    auto samples = getSamples(seconds);

    nlohmann::json arr = nlohmann::json::array();
    // Thin out to ~120 data points max for the frontend chart
    int step = std::max(1, static_cast<int>(samples.size()) / 120);
    for (size_t i = 0; i < samples.size(); i += step) {
        const auto& s = samples[i];
        arr.push_back({
            {"time", s.timestamp},
            {"fps", static_cast<int>(s.fps)},
            {"frameTimeMs", static_cast<int>(s.frameTimeMs * 10) / 10.0},
            {"cpuPercent", s.cpuPercent},
            {"memoryMB", s.memoryMB},
            {"activeStreams", s.activeStreams},
            {"activeChannels", s.activeChannels},
            {"totalPlayers", s.totalPlayers}
        });
    }

    return {"samples", arr};
}

nlohmann::json PerfMonitor::getAverages(int seconds) const {
    auto samples = getSamples(seconds);
    if (samples.empty()) {
        return {{"fps", 0}, {"frameTimeMs", 0}, {"memoryMB", 0},
                {"cpuPercent", 0}, {"sampleCount", 0}};
    }

    double avgFps = 0, avgFt = 0, avgCpu = 0;
    size_t avgMem = 0;
    for (const auto& s : samples) {
        avgFps += s.fps;
        avgFt += s.frameTimeMs;
        avgCpu += s.cpuPercent;
        avgMem += s.memoryMB;
    }
    double n = static_cast<double>(samples.size());

    double peakMem = 0;
    for (const auto& s : samples) {
        if (s.memoryMB > peakMem) peakMem = static_cast<double>(s.memoryMB);
    }

    return {
        {"avgFps", static_cast<int>(avgFps / n)},
        {"avgFrameTime", static_cast<int>(avgFt / n * 10) / 10.0},
        {"avgMemory", avgMem / samples.size()},
        {"peakMemory", static_cast<int>(peakMem)},
        {"cpuPercent", static_cast<int>(avgCpu / n)},
        {"activeStreams", samples.back().activeStreams},
        {"activeChannels", samples.back().activeChannels},
        {"totalPlayers", samples.back().totalPlayers},
        {"sampleCount", samples.size()}
    };
}

size_t PerfMonitor::getMemoryUsageMB() const {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return pmc.WorkingSetSize / (1024 * 1024);
    }
#endif
    return 0;
}

double PerfMonitor::getCpuUsage() const {
    // Simplified: return 0 for now.
    // Full implementation would track kernel/user time deltas.
    return 0.0;
}

} // namespace is::core
