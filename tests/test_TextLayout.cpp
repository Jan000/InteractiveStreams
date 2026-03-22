#include <doctest/doctest.h>

#include <SFML/Graphics/Text.hpp>
#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/RenderTexture.hpp>

#include <string>
#include <cmath>

#include "games/IGame.h"

// ══════════════════════════════════════════════════════════════════════════════
// Minimal concrete IGame for testing the text layout system
// ══════════════════════════════════════════════════════════════════════════════

namespace {

using namespace is::games;
using namespace is::platform;

class TestGame : public IGame {
public:
    std::string id() const override { return "test_game"; }
    std::string displayName() const override { return "Test Game"; }
    std::string description() const override { return "For testing"; }
    void initialize() override {}
    void shutdown() override {}
    void onChatMessage(const ChatMessage&) override {}
    void update(double) override {}
    void render(sf::RenderTarget&, double) override {}
    bool isRoundComplete() const override { return false; }
    bool isGameOver() const override { return false; }
    nlohmann::json getState() const override { return {}; }
    nlohmann::json getCommands() const override { return {}; }

    // Expose protected methods for testing
    using IGame::registerTextElement;
    using IGame::resolve;
    using IGame::applyTextLayout;
    using IGame::te;
    using IGame::parseHexColor;
    using IGame::m_textElements;
    using IGame::m_fontScale;
};

constexpr float W = 1080.f;
constexpr float H = 1920.f;

} // anonymous namespace

// ══════════════════════════════════════════════════════════════════════════════
TEST_SUITE("TextElement") {
// ══════════════════════════════════════════════════════════════════════════════

    TEST_CASE("Default TextElement values") {
        TextElement te;
        CHECK(te.x == 50.f);
        CHECK(te.y == 5.f);
        CHECK(te.fontSize == 24);
        CHECK(te.align == TextAlign::Center);
        CHECK(te.alignY == "top");
        CHECK(te.visible == true);
        CHECK(te.color.empty());
        CHECK(te.content.empty());
    }

    TEST_CASE("TextElement JSON round-trip") {
        TextElement orig;
        orig.id = "title";
        orig.label = "Title";
        orig.x = 3.5f;
        orig.y = 10.f;
        orig.fontSize = 32;
        orig.align = TextAlign::Right;
        orig.alignY = "bottom";
        orig.visible = false;
        orig.color = "#FF0000FF";
        orig.content = "Hello World";

        auto j = orig.toJson();
        auto restored = TextElement::fromJson(j);

        CHECK(restored.id == orig.id);
        CHECK(restored.label == orig.label);
        CHECK(restored.x == doctest::Approx(orig.x));
        CHECK(restored.y == doctest::Approx(orig.y));
        CHECK(restored.fontSize == orig.fontSize);
        CHECK(restored.align == orig.align);
        CHECK(restored.alignY == orig.alignY);
        CHECK(restored.visible == orig.visible);
        CHECK(restored.color == orig.color);
        CHECK(restored.content == orig.content);
    }

    TEST_CASE("TextElement fromJson with minimal fields") {
        nlohmann::json j = {{"id", "hint"}, {"align", "left"}};
        auto te = TextElement::fromJson(j);
        CHECK(te.id == "hint");
        CHECK(te.align == TextAlign::Left);
        // Defaults for missing fields
        CHECK(te.x == 50.f);
        CHECK(te.y == 5.f);
        CHECK(te.fontSize == 24);
        CHECK(te.alignY == "top");
        CHECK(te.visible == true);
    }

} // TEST_SUITE TextElement

