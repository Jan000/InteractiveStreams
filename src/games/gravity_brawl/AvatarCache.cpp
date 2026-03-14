#include "games/gravity_brawl/AvatarCache.h"

#include <httplib.h>
#include <spdlog/spdlog.h>

#include <cmath>

// Windows-native HTTPS when OpenSSL is not linked
#if defined(_WIN32) && !defined(CPPHTTPLIB_OPENSSL_SUPPORT)
#define AVATAR_USE_WINHTTP 1
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#endif

namespace is::games::gravity_brawl {

#ifdef AVATAR_USE_WINHTTP
static std::optional<std::string> downloadHttpsWinHttp(const std::string& host,
                                                       int port,
                                                       const std::string& path) {
    // Convert narrow strings to wide strings for WinHTTP
    auto toWide = [](const std::string& s) {
        if (s.empty()) return std::wstring();
        int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
        std::wstring w(len, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), len);
        return w;
    };

    std::wstring wHost = toWide(host);
    std::wstring wPath = toWide(path);

    HINTERNET hSession = WinHttpOpen(L"InteractiveStreams/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return std::nullopt;

    HINTERNET hConnect = WinHttpConnect(hSession, wHost.c_str(),
                                        static_cast<INTERNET_PORT>(port), 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return std::nullopt; }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", wPath.c_str(),
                                            nullptr, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return std::nullopt;
    }

    BOOL ok = WinHttpSendRequest(hRequest,
                                 WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                 WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (ok) ok = WinHttpReceiveResponse(hRequest, nullptr);

    std::string body;
    if (ok) {
        DWORD dwSize = 0;
        do {
            dwSize = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
            if (dwSize == 0) break;
            std::vector<char> buf(dwSize);
            DWORD dwRead = 0;
            if (WinHttpReadData(hRequest, buf.data(), dwSize, &dwRead))
                body.append(buf.data(), dwRead);
        } while (dwSize > 0);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (body.empty()) return std::nullopt;
    return body;
}
#endif

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
        auto failIt = m_failedUrls.find(url);
        if (failIt != m_failedUrls.end() && failIt->second >= MAX_RETRIES) return;

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
    m_failedUrls.clear();
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
            } else {
                m_failedUrls[url]++;
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

    std::string body;

    if (scheme == "https") {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
        httplib::SSLClient client(host, port);
        client.set_follow_location(true);
        client.set_connection_timeout(2, 0);
        client.set_read_timeout(3, 0);
        auto response = client.Get(path.c_str());
        if (!response || response->status != 200) return std::nullopt;
        body = std::move(response->body);
#elif defined(AVATAR_USE_WINHTTP)
        auto result = downloadHttpsWinHttp(host, port, path);
        if (!result) {
            spdlog::debug("[AvatarCache] WinHTTP download failed: {}", url);
            return std::nullopt;
        }
        body = std::move(*result);
#else
        spdlog::debug("[AvatarCache] HTTPS not supported by httplib build: {}", url);
        return std::nullopt;
#endif
    } else {
        httplib::Client client(host, port);
        client.set_follow_location(true);
        client.set_connection_timeout(2, 0);
        client.set_read_timeout(3, 0);
        auto response = client.Get(path.c_str());
        if (!response || response->status != 200) return std::nullopt;
        body = std::move(response->body);
    }

    sf::Image source;
    if (!source.loadFromMemory(body.data(), body.size())) {
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
