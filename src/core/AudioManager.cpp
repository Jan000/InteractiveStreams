#include "core/AudioManager.h"
#include <spdlog/spdlog.h>
#include <filesystem>
#include <algorithm>

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

void AudioManager::loadTrack(int index) {
    // caller must hold m_mutex
    if (index < 0 || index >= static_cast<int>(m_playlist.size())) return;

    const auto& path = m_playlist[index];
    if (!m_music.openFromFile(path)) {
        spdlog::error("[Audio] Failed to open music file: {}", path);
        return;
    }

    m_music.setVolume(m_muted ? 0.0f : m_musicVolume);
    m_music.setLoop(false);
    m_currentIndex = index;

    auto filename = fs::path(path).filename().string();
    spdlog::info("[Audio] Now playing: {} ({}/{})", filename, index + 1, m_playlist.size());
}

void AudioManager::playMusic() {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_allFiles.empty()) {
        spdlog::warn("[Audio] No music files loaded – nothing to play.");
        return;
    }

    shufflePlaylist();
    m_currentIndex = 0;
    loadTrack(0);
    m_music.play();
    m_musicPaused = false;
}

void AudioManager::pauseMusic() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_music.getStatus() == sf::Music::Playing) {
        m_music.pause();
        m_musicPaused = true;
    }
}

void AudioManager::resumeMusic() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_musicPaused && m_music.getStatus() == sf::Music::Paused) {
        m_music.play();
        m_musicPaused = false;
    }
}

void AudioManager::stopMusic() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_music.stop();
    m_currentIndex = -1;
    m_musicPaused = false;
}

void AudioManager::nextTrack() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_playlist.empty()) return;

    int next = m_currentIndex + 1;
    if (next >= static_cast<int>(m_playlist.size())) {
        // Re-shuffle and restart
        shufflePlaylist();
        next = 0;
    }

    m_music.stop();
    loadTrack(next);
    m_music.play();
    m_musicPaused = false;
}

void AudioManager::update() {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Auto-advance to next track when current finishes
    if (m_currentIndex >= 0 && !m_musicPaused &&
        m_music.getStatus() == sf::Music::Stopped) {
        int next = m_currentIndex + 1;
        if (next >= static_cast<int>(m_playlist.size())) {
            shufflePlaylist();
            next = 0;
        }
        loadTrack(next);
        m_music.play();
    }

    // Clean up finished sounds
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
    if (!m_muted) {
        m_music.setVolume(m_musicVolume);
    }
}

bool AudioManager::isMusicPlaying() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_music.getStatus() == sf::Music::Playing;
}

std::string AudioManager::currentTrackName() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_currentIndex < 0 || m_currentIndex >= static_cast<int>(m_playlist.size()))
        return "";
    return fs::path(m_playlist[m_currentIndex]).stem().string();
}

int AudioManager::trackCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return static_cast<int>(m_allFiles.size());
}

// ── SFX ──────────────────────────────────────────────────────────────────────

bool AudioManager::loadSfx(const std::string& name, const std::string& filepath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    SfxEntry entry;
    if (!entry.buffer.loadFromFile(filepath)) {
        spdlog::error("[Audio] Failed to load SFX '{}' from '{}'", name, filepath);
        return false;
    }
    m_sfxBuffers[name] = std::move(entry);
    spdlog::info("[Audio] Loaded SFX: {}", name);
    return true;
}

void AudioManager::playSfx(const std::string& name, float volume) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_sfxBuffers.find(name);
    if (it == m_sfxBuffers.end()) return;

    auto sound = std::make_unique<sf::Sound>();
    sound->setBuffer(it->second.buffer);
    float effectiveVol = m_muted ? 0.0f : (volume * m_sfxVolume / 100.0f);
    sound->setVolume(effectiveVol);
    sound->play();
    m_activeSounds.push_back(std::move(sound));

    // Limit active sounds to prevent resource exhaustion
    while (m_activeSounds.size() > 32) {
        m_activeSounds.erase(m_activeSounds.begin());
    }
}

void AudioManager::setSfxVolume(float volume) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_sfxVolume = std::clamp(volume, 0.0f, 100.0f);
}

// ── Mute ─────────────────────────────────────────────────────────────────────

void AudioManager::setMuted(bool muted) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_muted = muted;
    m_music.setVolume(muted ? 0.0f : m_musicVolume);
    // Active sounds will be cleaned up naturally
}

} // namespace is::core
