#include "core/AudioMixer.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace is::core {

namespace {

size_t mixFromFile(sf::InputSoundFile& file,
                   unsigned int fileChannels,
                   unsigned int fileSampleRate,
                   std::vector<sf::Int16>& readBuf,
                   float gain,
                   sf::Int16* output,
                   size_t frameCount) {
    if (fileChannels == 0 || fileSampleRate == 0 || frameCount == 0 || gain <= 0.0f) {
        return 0;
    }

    size_t readFrames = frameCount;
    if (fileSampleRate != AudioMixer::SAMPLE_RATE) {
        readFrames = static_cast<size_t>(
            std::ceil(static_cast<double>(frameCount) * fileSampleRate / AudioMixer::SAMPLE_RATE)) + 1;
    }

    const size_t readSamples = readFrames * fileChannels;
    if (readBuf.size() < readSamples) {
        readBuf.resize(readSamples);
    }

    const sf::Uint64 gotSamples = file.read(readBuf.data(), static_cast<sf::Uint64>(readSamples));
    if (gotSamples == 0) {
        return 0;
    }

    const size_t gotFrames = static_cast<size_t>(gotSamples) / fileChannels;
    size_t mixedFrames = 0;

    if (fileSampleRate == AudioMixer::SAMPLE_RATE) {
        mixedFrames = std::min(frameCount, gotFrames);
        for (size_t i = 0; i < mixedFrames; ++i) {
            int left = 0;
            int right = 0;
            if (fileChannels == 1) {
                left = right = readBuf[i];
            } else {
                left = readBuf[i * 2];
                right = readBuf[i * 2 + 1];
            }

            const int l = output[i * 2] + static_cast<int>(left * gain);
            const int r = output[i * 2 + 1] + static_cast<int>(right * gain);
            output[i * 2] = static_cast<sf::Int16>(std::clamp(l, -32768, 32767));
            output[i * 2 + 1] = static_cast<sf::Int16>(std::clamp(r, -32768, 32767));
        }
        return mixedFrames;
    }

    for (size_t out = 0; out < frameCount; ++out) {
        const size_t src = static_cast<size_t>(
            static_cast<double>(out) * fileSampleRate / AudioMixer::SAMPLE_RATE);
        if (src >= gotFrames) {
            break;
        }

        int left = 0;
        int right = 0;
        if (fileChannels == 1) {
            left = right = readBuf[src];
        } else {
            left = readBuf[src * 2];
            right = readBuf[src * 2 + 1];
        }

        const int l = output[out * 2] + static_cast<int>(left * gain);
        const int r = output[out * 2 + 1] + static_cast<int>(right * gain);
        output[out * 2] = static_cast<sf::Int16>(std::clamp(l, -32768, 32767));
        output[out * 2 + 1] = static_cast<sf::Int16>(std::clamp(r, -32768, 32767));
        mixedFrames = out + 1;
    }

    return mixedFrames;
}

} // namespace

AudioMixer::AudioMixer()
    : m_rng(std::random_device{}()) {
    m_musicReadBuf.resize(SAMPLE_RATE / 20 * CHANNELS);
    m_musicNextReadBuf.resize(SAMPLE_RATE / 20 * CHANNELS);
}

AudioMixer::~AudioMixer() = default;

bool AudioMixer::playTrack(const std::string& filepath) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto file = std::make_unique<sf::InputSoundFile>();
    if (!file->openFromFile(filepath)) {
        spdlog::warn("[AudioMixer] Failed to open track: {}", filepath);
        return false;
    }

    m_musicChannels = file->getChannelCount();
    m_musicSampleRate = file->getSampleRate();
    m_musicFile = std::move(file);

    m_musicNext.reset();
    m_musicNextChannels = 0;
    m_musicNextSampleRate = 0;
    m_isCrossfading = false;
    m_crossfadeTimer = 0.0f;
    m_playing = true;
    m_paused = false;

    spdlog::info("[AudioMixer] playTrack: {} ({}Hz, {}ch)", filepath, m_musicSampleRate, m_musicChannels);
    return true;
}

bool AudioMixer::crossfadeToTrack(const std::string& filepath) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto next = std::make_unique<sf::InputSoundFile>();
    if (!next->openFromFile(filepath)) {
        spdlog::warn("[AudioMixer] Failed to open crossfade target: {}", filepath);
        return false;
    }

    if (!m_musicFile) {
        m_musicChannels = next->getChannelCount();
        m_musicSampleRate = next->getSampleRate();
        m_musicFile = std::move(next);
        m_playing = true;
        m_paused = false;
        return true;
    }

    m_musicNextChannels = next->getChannelCount();
    m_musicNextSampleRate = next->getSampleRate();
    m_musicNext = std::move(next);

    m_isCrossfading = true;
    m_crossfadeTimer = 0.0f;
    m_playing = true;

    spdlog::info("[AudioMixer] crossfadeToTrack: {} ({}Hz, {}ch)",
        filepath, m_musicNextSampleRate, m_musicNextChannels);
    return true;
}

void AudioMixer::stop() {
    std::lock_guard<std::mutex> lock(m_mutex);

    m_musicFile.reset();
    m_musicNext.reset();
    m_musicChannels = 0;
    m_musicSampleRate = 0;
    m_musicNextChannels = 0;
    m_musicNextSampleRate = 0;
    m_isCrossfading = false;
    m_crossfadeTimer = 0.0f;
    m_playing = false;
    m_paused = false;
}

void AudioMixer::pause() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_paused = true;
}

void AudioMixer::resume() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_paused = false;
}

void AudioMixer::setMusicVolume(float volume) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_musicVolume = std::clamp(volume, 0.0f, 100.0f);
}

void AudioMixer::setFadeInDuration(float seconds) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_fadeInSeconds = std::max(0.0f, seconds);
}

