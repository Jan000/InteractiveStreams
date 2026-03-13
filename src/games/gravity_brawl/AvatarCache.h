#pragma once

#include <SFML/Graphics/Image.hpp>
#include <SFML/Graphics/Texture.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace is::games::gravity_brawl {

// CPU-oriented avatar cache:
// - Download and pre-bake avatars in a background worker
// - Convert to 64x64 and circular alpha mask before entering render path
// - Create sf::Texture only on the main thread via processPendingUploads()
class AvatarCache {
public:
    AvatarCache();
    ~AvatarCache();

    AvatarCache(const AvatarCache&) = delete;
    AvatarCache& operator=(const AvatarCache&) = delete;

    void request(const std::string& url);

    // Must be called from main thread. Creates textures from pre-baked images.
    void processPendingUploads(size_t maxUploadsPerTick = 2);

    // Returns nullptr if avatar isn't ready. Updates last-used timestamp on hit.
    const sf::Texture* getTexture(const std::string& url);

    // Frees textures that are not active and unused for maxIdle.
    void cleanupUnused(const std::unordered_set<std::string>& activeUrls,
                       std::chrono::seconds maxIdle = std::chrono::seconds(120));

    void clear();

private:
    struct PreparedAvatar {
        std::string url;
        sf::Image image;
    };

    struct TextureEntry {
        sf::Texture texture;
        std::chrono::steady_clock::time_point lastUsed;
    };

    void workerLoop();
    std::optional<PreparedAvatar> downloadAndPrepare(const std::string& url) const;

    static bool parseUrl(const std::string& url,
                         std::string& scheme,
                         std::string& host,
                         int& port,
                         std::string& path);
    static sf::Image downscaleTo64(const sf::Image& src);
    static void bakeCircularAlphaMask64(sf::Image& image);

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::queue<std::string> m_downloadQueue;
    std::queue<PreparedAvatar> m_readyImages;
    std::unordered_set<std::string> m_inFlight;
    std::unordered_map<std::string, TextureEntry> m_textures;
    std::unordered_map<std::string, int> m_failedUrls; // URL → retry count
    static constexpr int MAX_RETRIES = 2;

    std::atomic<bool> m_running{false};
    std::thread m_worker;
};

} // namespace is::games::gravity_brawl