// ══════════════════════════════════════════════════════════════════════════════
TEST_SUITE("resolve – Anchor-Relative Positioning") {
// ══════════════════════════════════════════════════════════════════════════════

    TEST_CASE("resolve returns screen center for unknown element") {
        TestGame game;
        auto r = game.resolve("nonexistent", W, H);
        CHECK(r.px == doctest::Approx(W / 2.f));
        CHECK(r.py == doctest::Approx(H / 2.f));
        CHECK(r.fontSize == 16);
        CHECK(r.align == TextAlign::Center);
        CHECK(r.alignY == "center");
        CHECK(r.visible == true);
    }

    TEST_CASE("Center-aligned, x=0, y=0: perfectly centered") {
        TestGame game;
        game.registerTextElement("ct", "Center Test", 0.f, 0.f, 24, TextAlign::Center, true, "", "center");
        auto r = game.resolve("ct", W, H);
        CHECK(r.px == doctest::Approx(W / 2.f));
        CHECK(r.py == doctest::Approx(H / 2.f));
    }

    TEST_CASE("Center-aligned, x=0, y=0, alignY=top: top-center") {
        TestGame game;
        game.registerTextElement("tc", "Top Center", 0.f, 0.f, 24, TextAlign::Center, true, "", "top");
        auto r = game.resolve("tc", W, H);
        CHECK(r.px == doctest::Approx(W / 2.f));
        CHECK(r.py == doctest::Approx(0.f));
    }

    TEST_CASE("Center-aligned with positive x offset: shifted right from center") {
        TestGame game;
        // x=5 means 5% of W offset right from center
        game.registerTextElement("cr", "Center Right", 5.f, 0.f, 24, TextAlign::Center, true, "", "center");
        auto r = game.resolve("cr", W, H);
        float expectedX = W / 2.f + (5.f * W / 100.f);
        CHECK(r.px == doctest::Approx(expectedX));
        CHECK(r.py == doctest::Approx(H / 2.f));
    }

    TEST_CASE("Left-aligned, x=0: left edge") {
        TestGame game;
        game.registerTextElement("ll", "Left", 0.f, 2.f, 20, TextAlign::Left, true, "", "top");
        auto r = game.resolve("ll", W, H);
        CHECK(r.px == doctest::Approx(0.f));
        CHECK(r.py == doctest::Approx(2.f * H / 100.f));
    }

    TEST_CASE("Left-aligned, x=1.4: offset from left edge") {
        TestGame game;
        game.registerTextElement("lo", "Left Offset", 1.4f, 0.f, 20, TextAlign::Left, true, "", "top");
        auto r = game.resolve("lo", W, H);
        CHECK(r.px == doctest::Approx(1.4f * W / 100.f));
    }

    TEST_CASE("Right-aligned, x=0: right edge") {
        TestGame game;
        game.registerTextElement("re", "Right Edge", 0.f, 2.f, 20, TextAlign::Right, true, "", "top");
        auto r = game.resolve("re", W, H);
        CHECK(r.px == doctest::Approx(W));
    }

    TEST_CASE("Right-aligned, x=1.4: offset inward from right edge") {
        TestGame game;
        game.registerTextElement("ro", "Right Offset", 1.4f, 2.f, 20, TextAlign::Right, true, "", "top");
        auto r = game.resolve("ro", W, H);
        float expectedX = W - (1.4f * W / 100.f);
        CHECK(r.px == doctest::Approx(expectedX));
    }

    TEST_CASE("Bottom-aligned, y=0: bottom edge") {
        TestGame game;
        game.registerTextElement("be", "Bottom Edge", 0.f, 0.f, 20, TextAlign::Center, true, "", "bottom");
        auto r = game.resolve("be", W, H);
        CHECK(r.px == doctest::Approx(W / 2.f));
        CHECK(r.py == doctest::Approx(H));
    }

    TEST_CASE("Bottom-aligned, y=2: offset upward from bottom") {
        TestGame game;
        game.registerTextElement("bo", "Bottom Ofst", 0.f, 2.f, 20, TextAlign::Center, true, "", "bottom");
        auto r = game.resolve("bo", W, H);
        float expectedY = H - (2.f * H / 100.f);
        CHECK(r.py == doctest::Approx(expectedY));
    }

    TEST_CASE("Top-aligned, y=3: offset downward from top") {
        TestGame game;
        game.registerTextElement("td", "Top Down", 0.f, 3.f, 20, TextAlign::Center, true, "", "top");
        auto r = game.resolve("td", W, H);
        CHECK(r.py == doctest::Approx(3.f * H / 100.f));
    }

    TEST_CASE("Font scale multiplier is applied") {
        TestGame game;
        game.registerTextElement("fs", "Scaled", 0.f, 0.f, 24, TextAlign::Center);
        game.m_fontScale = 2.0f;
        auto r = game.resolve("fs", W, H);
        CHECK(r.fontSize == 48);
    }

    TEST_CASE("Font size is at least 1") {
        TestGame game;
        game.registerTextElement("tiny", "Tiny", 0.f, 0.f, 1, TextAlign::Center);
        game.m_fontScale = 0.01f;
        auto r = game.resolve("tiny", W, H);
        CHECK(r.fontSize >= 1);
    }

    TEST_CASE("Visibility is forwarded") {
        TestGame game;
        game.registerTextElement("hidden", "Hidden", 0.f, 0.f, 24, TextAlign::Center, false);
        auto r = game.resolve("hidden", W, H);
        CHECK(r.visible == false);
    }

    TEST_CASE("Color is forwarded") {
        TestGame game;
        game.registerTextElement("col", "Colored", 0.f, 0.f, 24, TextAlign::Center, true, "#FF0000FF");
        auto r = game.resolve("col", W, H);
        CHECK(r.color == "#FF0000FF");
    }

    TEST_CASE("Different target sizes scale positions") {
        TestGame game;
        game.registerTextElement("sc", "Scaled", 10.f, 10.f, 24, TextAlign::Left, true, "", "top");
        // 1080×1920
        auto r1 = game.resolve("sc", 1080.f, 1920.f);
        CHECK(r1.px == doctest::Approx(108.f));
        CHECK(r1.py == doctest::Approx(192.f));
        // 1920×1080
        auto r2 = game.resolve("sc", 1920.f, 1080.f);
        CHECK(r2.px == doctest::Approx(192.f));
        CHECK(r2.py == doctest::Approx(108.f));
    }

} // TEST_SUITE resolve

