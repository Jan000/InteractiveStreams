#include "games/gravity_brawl/AvatarCache.h"

#include <httplib.h>
#include <spdlog/spdlog.h>

#include <cmath>

namespace is::games::gravity_brawl {

AvatarCache::AvatarCache() {
    m_running = true;
    m_worker = std::thread(&AvatarCache::workerLoop, this);
}

AvatarCache::~AvatarCache() {
    m_running = false;
    m_cv.notify_all();
    if (m_worker.joinable()) {
        m_worker.join();
    }
}

void AvatarCache::request(const std::string& url) {
    if (url.empty()) return;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_textures.find(url) != m_textures.end()) return;
        if (m_inFlight.find(url) != m_inFlight.end()) return;

        m_inFlight.insert(url);
        m_downloadQueue.push(url);
    }

    m_cv.notify_one();
}

void AvatarCache::processPendingUploads(size_t maxUploadsPerTick) {
    for (size_t i = 0; i < maxUploadsPerTick; ++i) {
        PreparedAvatar prepared;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_readyImages.empty()) break;
            prepared = std::move(m_readyImages.front());
            m_readyImages.pop();
        }

        sf::Texture texture;
        if (!texture.loadFromImage(prepared.image)) {
            spdlog::debug("[AvatarCache] Texture upload failed for {}", prepared.url);
            continue;
        }
        texture.setSmooth(false);
        texture.setRepeated(false);

        std::lock_guard<std::mutex> lock(m_mutex);
        m_textures[prepared.url] = TextureEntry{
            std::move(texture),
            std::chrono::steady_clock::now()
        };
    }
}

const sf::Texture* AvatarCache::getTexture(const std::string& url) {
    if (url.empty()) return nullptr;

    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_textures.find(url);
    if (it == m_textures.end()) return nullptr;

    it->second.lastUsed = std::chrono::steady_clock::now();
    return &it->second.texture;
}

void AvatarCache::cleanupUnused(const std::unordered_set<std::string>& activeUrls,
                                std::chrono::seconds maxIdle) {
    const auto now = std::chrono::steady_clock::now();

    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto it = m_textures.begin(); it != m_textures.end();) {
        const bool active = activeUrls.find(it->first) != activeUrls.end();
        const bool stale = (now - it->second.lastUsed) > maxIdle;
        if (!active && stale) {
            it = m_textures.erase(it);
        } else {
            ++it;
        }
    }
}

void AvatarCache::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_downloadQueue = {};
    m_readyImages = {};
    m_inFlight.clear();
    m_textures.clear();
}

void AvatarCache::workerLoop() {
    while (m_running.load()) {
        std::string url;

        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] {
                return !m_running.load() || !m_downloadQueue.empty();
            });

            if (!m_running.load()) break;
            url = std::move(m_downloadQueue.front());
            m_downloadQueue.pop();
        }

        auto prepared = downloadAndPrepare(url);

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_inFlight.erase(url);
            if (prepared.has_value()) {
                m_readyImages.push(std::move(*prepared));
            }
        }
    }
}

std::optional<AvatarCache::PreparedAvatar> AvatarCache::downloadAndPrepare(const std::string& url) const {
    std::string scheme;
    std::string host;
    std::string path;
    int port = 0;

    if (!parseUrl(url, scheme, host, port, path)) {
        spdlog::debug("[AvatarCache] Invalid avatar URL: {}", url);
        return std::nullopt;
    }

    httplib::Result response;

    if (scheme == "https") {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
        httplib::SSLClient client(host, port);
        client.set_follow_location(true);
        client.set_connection_timeout(2, 0);
        client.set_read_timeout(3, 0);
        response = client.Get(path.c_str());
#else
        spdlog::debug("[AvatarCache] HTTPS not supported by httplib build: {}", url);
        return std::nullopt;
#endif
    } else {
        httplib::Client client(host, port);
        client.set_follow_location(true);
        client.set_connection_timeout(2, 0);
        client.set_read_timeout(3, 0);
        response = client.Get(path.c_str());
    }

    if (!response || response->status != 200) {
        return std::nullopt;
    }

    sf::Image source;
    if (!source.loadFromMemory(response->body.data(), response->body.size())) {
        return std::nullopt;
    }

    sf::Image scaled = downscaleTo64(source);
    bakeCircularAlphaMask64(scaled);

    return PreparedAvatar{url, std::move(scaled)};
}

bool AvatarCache::parseUrl(const std::string& url,
                           std::string& scheme,
                           std::string& host,
                           int& port,
                           std::string& path) {
    const auto schemePos = url.find("://");
    if (schemePos == std::string::npos) return false;

    scheme = url.substr(0, schemePos);
    std::string rest = url.substr(schemePos + 3);

    const auto pathPos = rest.find('/');
    std::string hostPort = (pathPos == std::string::npos) ? rest : rest.substr(0, pathPos);
    path = (pathPos == std::string::npos) ? "/" : rest.substr(pathPos);

    const auto portPos = hostPort.find(':');
    if (portPos == std::string::npos) {
        host = hostPort;
        port = (scheme == "https") ? 443 : 80;
    } else {
        host = hostPort.substr(0, portPos);
        try {
            port = std::stoi(hostPort.substr(portPos + 1));
        } catch (...) {
            return false;
        }
    }

    if (host.empty()) return false;
    if (path.empty()) path = "/";
    return (scheme == "http" || scheme == "https");
}

sf::Image AvatarCache::downscaleTo64(const sf::Image& src) {
    sf::Image out;
    out.create(64, 64, sf::Color(0, 0, 0, 0));

    const auto srcSize = src.getSize();
    if (srcSize.x == 0 || srcSize.y == 0) {
        return out;
    }

    for (unsigned y = 0; y < 64; ++y) {
        for (unsigned x = 0; x < 64; ++x) {
            unsigned sx = std::min(srcSize.x - 1, static_cast<unsigned>((x * srcSize.x) / 64));
            unsigned sy = std::min(srcSize.y - 1, static_cast<unsigned>((y * srcSize.y) / 64));
            out.setPixel(x, y, src.getPixel(sx, sy));
        }
    }

    return out;
}

void AvatarCache::bakeCircularAlphaMask64(sf::Image& image) {
    constexpr float center = 31.5f;
    constexpr float radius = 32.0f;
    constexpr float radiusSq = radius * radius;

    for (unsigned y = 0; y < 64; ++y) {
        for (unsigned x = 0; x < 64; ++x) {
            const float dx = static_cast<float>(x) - center;
            const float dy = static_cast<float>(y) - center;
            const float distSq = dx * dx + dy * dy;
            if (distSq > radiusSq) {
                sf::Color c = image.getPixel(x, y);
                c.a = 0;
                image.setPixel(x, y, c);
            }
        }
    }
}

} // namespace is::games::gravity_brawl
