// ═══════════════════════════════════════════════════════════════════════════
// Tests: GameRegistry – registration, listing, creation, duplicates
// ═══════════════════════════════════════════════════════════════════════════
#include <doctest/doctest.h>
#include "games/GameRegistry.h"
#include "games/IGame.h"

namespace {

/// Minimal stub game for testing the registry.
class StubGame : public is::games::IGame {
public:
    explicit StubGame(std::string id = "stub") : m_id(std::move(id)) {}

    std::string id() const override { return m_id; }
    std::string displayName() const override { return "Stub Game"; }
    std::string description() const override { return "A test stub"; }
    void initialize() override {}
    void shutdown() override {}
    void onChatMessage(const is::platform::ChatMessage&) override {}
    void update(double) override {}
    void render(sf::RenderTarget&, double) override {}
    bool isRoundComplete() const override { return false; }
    bool isGameOver() const override { return false; }
    nlohmann::json getState() const override { return {}; }
    nlohmann::json getCommands() const override { return {}; }

private:
    std::string m_id;
};

} // anonymous namespace

TEST_SUITE("GameRegistry") {

    TEST_CASE("Instance is a singleton") {
        auto& r1 = is::games::GameRegistry::instance();
        auto& r2 = is::games::GameRegistry::instance();
        CHECK(&r1 == &r2);
    }

    TEST_CASE("Register and create a game") {
        auto& reg = is::games::GameRegistry::instance();
        reg.registerGame("test_stub_1", []() {
            return std::make_unique<StubGame>("test_stub_1");
        });

        CHECK(reg.has("test_stub_1") == true);

        auto game = reg.create("test_stub_1");
        REQUIRE(game != nullptr);
        CHECK(game->id() == "test_stub_1");
        CHECK(game->displayName() == "Stub Game");
    }

    TEST_CASE("Create returns nullptr for unknown game") {
        auto& reg = is::games::GameRegistry::instance();
        auto game = reg.create("nonexistent_game_xyz");
        CHECK(game == nullptr);
    }

    TEST_CASE("has() returns false for unknown game") {
        auto& reg = is::games::GameRegistry::instance();
        CHECK(reg.has("unknown_game_abc") == false);
    }

    TEST_CASE("list() includes registered games") {
        auto& reg = is::games::GameRegistry::instance();
        reg.registerGame("test_stub_2", []() {
            return std::make_unique<StubGame>("test_stub_2");
        });

        auto games = reg.list();
        bool found = false;
        for (const auto& name : games) {
            if (name == "test_stub_2") found = true;
        }
        CHECK(found == true);
    }

    TEST_CASE("Re-registering overwrites factory") {
        auto& reg = is::games::GameRegistry::instance();

        int version = 0;
        reg.registerGame("test_overwrite", [&version]() {
            version = 1;
            return std::make_unique<StubGame>("test_overwrite");
        });

        auto g1 = reg.create("test_overwrite");
        CHECK(version == 1);

        reg.registerGame("test_overwrite", [&version]() {
            version = 2;
            return std::make_unique<StubGame>("test_overwrite");
        });

        auto g2 = reg.create("test_overwrite");
        CHECK(version == 2);
    }
}
