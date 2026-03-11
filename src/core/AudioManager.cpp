#include "core/AudioManager.h"
#include "core/AudioMixer.h"
#include <spdlog/spdlog.h>
#include <filesystem>
#include <algorithm>
#include <cmath>

namespace fs = std::filesystem;

namespace is::core {

// Supported music file extensions
static const std::vector<std::string> MUSIC_EXTENSIONS = {
    ".mp3", ".ogg", ".wav", ".flac"
};

static bool isMusicFile(const std::string& ext) {
    std::string lower = ext;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    for (const auto& e : MUSIC_EXTENSIONS) {
        if (lower == e) return true;
    }
    return false;
}

AudioManager::AudioManager()
    : m_rng(std::random_device{}())
{
}

AudioManager::~AudioManager() {
    stopMusic();
}

// ── Music ────────────────────────────────────────────────────────────────────

void AudioManager::scanMusicDirectory(const std::string& directory) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_musicDirectory = directory;
    m_allFiles.clear();

    if (!fs::exists(directory) || !fs::is_directory(directory)) {
        spdlog::warn("[Audio] Music directory '{}' does not exist.", directory);
        return;
    }

    for (const auto& entry : fs::directory_iterator(directory)) {
        if (!entry.is_regular_file()) continue;
        auto ext = entry.path().extension().string();
        if (isMusicFile(ext)) {
            m_allFiles.push_back(entry.path().string());
        }
    }

    std::sort(m_allFiles.begin(), m_allFiles.end());
    spdlog::info("[Audio] Found {} music file(s) in '{}'.", m_allFiles.size(), directory);
}

void AudioManager::rescan() {
    bool wasPlaying = isMusicPlaying();
    std::string currentFile;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_currentIndex >= 0 && m_currentIndex < static_cast<int>(m_playlist.size())) {
            currentFile = m_playlist[m_currentIndex];
        }
    }

    scanMusicDirectory(m_musicDirectory);

    if (wasPlaying) {
        std::lock_guard<std::mutex> lock(m_mutex);
        shufflePlaylist();
        // Try to find the currently playing track in the new playlist
        for (int i = 0; i < static_cast<int>(m_playlist.size()); ++i) {
            if (m_playlist[i] == currentFile) {
                m_currentIndex = i;
                return;
            }
        }
        // If current track was removed, just keep playing (it'll advance on end)
    }
}

void AudioManager::shufflePlaylist() {
    // caller must hold m_mutex
    m_playlist = m_allFiles;
    std::shuffle(m_playlist.begin(), m_playlist.end(), m_rng);
}

bool AudioManager::loadTrackInto(sf::Music& music, int index) {
    // caller must hold m_mutex
    if (index < 0 || index >= static_cast<int>(m_playlist.size())) return false;

    const auto& path = m_playlist[index];
    if (!music.openFromFile(path)) {
        spdlog::error("[Audio] Failed to open music file: {}", path);
        return false;
    }

    music.setLoop(false);
    auto filename = fs::path(path).filename().string();
    spdlog::info("[Audio] Loaded track: {} ({}/{})", filename, index + 1, m_playlist.size());
    return true;
}

int AudioManager::nextTrackIndex() const {
    // caller must hold m_mutex
    if (m_playlist.empty()) return -1;
    int next = m_currentIndex + 1;
    if (next >= static_cast<int>(m_playlist.size())) {
        return -1; // signal re-shuffle needed
    }
    return next;
}

void AudioManager::playMusic() {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_allFiles.empty()) {
        spdlog::warn("[Audio] No music files loaded – nothing to play.");
        return;
    }

    shufflePlaylist();
    m_currentIndex = 0;

    m_musicCurrent = std::make_unique<sf::Music>();
    if (!loadTrackInto(*m_musicCurrent, 0)) return;

    // Start with fade-in if configured
    if (m_fadeInSeconds > 0.01f) {
        m_musicCurrent->setVolume(0.0f);
        m_fadeState = FadeState::FadingIn;
        m_fadeTimer = 0.0f;
    } else {
        m_musicCurrent->setVolume(m_muted ? 0.0f : m_musicVolume);
        m_fadeState = FadeState::None;
    }

    m_musicCurrent->play();
    m_musicPaused = false;

    spdlog::info("[Audio] Now playing: {} ({}/{})",
        fs::path(m_playlist[0]).filename().string(), 1, m_playlist.size());
}

