#pragma once

#include <SFML/Graphics.hpp>
#include <string>
#include <functional>
#include <vector>
#include <filesystem>

namespace is::rendering {

/// Main renderer – manages the SFML window and offscreen render target.
/// The offscreen target is used for streaming; the window is a local preview.
/// In headless mode the window is not created (for server deployments).
class Renderer {
public:
    Renderer(int width, int height, const std::string& title = "InteractiveStreams",
             bool headless = false);
    ~Renderer();

    /// Begin a new frame (clear the render target).
    void beginFrame();

    /// End the frame (display to window and capture for streaming).
    void endFrame();

    /// Get the render target for drawing.
    sf::RenderTarget& getRenderTarget();

    /// Get the captured frame pixels (RGBA) for streaming.
    const sf::Uint8* getFrameBuffer() const;

    /// Get frame dimensions.
    int width() const  { return m_width; }
    int height() const { return m_height; }

    /// Process window events.
    void processEvents(std::function<void(const sf::Event&)> handler);

    /// Check if the window is still open (always true in headless mode).
    bool isOpen() const;

    /// Resize the render target (e.g., from web dashboard).
    void resize(int width, int height);

    /// Update the window title.
    void setWindowTitle(const std::string& title);

    /// Display an external texture in the preview window (for multi-stream).
    void displayPreview(const sf::Texture& texture, int sourceWidth, int sourceHeight);

    /// Toggle headless mode at runtime (open/close preview window).
    void setHeadless(bool headless);

    /// Query current headless state.
    bool isHeadless() const { return m_headless; }

    /// Save a full-resolution screenshot of the given texture to screenshots/ dir.
    /// Returns the file path on success, empty string on failure.
    std::string takeScreenshot(const sf::Texture& texture);

private:
    int m_width;
    int m_height;

    sf::RenderWindow   m_window;
    sf::RenderTexture  m_renderTexture;
    sf::Image          m_frameCapture;
    sf::Sprite         m_previewSprite;

    bool m_headless = false;
};

} // namespace is::rendering
