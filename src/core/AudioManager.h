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
/// Supports crossfade between tracks: the outgoing track fades out while
/// the incoming track fades in, with a configurable overlap duration.
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

    /// Skip to the next track (triggers crossfade if enabled).
    void nextTrack();

    /// Must be called every frame with elapsed time since last call.
    void update(double dt);

    /// Set music volume (0–100).
    void setMusicVolume(float volume);
    float musicVolume() const { return m_musicVolume; }

    /// Check state.
    bool isMusicPlaying() const;
    std::string currentTrackName() const;
    int trackCount() const;

    // ── Crossfade ────────────────────────────────────────────────────────

    /// Set fade-in duration in seconds (0 = instant).
    void setFadeInDuration(float seconds);
    float fadeInDuration() const { return m_fadeInSeconds; }

    /// Set fade-out duration in seconds (0 = instant).
    void setFadeOutDuration(float seconds);
    float fadeOutDuration() const { return m_fadeOutSeconds; }

    /// Set crossfade overlap in seconds (how long both tracks play together).
    void setCrossfadeOverlap(float seconds);
    float crossfadeOverlap() const { return m_crossfadeOverlap; }

    // ── SFX ──────────────────────────────────────────────────────────────

    /// Pre-load a sound effect from a single file. Returns false if loading fails.
    bool loadSfx(const std::string& name, const std::string& filepath);

    /// Pre-load all audio files from a directory as variants for one SFX name.
    /// Each call to playSfx() will pick a random variant. Returns false if no
    /// files were found or the directory does not exist.
    bool loadSfxFromDirectory(const std::string& name, const std::string& dirpath);

    /// Play a previously loaded SFX (fire-and-forget).
    void playSfx(const std::string& name, float volume = 100.0f);

    /// Set global SFX volume multiplier (0–100).
    void setSfxVolume(float volume);
    float sfxVolume() const { return m_sfxVolume; }

    /// Set an AudioMixer to receive SFX for stream encoding.
    void setAudioMixer(class AudioMixer* mixer);

    // ── Mute ─────────────────────────────────────────────────────────────

    void setMuted(bool muted);
    bool isMuted() const { return m_muted; }

private:
    void shufflePlaylist();
    bool loadTrackInto(sf::Music& music, int index);
    int  nextTrackIndex() const;
    void beginCrossfade();
    void finishCrossfade();

    // Music
    std::string              m_musicDirectory;
    std::vector<std::string> m_playlist;       ///< shuffled file paths
    std::vector<std::string> m_allFiles;       ///< all discovered file paths
    int                      m_currentIndex = -1;
    int                      m_nextIndex    = -1;
    float                    m_musicVolume  = 50.0f;
    bool                     m_musicPaused  = false;

    // Two music streams for crossfade
    std::unique_ptr<sf::Music> m_musicCurrent;
    std::unique_ptr<sf::Music> m_musicNext;

    // Crossfade parameters
    float m_fadeInSeconds     = 2.0f;
    float m_fadeOutSeconds    = 2.0f;
    float m_crossfadeOverlap = 1.5f;

    // Crossfade state
    enum class FadeState {
        None,         ///< Normal playback, no fading
        FadingIn,     ///< Single track fading in (first play)
        Crossfading,  ///< Two tracks overlapping (old fading out, new fading in)
    };
    FadeState m_fadeState = FadeState::None;
    float     m_fadeTimer = 0.0f;  ///< elapsed time since fade started

    // SFX
    struct SfxEntry {
        std::vector<std::unique_ptr<sf::SoundBuffer>> buffers;
    };
    std::unordered_map<std::string, SfxEntry> m_sfxBuffers;
    std::vector<std::unique_ptr<sf::Sound>>   m_activeSounds; ///< playing sounds
    float                                     m_sfxVolume = 80.0f;

    bool     m_muted = false;
    class AudioMixer* m_audioMixer = nullptr;
    std::mt19937 m_rng;
    mutable std::mutex m_mutex;
};

} // namespace is::core