void AudioManager::pauseMusic() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_musicCurrent && m_musicCurrent->getStatus() == sf::Music::Playing) {
        m_musicCurrent->pause();
        m_musicPaused = true;
    }
    // Also pause the next track if crossfading
    if (m_musicNext && m_musicNext->getStatus() == sf::Music::Playing) {
        m_musicNext->pause();
    }
}

void AudioManager::resumeMusic() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_musicPaused) {
        if (m_musicCurrent && m_musicCurrent->getStatus() == sf::Music::Paused) {
            m_musicCurrent->play();
        }
        if (m_musicNext && m_musicNext->getStatus() == sf::Music::Paused) {
            m_musicNext->play();
        }
        m_musicPaused = false;
    }
}

void AudioManager::stopMusic() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_musicCurrent) m_musicCurrent->stop();
    if (m_musicNext)    m_musicNext->stop();
    m_musicCurrent.reset();
    m_musicNext.reset();
    m_currentIndex = -1;
    m_nextIndex    = -1;
    m_musicPaused  = false;
    m_fadeState     = FadeState::None;
    m_fadeTimer     = 0.0f;
}

void AudioManager::nextTrack() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_playlist.empty()) return;

    // If already crossfading, finish immediately
    if (m_fadeState == FadeState::Crossfading) {
        finishCrossfade();
    }

    beginCrossfade();
}

void AudioManager::beginCrossfade() {
    // caller must hold m_mutex
    int next = nextTrackIndex();
    if (next < 0) {
        // Re-shuffle and restart from beginning
        shufflePlaylist();
        next = 0;
    }

    m_nextIndex = next;
    m_musicNext = std::make_unique<sf::Music>();
    if (!loadTrackInto(*m_musicNext, next)) {
        m_musicNext.reset();
        m_nextIndex = -1;
        return;
    }

    // Start fade-in of new track at 0 volume
    m_musicNext->setVolume(0.0f);
    m_musicNext->play();

    m_fadeState = FadeState::Crossfading;
    m_fadeTimer = 0.0f;

    spdlog::info("[Audio] Crossfade started → {} ({}/{})",
        fs::path(m_playlist[next]).filename().string(), next + 1, m_playlist.size());
}

void AudioManager::finishCrossfade() {
    // caller must hold m_mutex
    if (m_musicCurrent) {
        m_musicCurrent->stop();
    }

    // Swap next → current
    m_musicCurrent = std::move(m_musicNext);
    m_musicNext.reset();
    m_currentIndex = m_nextIndex;
    m_nextIndex    = -1;

    // Set to full volume
    if (m_musicCurrent) {
        m_musicCurrent->setVolume(m_muted ? 0.0f : m_musicVolume);
    }

    m_fadeState = FadeState::None;
    m_fadeTimer = 0.0f;
}

