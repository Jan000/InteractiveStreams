#pragma once

#include "platform/ChatMessage.h"
#include <SFML/Graphics.hpp>
#include <string>
#include <vector>
#include <utility>
#include <unordered_map>
#include <functional>
#include <nlohmann/json.hpp>

namespace is::games {

/// Callback type for sending chat feedback to viewers.
/// Games call this to send confirmation messages (e.g. "Player joined!").
using ChatFeedbackCallback = std::function<void(const std::string& message)>;

/// Callback type for fetching audio spectrum data.
/// @param out       Destination buffer (numBands floats, normalised 0–1)
/// @param numBands  Number of desired frequency bands
/// @return true if data was available
using SpectrumCallback = std::function<bool(float* out, int numBands)>;

// ── Text-element layout ──────────────────────────────────────────────────────

/// Alignment modes for a text element.
enum class TextAlign { Left, Center, Right };

/// Describes default + overridden layout for a single rendered text element.
/// Positions are expressed as *percentages* of the render target (0..100).
struct TextElement {
    std::string id;             ///< Unique key (e.g. "title", "join_hint")
    std::string label;          ///< Human-readable label for UI
    float       x       = 50.f; ///< X position (% of screen width, 0=left, 100=right)
    float       y       = 5.f;  ///< Y position (% of screen height, 0=top, 100=bottom)
    int         fontSize = 24;  ///< Base font size in pixels (before fontScale)
    TextAlign   align    = TextAlign::Center;
    bool        visible  = true;
    std::string color;          ///< RGBA hex color (e.g. "#FF0000FF"), empty = game default
    std::string content;        ///< Text content override (empty = game decides dynamically)

    // Serialization helpers
    nlohmann::json toJson() const {
        return {
            {"id", id}, {"label", label},
            {"x", x}, {"y", y},
            {"font_size", fontSize},
            {"align", align == TextAlign::Left ? "left" : align == TextAlign::Right ? "right" : "center"},
            {"visible", visible},
            {"color", color},
            {"content", content}
        };
    }

    static TextElement fromJson(const nlohmann::json& j) {
        TextElement te;
        te.id       = j.value("id", "");
        te.label    = j.value("label", te.id);
        te.x        = j.value("x", 50.f);
        te.y        = j.value("y", 5.f);
        te.fontSize = j.value("font_size", 24);
        te.visible  = j.value("visible", true);
        te.color    = j.value("color", "");
        te.content  = j.value("content", "");
        std::string a = j.value("align", "center");
        te.align = (a == "left") ? TextAlign::Left : (a == "right") ? TextAlign::Right : TextAlign::Center;
        return te;
    }
};

/// Abstract interface that all games must implement.
/// This is the core extension point for adding new games.
class IGame {
public:
    virtual ~IGame() = default;

    /// Unique identifier for this game type.
    virtual std::string id() const = 0;

    /// Human-readable display name.
    virtual std::string displayName() const = 0;

    /// Short description.
    virtual std::string description() const = 0;

    /// Maximum number of players this game supports (0 = unlimited).
    virtual int maxPlayers() const { return 0; }

    /// Set the font scale factor (default 1.0).
    virtual void setFontScale(float scale) { m_fontScale = scale; }

    /// Get the current font scale factor.
    float fontScale() const { return m_fontScale; }

    /// Configure game-specific settings from JSON.
    /// Called when a game is loaded or when settings change.
    /// Games should override this to accept custom parameters.
    virtual void configure(const nlohmann::json& settings) { (void)settings; }

    /// Return current game-specific settings as JSON.
    virtual nlohmann::json getSettings() const { return {}; }

    /// Set the chat feedback callback.  The stream instance installs this
    /// so that games can send confirmation messages to viewers.
    void setChatFeedback(ChatFeedbackCallback cb) { m_chatFeedback = std::move(cb); }

    /// Set the spectrum callback.  The stream instance installs this
    /// so that games can query real-time audio spectrum data.
    void setSpectrumCallback(SpectrumCallback cb) { m_spectrumCallback = std::move(cb); }

    /// Initialize game state. Called once when the game is loaded.
    virtual void initialize() = 0;

    /// Clean up game state. Called when switching away from this game.
    virtual void shutdown() = 0;

    /// Process a chat message from a viewer.
    virtual void onChatMessage(const platform::ChatMessage& msg) = 0;

    /// Fixed-timestep game logic update.
    virtual void update(double dt) = 0;

    /// Render the game to the given target.
    /// @param alpha Interpolation factor for smooth rendering between updates.
    virtual void render(sf::RenderTarget& target, double alpha) = 0;

    /// Returns true if the current round / phase has ended and a game switch
    /// could happen without interrupting active gameplay.
    virtual bool isRoundComplete() const = 0;

    /// Returns true if the entire game session is over.
    virtual bool isGameOver() const = 0;

    /// Get current game state as JSON (for the web dashboard).
    virtual nlohmann::json getState() const = 0;

    /// Get available chat commands as JSON.
    virtual nlohmann::json getCommands() const = 0;

    /// Get current in-game leaderboard for overlay display.
    /// Returns a list of (displayName, score) pairs, already sorted descending.
    /// Default: empty (no in-game leaderboard).
    virtual std::vector<std::pair<std::string, int>> getLeaderboard() const { return {}; }

