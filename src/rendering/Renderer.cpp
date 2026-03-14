#include "rendering/Renderer.h"
#include <spdlog/spdlog.h>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace is::rendering {

Renderer::Renderer(int width, int height, const std::string& title, bool headless)
    : m_width(width), m_height(height), m_headless(headless)
{
    if (!headless) {
        // Create window for local preview (scaled down to fit desktop)
        sf::ContextSettings settings;
        settings.antialiasingLevel = 4;

        // For tall/vertical resolutions, scale the preview window to fit screen
        int previewW = width;
        int previewH = height;
        if (height > 1080 || width > 1920) {
            float scale = std::min(1080.0f / height, 1920.0f / width);
            previewW = static_cast<int>(width * scale);
            previewH = static_cast<int>(height * scale);
        }

        m_window.create(sf::VideoMode(previewW, previewH), title,
            sf::Style::Default, settings);
        m_window.setVerticalSyncEnabled(false);
        m_window.setFramerateLimit(0);  // We manage timing ourselves
    }

    // Create offscreen render texture (for streaming)
    if (!m_renderTexture.create(width, height)) {
        spdlog::error("Failed to create render texture {}x{}", width, height);
    }
    m_renderTexture.setSmooth(true);

    m_previewSprite.setTexture(m_renderTexture.getTexture());

    spdlog::info("Renderer initialized: {}x{} (headless={})", width, height, headless);
}

Renderer::~Renderer() {
    if (m_window.isOpen()) {
        m_window.close();
    }
}

void Renderer::beginFrame() {
    m_renderTexture.clear(sf::Color(15, 15, 25));
}

void Renderer::endFrame() {
    m_renderTexture.display();

    // Capture frame for streaming
    m_frameCapture = m_renderTexture.getTexture().copyToImage();

    // Draw to preview window
    if (m_window.isOpen()) {
        m_window.clear();
        m_previewSprite.setTexture(m_renderTexture.getTexture());

        // Scale to window size
        auto windowSize = m_window.getSize();
        float scaleX = static_cast<float>(windowSize.x) / m_width;
        float scaleY = static_cast<float>(windowSize.y) / m_height;
        float scale = std::min(scaleX, scaleY);

        m_previewSprite.setScale(scale, scale);
        m_previewSprite.setPosition(
            (windowSize.x - m_width * scale) / 2,
            (windowSize.y - m_height * scale) / 2);

        m_window.draw(m_previewSprite);
        m_window.display();
    }
}

sf::RenderTarget& Renderer::getRenderTarget() {
    return m_renderTexture;
}

const sf::Uint8* Renderer::getFrameBuffer() const {
    if (m_frameCapture.getSize().x == 0) return nullptr;
    return m_frameCapture.getPixelsPtr();
}

void Renderer::processEvents(std::function<void(const sf::Event&)> handler) {
    if (!m_window.isOpen()) return;

    sf::Event event;
    while (m_window.pollEvent(event)) {
        handler(event);

        if (event.type == sf::Event::Resized) {
            sf::FloatRect visibleArea(0, 0,
                static_cast<float>(event.size.width),
                static_cast<float>(event.size.height));
            m_window.setView(sf::View(visibleArea));
        }
    }
}

bool Renderer::isOpen() const {
    if (m_headless) return true;
    return m_window.isOpen();
}

void Renderer::resize(int width, int height) {
    m_width = width;
    m_height = height;
    m_renderTexture.create(width, height);
    m_renderTexture.setSmooth(true);
    spdlog::info("Renderer resized to {}x{}", width, height);
}

void Renderer::setWindowTitle(const std::string& title) {
    if (m_window.isOpen()) {
        m_window.setTitle(title);
    }
}

void Renderer::displayPreview(const sf::Texture& texture,
                               int sourceWidth, int sourceHeight) {
    if (!m_window.isOpen()) return;

    m_window.clear();

    sf::Sprite sprite(texture);
    auto ws = m_window.getSize();
    float scaleX = static_cast<float>(ws.x) / sourceWidth;
    float scaleY = static_cast<float>(ws.y) / sourceHeight;
    float scale  = std::min(scaleX, scaleY);

    sprite.setScale(scale, scale);
    sprite.setPosition(
        (ws.x - sourceWidth  * scale) / 2.0f,
        (ws.y - sourceHeight * scale) / 2.0f);

    m_window.draw(sprite);
    m_window.display();
}

void Renderer::setHeadless(bool headless) {
    if (headless == m_headless) return;
    m_headless = headless;

    if (headless) {
        // Switching TO headless – close the window
        if (m_window.isOpen()) {
            m_window.close();
            spdlog::info("Renderer: switched to headless mode (window closed)");
        }
    } else {
        // Switching FROM headless – open the window
        sf::ContextSettings settings;
        settings.antialiasingLevel = 4;

        int previewW = m_width;
        int previewH = m_height;
        if (m_height > 1080 || m_width > 1920) {
            float scale = std::min(1080.0f / m_height, 1920.0f / m_width);
            previewW = static_cast<int>(m_width * scale);
            previewH = static_cast<int>(m_height * scale);
        }

        m_window.create(sf::VideoMode(previewW, previewH),
            "InteractiveStreams", sf::Style::Default, settings);
        m_window.setVerticalSyncEnabled(false);
        m_window.setFramerateLimit(0);
        spdlog::info("Renderer: switched to windowed mode ({}x{})", previewW, previewH);
    }
}

std::string Renderer::takeScreenshot(const sf::Texture& texture) {
    namespace fs = std::filesystem;

    // Create screenshots directory next to executable
    fs::path dir = fs::current_path() / "screenshots";
    std::error_code ec;
    fs::create_directories(dir, ec);

    // Timestamp filename: screenshot_YYYYMMDD_HHMMSS.png
    auto now = std::chrono::system_clock::now();
    auto tt  = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    std::ostringstream oss;
    oss << "screenshot_" << std::put_time(&tm, "%Y%m%d_%H%M%S") << ".png";
    fs::path filePath = dir / oss.str();

    sf::Image img = texture.copyToImage();
    if (img.saveToFile(filePath.string())) {
        spdlog::info("Screenshot saved: {}", filePath.string());
        return filePath.string();
    }
    spdlog::error("Failed to save screenshot: {}", filePath.string());
    return {};
}

} // namespace is::rendering
