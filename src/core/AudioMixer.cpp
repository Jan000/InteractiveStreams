#include "core/AudioMixer.h"
#include <spdlog/spdlog.h>
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <cmath>

namespace fs = std::filesystem;

namespace is::core {

// Supported music file extensions (same as AudioManager)
static const std::vector<std::string> MIXER_MUSIC_EXT = {
    ".mp3", ".ogg", ".wav", ".flac"
};

static bool isMixerMusicFile(const std::string& ext) {
    std::string lower = ext;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    for (const auto& e : MIXER_MUSIC_EXT) {
        if (lower == e) return true;
    }
    return false;
}

AudioMixer::AudioMixer()
    : m_rng(std::random_device{}())
{
    // Pre-allocate a decent read buffer (enough for ~50ms of stereo audio)
    m_musicReadBuf.resize(SAMPLE_RATE / 20 * CHANNELS);
}

AudioMixer::~AudioMixer() = default;

// ── Music control ────────────────────────────────────────────────────────────

void AudioMixer::scanMusicDirectory(const std::string& directory) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_musicDirectory = directory;
    m_allFiles.clear();

    if (!fs::exists(directory) || !fs::is_directory(directory)) {
        spdlog::warn("[AudioMixer] Music directory '{}' does not exist.", directory);
        return;
    }

    for (const auto& entry : fs::directory_iterator(directory)) {
        if (!entry.is_regular_file()) continue;
        auto ext = entry.path().extension().string();
        if (isMixerMusicFile(ext)) {
            m_allFiles.push_back(entry.path().string());
        }
    }

    std::sort(m_allFiles.begin(), m_allFiles.end());
    spdlog::info("[AudioMixer] Found {} music file(s) in '{}'.", m_allFiles.size(), directory);
}

void AudioMixer::rescan() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string dir = m_musicDirectory;
    m_allFiles.clear();

    if (dir.empty() || !fs::exists(dir)) return;

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        auto ext = entry.path().extension().string();
        if (isMixerMusicFile(ext)) {
            m_allFiles.push_back(entry.path().string());
        }
    }
    std::sort(m_allFiles.begin(), m_allFiles.end());
    spdlog::info("[AudioMixer] Rescan: {} music file(s).", m_allFiles.size());
}

void AudioMixer::shufflePlaylist() {
    // caller must hold m_mutex
    m_playlist = m_allFiles;
    std::shuffle(m_playlist.begin(), m_playlist.end(), m_rng);
}

bool AudioMixer::openNextTrack() {
    // caller must hold m_mutex
    if (m_playlist.empty()) return false;

    // Advance index; if at end, re-shuffle and start from 0
    m_currentIndex++;
    if (m_currentIndex >= static_cast<int>(m_playlist.size())) {
        shufflePlaylist();
        m_currentIndex = 0;
    }

    const auto& path = m_playlist[m_currentIndex];
    if (!m_musicFile.openFromFile(path)) {
        spdlog::warn("[AudioMixer] Failed to open: {}", path);
        m_musicFileOpen = false;
        // Try the next track (avoid infinite recursion by limiting retries)
        if (m_playlist.size() > 1) {
            return openNextTrack();
        }
        return false;
    }

    m_musicFileOpen    = true;
    m_musicChannels    = m_musicFile.getChannelCount();
    m_musicSampleRate  = m_musicFile.getSampleRate();
    m_musicTotalSamples = m_musicFile.getSampleCount();

    auto filename = fs::path(path).filename().string();
    spdlog::info("[AudioMixer] Now mixing: {} ({}Hz, {}ch)",
                 filename, m_musicSampleRate, m_musicChannels);
    return true;
}

void AudioMixer::play() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_allFiles.empty()) {
        spdlog::warn("[AudioMixer] No music files – nothing to play.");
        return;
    }
    shufflePlaylist();
    m_currentIndex = -1; // openNextTrack will advance to 0
    m_playing = true;
    m_paused  = false;
    openNextTrack();
}

void AudioMixer::pause() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_paused = true;
}

void AudioMixer::resume() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_paused = false;
}

void AudioMixer::nextTrack() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_playing) return;
    openNextTrack();
}

void AudioMixer::setMusicVolume(float volume) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_musicVolume = std::clamp(volume, 0.0f, 100.0f);
}

void AudioMixer::setSfxVolume(float volume) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_sfxVolume = std::clamp(volume, 0.0f, 100.0f);
}

void AudioMixer::setMuted(bool muted) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_muted = muted;
}

// ── SFX ──────────────────────────────────────────────────────────────────────

void AudioMixer::playSfx(const sf::SoundBuffer& buffer) {
    std::lock_guard<std::mutex> lock(m_mutex);

    SfxInstance sfx;
    sfx.channels   = buffer.getChannelCount();
    sfx.sampleRate = buffer.getSampleRate();
    sfx.position   = 0;

    // Copy sample data from the buffer
    const sf::Int16* ptr = buffer.getSamples();
    sf::Uint64 count = buffer.getSampleCount();
    sfx.samples.assign(ptr, ptr + count);

    m_activeSfx.push_back(std::move(sfx));
}

// ── PCM output ───────────────────────────────────────────────────────────────