    // ── Text-element layout API ──────────────────────────────────────────────

    /// Return the list of text elements this game supports (with current values).
    const std::vector<TextElement>& textElements() const { return m_textElements; }

    /// Apply text-element overrides from JSON (called from configure()).
    void applyTextOverrides(const nlohmann::json& arr) {
        if (!arr.is_array()) return;
        for (const auto& j : arr) {
            std::string id = j.value("id", "");
            for (auto& te : m_textElements) {
                if (te.id == id) {
                    if (j.contains("x"))         te.x        = j["x"].get<float>();
                    if (j.contains("y"))         te.y        = j["y"].get<float>();
                    if (j.contains("font_size")) te.fontSize = j["font_size"].get<int>();
                    if (j.contains("visible"))   te.visible  = j["visible"].get<bool>();
                    if (j.contains("align")) {
                        std::string a = j["align"].get<std::string>();
                        te.align = (a == "left") ? TextAlign::Left : (a == "right") ? TextAlign::Right : TextAlign::Center;
                    }
                    if (j.contains("color")) te.color = j["color"].get<std::string>();
                    if (j.contains("content")) te.content = j["content"].get<std::string>();
                    break;
                }
            }
        }
    }

    /// Serialize text elements to JSON array.
    nlohmann::json textElementsJson() const {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& te : m_textElements) arr.push_back(te.toJson());
        return arr;
    }

protected:
    /// Send a feedback message to viewers via the installed callback.
    void sendChatFeedback(const std::string& message) {
        if (m_chatFeedback) m_chatFeedback(message);
    }

    /// Look up a text element by id.  Returns nullptr if not found.
    const TextElement* te(const std::string& id) const {
        for (const auto& e : m_textElements)
            if (e.id == id) return &e;
        return nullptr;
    }

    /// Resolve position of a named text element to pixel coordinates.
    /// targetW/targetH = render target size.
    /// Returns {pixelX, pixelY, resolvedFontSize, align, visible}.
    struct ResolvedText {
        float     px, py;
        unsigned  fontSize;
        TextAlign align;
        bool      visible;
        std::string color;    ///< RGBA hex (empty = use game default)
        std::string content;  ///< Text content override (empty = game default)
    };

    /// Parse a hex color string ("#RRGGBBAA" or "#RRGGBB") into sf::Color.
    /// Returns false if the string is empty or invalid.
    static bool parseHexColor(const std::string& hex, sf::Color& out) {
        if (hex.empty()) return false;
        std::string h = hex;
        if (h[0] == '#') h = h.substr(1);
        if (h.size() != 6 && h.size() != 8) return false;
        unsigned long val;
        try { val = std::stoul(h, nullptr, 16); } catch (...) { return false; }
        if (h.size() == 8) {
            out.r = (val >> 24) & 0xFF;
            out.g = (val >> 16) & 0xFF;
            out.b = (val >> 8)  & 0xFF;
            out.a = val & 0xFF;
        } else {
            out.r = (val >> 16) & 0xFF;
            out.g = (val >> 8)  & 0xFF;
            out.b = val & 0xFF;
            out.a = 255;
        }
        return true;
    }

    ResolvedText resolve(const std::string& id, float targetW, float targetH) const {
        const TextElement* e = te(id);
        if (!e) return {0, 0, 16, TextAlign::Center, true, "", ""};
        float px = e->x * targetW / 100.f;
        float py = e->y * targetH / 100.f;
        unsigned fs = static_cast<unsigned>(std::max(1.f, e->fontSize * m_fontScale));
        return {px, py, fs, e->align, e->visible, e->color, e->content};
    }

    /// Position an sf::Text using a ResolvedText.
    static void applyTextLayout(sf::Text& text, const ResolvedText& r) {
        text.setCharacterSize(r.fontSize);
        auto lb = text.getLocalBounds();
        switch (r.align) {
        case TextAlign::Left:
            text.setOrigin(0.f, 0.f);
            break;
        case TextAlign::Center:
            text.setOrigin(lb.left + lb.width / 2.f, 0.f);
            break;
        case TextAlign::Right:
            text.setOrigin(lb.left + lb.width, 0.f);
            break;
        }
        text.setPosition(r.px, r.py);

        // Apply configured color override if set
        sf::Color col;
        if (parseHexColor(r.color, col)) {
            text.setFillColor(col);
        }
    }

    /// Register default text elements.  Games call this in their constructor.
    void registerTextElement(const std::string& id, const std::string& label,
                             float x, float y, int fontSize,
                             TextAlign align = TextAlign::Center,
                             bool visible = true,
                             const std::string& defaultColor = "") {
        m_textElements.push_back({id, label, x, y, fontSize, align, visible, defaultColor});
    }

    /// Fetch audio spectrum.  Returns false when no data (e.g. no audio mixer).
    bool getSpectrum(float* bands, int numBands) {
        if (m_spectrumCallback) return m_spectrumCallback(bands, numBands);
        return false;
    }

    float m_fontScale = 1.0f;
    ChatFeedbackCallback m_chatFeedback;
    SpectrumCallback m_spectrumCallback;
    std::vector<TextElement> m_textElements;
};

} // namespace is::games