void AudioManager::update(double dt) {
    std::lock_guard<std::mutex> lock(m_mutex);

    float fdt = static_cast<float>(dt);

    // ── Handle fade states ───────────────────────────────────────────────
    if (m_fadeState == FadeState::FadingIn && m_musicCurrent) {
        m_fadeTimer += fdt;
        float progress = (m_fadeInSeconds > 0.01f)
            ? std::min(m_fadeTimer / m_fadeInSeconds, 1.0f) : 1.0f;
        float vol = m_muted ? 0.0f : (m_musicVolume * progress);
        m_musicCurrent->setVolume(vol);

        if (progress >= 1.0f) {
            m_fadeState = FadeState::None;
            m_fadeTimer = 0.0f;
        }
    }
    else if (m_fadeState == FadeState::Crossfading) {
        m_fadeTimer += fdt;

        // Use a single progress curve for both tracks so the combined
        // volume never exceeds m_musicVolume (complementary crossfade).
        float crossfadeDur = std::max({m_fadeOutSeconds, m_fadeInSeconds, 0.01f});
        float progress = std::min(m_fadeTimer / crossfadeDur, 1.0f);

        // Fade out old track
        if (m_musicCurrent) {
            float vol = m_muted ? 0.0f : (m_musicVolume * (1.0f - progress));
            m_musicCurrent->setVolume(vol);
        }

        // Fade in new track
        if (m_musicNext) {
            float vol = m_muted ? 0.0f : (m_musicVolume * progress);
            m_musicNext->setVolume(vol);
        }

        // Crossfade complete
        if (progress >= 1.0f) {
            finishCrossfade();
        }
    }

    // ── Auto-advance: detect when current track is about to end ──────────
    if (m_fadeState == FadeState::None && m_musicCurrent && !m_musicPaused &&
        m_currentIndex >= 0) {

        auto status = m_musicCurrent->getStatus();

        if (status == sf::Music::Playing && m_crossfadeOverlap > 0.01f) {
            // Check remaining time
            sf::Time duration = m_musicCurrent->getDuration();
            sf::Time offset   = m_musicCurrent->getPlayingOffset();
            float remaining   = duration.asSeconds() - offset.asSeconds();

            // Start crossfade early enough for the fade-out to complete
            // before the track's audio data runs out.
            float triggerTime = std::max(m_crossfadeOverlap,
                                         std::max(m_fadeOutSeconds, m_fadeInSeconds));
            if (remaining <= triggerTime && remaining > 0.0f) {
                beginCrossfade();
            }
        }
        else if (status == sf::Music::Stopped) {
            // Track ended without crossfade (crossfadeOverlap == 0 or very short track)
            int next = nextTrackIndex();
            if (next < 0) {
                shufflePlaylist();
                next = 0;
            }
            m_currentIndex = next;
            m_musicCurrent = std::make_unique<sf::Music>();
            if (loadTrackInto(*m_musicCurrent, next)) {
                if (m_fadeInSeconds > 0.01f) {
                    m_musicCurrent->setVolume(0.0f);
                    m_fadeState = FadeState::FadingIn;
                    m_fadeTimer = 0.0f;
                } else {
                    m_musicCurrent->setVolume(m_muted ? 0.0f : m_musicVolume);
                }
                m_musicCurrent->play();

                spdlog::info("[Audio] Now playing: {} ({}/{})",
                    fs::path(m_playlist[next]).filename().string(),
                    next + 1, m_playlist.size());
            }
        }
    }

    // ── Clean up finished sounds ─────────────────────────────────────────
    m_activeSounds.erase(
        std::remove_if(m_activeSounds.begin(), m_activeSounds.end(),
            [](const std::unique_ptr<sf::Sound>& s) {
                return s->getStatus() == sf::Sound::Stopped;
            }),
        m_activeSounds.end());
}

void AudioManager::setMusicVolume(float volume) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_musicVolume = std::clamp(volume, 0.0f, 100.0f);
    if (!m_muted && m_fadeState == FadeState::None) {
        if (m_musicCurrent) m_musicCurrent->setVolume(m_musicVolume);
    }
    // During fading, the volume is managed by the fade logic
}

bool AudioManager::isMusicPlaying() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_musicCurrent && m_musicCurrent->getStatus() == sf::Music::Playing)
        return true;
    if (m_musicNext && m_musicNext->getStatus() == sf::Music::Playing)
        return true;
    return false;
}

std::string AudioManager::currentTrackName() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    // During crossfade, show the incoming track name
    int idx = (m_fadeState == FadeState::Crossfading && m_nextIndex >= 0)
        ? m_nextIndex : m_currentIndex;
    if (idx < 0 || idx >= static_cast<int>(m_playlist.size()))
        return "";
    return fs::path(m_playlist[idx]).stem().string();
}

int AudioManager::trackCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return static_cast<int>(m_allFiles.size());
}

// ── Crossfade settings ───────────────────────────────────────────────────────

