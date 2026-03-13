#pragma once

#include <SFML/Audio.hpp>
#include <string>
#include <vector>
#include <mutex>
#include <random>
#include <memory>

namespace is::core {

/// Decodes and mixes game audio (music + SFX) into raw PCM samples that
/// can be piped to FFmpeg alongside the video stream.
///
/// This is separate from AudioManager (which plays to local speakers via
/// SFML).  AudioMixer decodes the same music files and SFX buffers into
/// a pull-based PCM stream for the stream encoder.
///
/// Thread-safe: pullSamples() is called from the encoder's audio thread,
/// while music/SFX control methods are called from the main/web threads.
class AudioMixer {
public:
    AudioMixer();
    ~AudioMixer();

    // ── Music ────────────────────────────────────────────────────────────

    /// Play one explicit track as the current stream-audio source.
    bool playTrack(const std::string& filepath);

    /// Crossfade from current track to explicit next track.
    bool crossfadeToTrack(const std::string& filepath);

    /// Stop mixer music playback.
    void stop();

    /// Pause / resume playback.
    void pause();
    void resume();

    /// Set music volume (0–100).
    void setMusicVolume(float volume);

    /// Fade settings propagated from AudioManager.
    void setFadeInDuration(float seconds);
    void setFadeOutDuration(float seconds);
    void setCrossfadeOverlap(float seconds);

    /// Set SFX volume (0–100).
    void setSfxVolume(float volume);

    /// Set muted state.
    void setMuted(bool muted);

    bool isMuted() const { return m_muted; }
    bool isPlaying() const { return m_playing && !m_paused; }

    // ── SFX ──────────────────────────────────────────────────────────────

    /// Queue a sound effect for mixing (by reference to an sf::SoundBuffer).
    /// @param volume  Per-sound volume multiplier (0–100), combined with global SFX volume.
    void playSfx(const sf::SoundBuffer& buffer, float volume = 100.0f);

    // ── PCM output ───────────────────────────────────────────────────────

    /// Pull mixed PCM samples (stereo interleaved, s16le, 44100 Hz).
    /// Called by the encoder audio thread.
    /// @param output  Destination buffer (must hold at least frameCount*2 samples)
    /// @param frameCount  Number of stereo frames to generate
    /// @return Number of frames actually written (always == frameCount)
    size_t pullSamples(sf::Int16* output, size_t frameCount);

    static constexpr unsigned int SAMPLE_RATE = 44100;
    static constexpr unsigned int CHANNELS    = 2;

private:
    // Music state
    std::unique_ptr<sf::InputSoundFile> m_musicFile;
    std::unique_ptr<sf::InputSoundFile> m_musicNext;
    unsigned int             m_musicChannels     = 0;
    unsigned int             m_musicSampleRate   = 0;
    unsigned int             m_musicNextChannels = 0;
    unsigned int             m_musicNextSampleRate = 0;
    bool                     m_playing           = false;
    bool                     m_paused            = false;
    bool                     m_isCrossfading     = false;
    float                    m_crossfadeTimer    = 0.0f;

    // Temporary read buffer for music decoding
    std::vector<sf::Int16>   m_musicReadBuf;
    std::vector<sf::Int16>   m_musicNextReadBuf;

    // Active SFX instances (mixed in pullSamples)
    struct SfxInstance {
        const sf::Int16* samples = nullptr;
        size_t       totalSamples = 0;
        unsigned int channels    = 0;
        unsigned int sampleRate  = 0;
        size_t       position    = 0;    ///< Current read position (in samples)
        float        volume      = 100.0f; ///< Per-sound volume (0–100)
    };
    std::vector<SfxInstance>  m_activeSfx;

    // Volume
    float m_musicVolume = 50.0f;  // 0–100
    float m_sfxVolume   = 80.0f;  // 0–100
    bool  m_muted       = false;
    float m_fadeInSeconds = 2.0f;
    float m_fadeOutSeconds = 2.0f;
    float m_crossfadeOverlap = 1.5f;

    std::mt19937       m_rng;
    mutable std::mutex m_mutex;
};

} // namespace is::core