void AudioMixer::setFadeOutDuration(float seconds) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_fadeOutSeconds = std::max(0.0f, seconds);
}

void AudioMixer::setCrossfadeOverlap(float seconds) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_crossfadeOverlap = std::max(0.0f, seconds);
}

void AudioMixer::setSfxVolume(float volume) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_sfxVolume = std::clamp(volume, 0.0f, 100.0f);
}

void AudioMixer::setMuted(bool muted) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_muted = muted;
}

void AudioMixer::playSfx(const sf::SoundBuffer& buffer, float volume) {
    std::lock_guard<std::mutex> lock(m_mutex);

    SfxInstance sfx;
    sfx.channels = buffer.getChannelCount();
    sfx.sampleRate = buffer.getSampleRate();
    sfx.position = 0;
    sfx.volume = std::clamp(volume, 0.0f, 100.0f);
    sfx.samples = buffer.getSamples();
    sfx.totalSamples = static_cast<size_t>(buffer.getSampleCount());

    m_activeSfx.push_back(std::move(sfx));
}

size_t AudioMixer::pullSamples(sf::Int16* output, size_t frameCount) {
    std::lock_guard<std::mutex> lock(m_mutex);

    const size_t totalSamples = frameCount * CHANNELS;
    std::memset(output, 0, totalSamples * sizeof(sf::Int16));

    if (m_muted || !m_playing || m_paused) {
        return frameCount;
    }

    if (m_musicFile) {
        const float musicGain = m_musicVolume / 100.0f;

        if (m_isCrossfading && m_musicNext) {
            const float crossfadeDur = std::max({m_crossfadeOverlap, m_fadeOutSeconds, m_fadeInSeconds, 0.01f});
            float progress = std::clamp(m_crossfadeTimer / crossfadeDur, 0.0f, 1.0f);

            const float gainCurrent = musicGain * (1.0f - progress);
            const float gainNext = musicGain * progress;

            const size_t mixedCurrent = mixFromFile(
                *m_musicFile, m_musicChannels, m_musicSampleRate, m_musicReadBuf,
                gainCurrent, output, frameCount);
            const size_t mixedNext = mixFromFile(
                *m_musicNext, m_musicNextChannels, m_musicNextSampleRate, m_musicNextReadBuf,
                gainNext, output, frameCount);

            const bool currentEnded = (mixedCurrent == 0);
            const bool nextEnded = (mixedNext == 0);

            if (nextEnded) {
                m_musicNext.reset();
                m_musicNextChannels = 0;
                m_musicNextSampleRate = 0;
                m_isCrossfading = false;
                m_crossfadeTimer = 0.0f;
            }

            if (currentEnded) {
                if (m_musicNext) {
                    m_musicFile = std::move(m_musicNext);
                    m_musicChannels = m_musicNextChannels;
                    m_musicSampleRate = m_musicNextSampleRate;
                    m_musicNextChannels = 0;
                    m_musicNextSampleRate = 0;
                    m_isCrossfading = false;
                    m_crossfadeTimer = 0.0f;
                } else {
                    m_musicFile.reset();
                    m_musicChannels = 0;
                    m_musicSampleRate = 0;
                    m_playing = false;
                }
            } else if (m_isCrossfading && m_musicNext) {
                m_crossfadeTimer += static_cast<float>(frameCount) / static_cast<float>(SAMPLE_RATE);
                progress = std::clamp(m_crossfadeTimer / crossfadeDur, 0.0f, 1.0f);
                if (progress >= 1.0f) {
                    m_musicFile = std::move(m_musicNext);
                    m_musicChannels = m_musicNextChannels;
                    m_musicSampleRate = m_musicNextSampleRate;
                    m_musicNextChannels = 0;
                    m_musicNextSampleRate = 0;
                    m_isCrossfading = false;
                    m_crossfadeTimer = 0.0f;
                }
            }
        } else {
            const size_t mixed = mixFromFile(
                *m_musicFile, m_musicChannels, m_musicSampleRate, m_musicReadBuf,
                musicGain, output, frameCount);
            if (mixed == 0) {
                m_musicFile.reset();
                m_musicChannels = 0;
                m_musicSampleRate = 0;
                m_playing = false;
            }
        }
    }

    const float sfxGain = m_sfxVolume / 100.0f;
    for (auto it = m_activeSfx.begin(); it != m_activeSfx.end(); ) {
        auto& sfx = *it;

        if (!sfx.samples || sfx.totalSamples == 0) {
            it = m_activeSfx.erase(it);
            continue;
        }

        bool done = false;
        for (size_t i = 0; i < frameCount; ++i) {
            if (sfx.position >= sfx.totalSamples) {
                done = true;
                break;
            }

            int left = 0;
            int right = 0;
            if (sfx.channels == 1) {
                left = right = sfx.samples[sfx.position];
                sfx.position += 1;
            } else {
                left = sfx.samples[sfx.position];
                right = (sfx.position + 1 < sfx.totalSamples)
                    ? sfx.samples[sfx.position + 1]
                    : left;
                sfx.position += 2;
            }

            // Global SFX volume is applied exactly once here; per-sound volume
            // is provided by the caller (AudioManager).
            const float gain = sfxGain * (sfx.volume / 100.0f);
            const int l = output[i * 2] + static_cast<int>(left * gain);
            const int r = output[i * 2 + 1] + static_cast<int>(right * gain);
            output[i * 2] = static_cast<sf::Int16>(std::clamp(l, -32768, 32767));
            output[i * 2 + 1] = static_cast<sf::Int16>(std::clamp(r, -32768, 32767));
        }

        if (done) {
            it = m_activeSfx.erase(it);
        } else {
            ++it;
        }
    }

    return frameCount;
}

} // namespace is::core
