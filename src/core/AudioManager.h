#pragma once

#include <SFML/Audio.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <random>
#include <mutex>

namespace is::core {

/// Manages background music playlist and sound effects.
/// Music tracks are loaded from a configurable directory and played in
/// shuffle order.  New tracks can be dropped into the directory at any
/// time – call rescan() to pick them up.
///
/// SFX are loaded once and played via short-lived sf::Sound instances.
class AudioManager {
public:
    AudioManager();
    ~AudioManager();

    // ── Music ────────────────────────────────────────────────────────────

    /// Scan a directory for music files (.mp3, .ogg, .wav, .flac).
    void scanMusicDirectory(const std::string& directory);

    /// Re-scan the music directory (picks up newly added files).
    void rescan();

    /// Start playing the playlist (shuffled).
    void playMusic();

    /// Pause / resume playback.
    void pauseMusic();
    void resumeMusic();

    /// Stop playback and reset playlist position.
    void stopMusic();

    /// Skip to the next track.
    void nextTrack();

    /// Must be called every frame to detect when a track ends.
    void update();

    /// Set music volume (0–100).
    void setMusicVolume(float volume);
    float musicVolume() const { return m_musicVolume; }

    /// Check state.
    bool isMusicPlaying() const;
    std::string currentTrackName() const;
    int trackCount() const;

    // ── SFX ──────────────────────────────────────────────────────────────

    /// Pre-load a sound effect from file.  Returns false if loading fails.
    bool loadSfx(const std::string& name, const std::string& filepath);

    /// Play a previously loaded SFX (fire-and-forget).
    void playSfx(const std::string& name, float volume = 100.0f);

    /// Set global SFX volume multiplier (0–100).
    void setSfxVolume(float volume);
    float sfxVolume() const { return m_sfxVolume; }

    // ── Mute ─────────────────────────────────────────────────────────────

    void setMuted(bool muted);
    bool isMuted() const { return m_muted; }

private:
    void shufflePlaylist();
    void loadTrack(int index);

    // Music
    std::string              m_musicDirectory;
    std::vector<std::string> m_playlist;       ///< shuffled file paths
    std::vector<std::string> m_allFiles;       ///< all discovered file paths
    int                      m_currentIndex = -1;
    sf::Music                m_music;
    float                    m_musicVolume = 50.0f;
    bool                     m_musicPaused = false;

    // SFX
    struct SfxEntry {
        sf::SoundBuffer buffer;
    };
    std::unordered_map<std::string, SfxEntry> m_sfxBuffers;
    std::vector<std::unique_ptr<sf::Sound>>   m_activeSounds; ///< playing sounds
    float                                     m_sfxVolume = 80.0f;

    bool     m_muted = false;
    std::mt19937 m_rng;
    mutable std::mutex m_mutex;
};

} // namespace is::core