// ══════════════════════════════════════════════════════════════════════════════
TEST_SUITE("registerTextElement & management") {
// ══════════════════════════════════════════════════════════════════════════════

    TEST_CASE("registerTextElement adds elements") {
        TestGame game;
        CHECK(game.textElements().empty());
        game.registerTextElement("a", "A", 0.f, 0.f, 24);
        game.registerTextElement("b", "B", 1.f, 2.f, 16, TextAlign::Left);
        CHECK(game.textElements().size() == 2);
        CHECK(game.te("a") != nullptr);
        CHECK(game.te("b") != nullptr);
        CHECK(game.te("c") == nullptr);
    }

    TEST_CASE("applyTextOverrides modifies existing element") {
        TestGame game;
        game.registerTextElement("title", "Title", 0.f, 2.f, 32, TextAlign::Center);

        nlohmann::json overrides = nlohmann::json::array();
        overrides.push_back({{"id", "title"}, {"x", 5.0}, {"font_size", 48}, {"visible", false}});
        game.applyTextOverrides(overrides);

        auto* t = game.te("title");
        REQUIRE(t != nullptr);
        CHECK(t->x == doctest::Approx(5.0f));
        CHECK(t->fontSize == 48);
        CHECK(t->visible == false);
        // Unchanged fields
        CHECK(t->y == doctest::Approx(2.f));
        CHECK(t->align == TextAlign::Center);
    }

    TEST_CASE("applyTextOverrides creates new custom element") {
        TestGame game;
        nlohmann::json overrides = nlohmann::json::array();
        overrides.push_back({{"id", "custom1"}, {"label", "Custom"}, {"x", 10.0}, {"y", 20.0},
                             {"font_size", 18}, {"content", "Hello"}});
        game.applyTextOverrides(overrides);

        auto* t = game.te("custom1");
        REQUIRE(t != nullptr);
        CHECK(t->label == "Custom");
        CHECK(t->content == "Hello");
    }

    TEST_CASE("applyTextOverrides deletes element with _deleted flag") {
        TestGame game;
        game.registerTextElement("del_me", "Delete Me", 0.f, 0.f, 24);
        CHECK(game.te("del_me") != nullptr);

        nlohmann::json overrides = nlohmann::json::array();
        overrides.push_back({{"id", "del_me"}, {"_deleted", true}});
        game.applyTextOverrides(overrides);

        CHECK(game.te("del_me") == nullptr);
    }

    TEST_CASE("removeTextElement returns false for unknown id") {
        TestGame game;
        CHECK(game.removeTextElement("nope") == false);
    }

    TEST_CASE("textElementsJson round-trips") {
        TestGame game;
        game.registerTextElement("t1", "Title", 0.f, 2.f, 32, TextAlign::Center, true, "#FFFFFF");
        game.registerTextElement("t2", "Sub", 1.f, 5.f, 20, TextAlign::Left, false, "", "bottom");

        auto j = game.textElementsJson();
        CHECK(j.size() == 2);
        CHECK(j[0]["id"] == "t1");
        CHECK(j[1]["id"] == "t2");
        CHECK(j[1]["align"] == "left");
        CHECK(j[1]["align_y"] == "bottom");
    }

} // TEST_SUITE registerTextElement

// ══════════════════════════════════════════════════════════════════════════════
TEST_SUITE("parseHexColor") {
// ══════════════════════════════════════════════════════════════════════════════

    TEST_CASE("Empty string returns false") {
        sf::Color c;
        CHECK(TestGame::parseHexColor("", c) == false);
    }

    TEST_CASE("6-digit hex (#RRGGBB)") {
        sf::Color c;
        REQUIRE(TestGame::parseHexColor("#FF8000", c));
        CHECK(c.r == 255);
        CHECK(c.g == 128);
        CHECK(c.b == 0);
        CHECK(c.a == 255);
    }

    TEST_CASE("8-digit hex (#RRGGBBAA)") {
        sf::Color c;
        REQUIRE(TestGame::parseHexColor("#FF000080", c));
        CHECK(c.r == 255);
        CHECK(c.g == 0);
        CHECK(c.b == 0);
        CHECK(c.a == 128);
    }

    TEST_CASE("Without # prefix") {
        sf::Color c;
        REQUIRE(TestGame::parseHexColor("00FF00", c));
        CHECK(c.r == 0);
        CHECK(c.g == 255);
        CHECK(c.b == 0);
    }

    TEST_CASE("Invalid hex returns false") {
        sf::Color c;
        CHECK(TestGame::parseHexColor("#XYZ", c) == false);
        CHECK(TestGame::parseHexColor("#12345", c) == false);
        CHECK(TestGame::parseHexColor("#1234567890", c) == false);
    }

} // TEST_SUITE parseHexColor