void AudioManager::setFadeInDuration(float seconds) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_fadeInSeconds = std::max(seconds, 0.0f);
}

void AudioManager::setFadeOutDuration(float seconds) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_fadeOutSeconds = std::max(seconds, 0.0f);
}

void AudioManager::setCrossfadeOverlap(float seconds) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_crossfadeOverlap = std::max(seconds, 0.0f);
}

// ── SFX ──────────────────────────────────────────────────────────────────────

bool AudioManager::loadSfx(const std::string& name, const std::string& filepath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto buf = std::make_unique<sf::SoundBuffer>();
    if (!buf->loadFromFile(filepath)) {
        spdlog::error("[Audio] Failed to load SFX '{}' from '{}'", name, filepath);
        return false;
    }
    auto& entry = m_sfxBuffers[name];
    entry.buffers.clear();
    entry.buffers.push_back(std::move(buf));
    spdlog::info("[Audio] Loaded SFX: {}", name);
    return true;
}

bool AudioManager::loadSfxFromDirectory(const std::string& name, const std::string& dirpath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!fs::is_directory(dirpath)) {
        spdlog::warn("[Audio] SFX directory not found: {}", dirpath);
        return false;
    }
    static const std::vector<std::string> SFX_EXTENSIONS = {".wav", ".ogg", ".mp3"};
    auto& entry = m_sfxBuffers[name];
    entry.buffers.clear();
    for (const auto& file : fs::directory_iterator(dirpath)) {
        if (!file.is_regular_file()) continue;
        std::string ext = file.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (std::find(SFX_EXTENSIONS.begin(), SFX_EXTENSIONS.end(), ext) == SFX_EXTENSIONS.end()) continue;
        auto buf = std::make_unique<sf::SoundBuffer>();
        if (buf->loadFromFile(file.path().string())) {
            entry.buffers.push_back(std::move(buf));
        }
    }
    if (entry.buffers.empty()) {
        m_sfxBuffers.erase(name);
        spdlog::warn("[Audio] No audio files found for SFX '{}' in '{}'", name, dirpath);
        return false;
    }
    spdlog::info("[Audio] Loaded {} variant(s) for SFX '{}' from '{}'", entry.buffers.size(), name, dirpath);
    return true;
}

void AudioManager::playSfx(const std::string& name, float volume) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_sfxBuffers.find(name);
    if (it == m_sfxBuffers.end() || it->second.buffers.empty()) return;

    int idx = 0;
    if (it->second.buffers.size() > 1) {
        std::uniform_int_distribution<int> dist(0, static_cast<int>(it->second.buffers.size()) - 1);
        idx = dist(m_rng);
    }

    auto sound = std::make_unique<sf::Sound>();
    sound->setBuffer(*it->second.buffers[idx]);
    float effectiveVol = m_muted ? 0.0f : (volume * m_sfxVolume / 100.0f);
    sound->setVolume(effectiveVol);
    sound->play();
    m_activeSounds.push_back(std::move(sound));

    // Also route to AudioMixer for stream encoding
    if (m_audioMixer) {
        m_audioMixer->playSfx(*it->second.buffers[idx], effectiveVol);
    }

    // Limit active sounds to prevent resource exhaustion
    while (m_activeSounds.size() > 32) {
        m_activeSounds.erase(m_activeSounds.begin());
    }
}

void AudioManager::setAudioMixer(AudioMixer* mixer) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_audioMixer = mixer;
}

void AudioManager::setSfxVolume(float volume) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_sfxVolume = std::clamp(volume, 0.0f, 100.0f);
}

// ── Mute ─────────────────────────────────────────────────────────────────────

void AudioManager::setMuted(bool muted) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_muted = muted;
    if (muted) {
        if (m_musicCurrent) m_musicCurrent->setVolume(0.0f);
        if (m_musicNext)    m_musicNext->setVolume(0.0f);
    } else if (m_fadeState == FadeState::None) {
        if (m_musicCurrent) m_musicCurrent->setVolume(m_musicVolume);
    }
    // During fading, unmute effect will be handled by next update() tick
}

} // namespace is::core