size_t AudioMixer::pullSamples(sf::Int16* output, size_t frameCount) {
    std::lock_guard<std::mutex> lock(m_mutex);

    const size_t totalSamples = frameCount * CHANNELS; // stereo interleaved
    std::memset(output, 0, totalSamples * sizeof(sf::Int16));

    if (m_muted) return frameCount;

    // ── Mix music ────────────────────────────────────────────────────────
    if (m_playing && !m_paused && m_musicFileOpen) {
        const float musicGain = m_musicVolume / 100.0f;
        size_t framesRemaining = frameCount;
        sf::Int16* dst = output;

        while (framesRemaining > 0) {
            // How many source frames to read from the file?
            // If sample rates differ, we need to adjust the read count.
            size_t readFrames = framesRemaining;
            if (m_musicSampleRate != SAMPLE_RATE) {
                // Simple ratio-based read count
                readFrames = static_cast<size_t>(
                    std::ceil(static_cast<double>(framesRemaining) *
                              m_musicSampleRate / SAMPLE_RATE));
            }

            size_t readSamples = readFrames * m_musicChannels;
            if (m_musicReadBuf.size() < readSamples) {
                m_musicReadBuf.resize(readSamples);
            }

            sf::Uint64 got = m_musicFile.read(m_musicReadBuf.data(),
                                              static_cast<sf::Uint64>(readSamples));
            if (got == 0) {
                // Track ended – open next
                if (!openNextTrack()) {
                    break; // no more tracks
                }
                continue; // retry with new track
            }

            size_t gotFrames = static_cast<size_t>(got) / m_musicChannels;

            // Convert to stereo 44100Hz and mix into output
            for (size_t i = 0; i < gotFrames && framesRemaining > 0; ++i) {
                // Simple nearest-neighbor resampling
                if (m_musicSampleRate != SAMPLE_RATE) {
                    double srcPos = static_cast<double>(i) * SAMPLE_RATE / m_musicSampleRate;
                    size_t outIdx = static_cast<size_t>(srcPos);
                    if (outIdx >= framesRemaining) break;
                    // Get source sample
                    int left, right;
                    if (m_musicChannels == 1) {
                        left = right = m_musicReadBuf[i];
                    } else {
                        left  = m_musicReadBuf[i * 2];
                        right = m_musicReadBuf[i * 2 + 1];
                    }
                    // Apply gain and mix (additive)
                    int l = dst[outIdx * 2]     + static_cast<int>(left  * musicGain);
                    int r = dst[outIdx * 2 + 1] + static_cast<int>(right * musicGain);
                    dst[outIdx * 2]     = static_cast<sf::Int16>(std::clamp(l, -32768, 32767));
                    dst[outIdx * 2 + 1] = static_cast<sf::Int16>(std::clamp(r, -32768, 32767));
                } else {
                    // Same sample rate – direct copy with gain
                    int left, right;
                    if (m_musicChannels == 1) {
                        left = right = m_musicReadBuf[i];
                    } else {
                        left  = m_musicReadBuf[i * 2];
                        right = m_musicReadBuf[i * 2 + 1];
                    }
                    int l = dst[0] + static_cast<int>(left  * musicGain);
                    int r = dst[1] + static_cast<int>(right * musicGain);
                    dst[0] = static_cast<sf::Int16>(std::clamp(l, -32768, 32767));
                    dst[1] = static_cast<sf::Int16>(std::clamp(r, -32768, 32767));
                    dst += 2;
                    framesRemaining--;
                }
            }

            // If resampling, account for output frames we've consumed
            if (m_musicSampleRate != SAMPLE_RATE) {
                size_t outputFrames = static_cast<size_t>(
                    static_cast<double>(gotFrames) * SAMPLE_RATE / m_musicSampleRate);
                if (outputFrames > framesRemaining) outputFrames = framesRemaining;
                dst += outputFrames * 2;
                framesRemaining -= outputFrames;
            }
        }
    }

    // ── Mix SFX ──────────────────────────────────────────────────────────
    {
        const float sfxGain = m_sfxVolume / 100.0f;

        for (auto it = m_activeSfx.begin(); it != m_activeSfx.end(); ) {
            auto& sfx = *it;
            size_t sfxTotalSamples = sfx.samples.size();
            bool done = false;

            for (size_t i = 0; i < frameCount; ++i) {
                if (sfx.position >= sfxTotalSamples) {
                    done = true;
                    break;
                }

                int left, right;
                if (sfx.channels == 1) {
                    left = right = sfx.samples[sfx.position];
                    sfx.position += 1;
                } else {
                    left  = sfx.samples[sfx.position];
                    right = (sfx.position + 1 < sfxTotalSamples)
                            ? sfx.samples[sfx.position + 1] : left;
                    sfx.position += 2;
                }

                // Simple resampling: if sfx sample rate differs, skip/repeat
                // For simplicity, we assume SFX are at 44100Hz (most game SFX are).
                // A proper resampler could be added later.

                int l = output[i * 2]     + static_cast<int>(left  * sfxGain);
                int r = output[i * 2 + 1] + static_cast<int>(right * sfxGain);
                output[i * 2]     = static_cast<sf::Int16>(std::clamp(l, -32768, 32767));
                output[i * 2 + 1] = static_cast<sf::Int16>(std::clamp(r, -32768, 32767));
            }

            if (done) {
                it = m_activeSfx.erase(it);
            } else {
                ++it;
            }
        }
    }

    return frameCount;
}

} // namespace is::core
