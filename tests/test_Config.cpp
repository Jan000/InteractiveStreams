// ═══════════════════════════════════════════════════════════════════════════
// Tests: Config – dotted-key navigation, get/set, defaults
// ═══════════════════════════════════════════════════════════════════════════
#include <doctest/doctest.h>
#include "core/Config.h"
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

/// Helper: write a temp JSON config file and return its path.
static std::string writeTempConfig(const std::string& content) {
    auto path = (fs::temp_directory_path() / "is_test_config.json").string();
    std::ofstream f(path);
    f << content;
    f.close();
    return path;
}

TEST_SUITE("Config") {

    TEST_CASE("Load simple values") {
        auto path = writeTempConfig(R"({
            "rendering": { "width": 1080, "height": 1920 },
            "application": { "name": "TestApp" }
        })");

        is::core::Config cfg(path);

        CHECK(cfg.get<int>("rendering.width") == 1080);
        CHECK(cfg.get<int>("rendering.height") == 1920);
        CHECK(cfg.get<std::string>("application.name") == "TestApp");

        fs::remove(path);
    }

    TEST_CASE("Default value for missing key") {
        auto path = writeTempConfig(R"({"a": 1})");
        is::core::Config cfg(path);

        CHECK(cfg.get<int>("missing.key", 42) == 42);
        CHECK(cfg.get<std::string>("also.missing", "default") == "default");
        CHECK(cfg.get<bool>("b", true) == true);

        fs::remove(path);
    }

    TEST_CASE("Set creates nested keys") {
        auto path = writeTempConfig(R"({})");
        is::core::Config cfg(path);

        cfg.set("deeply.nested.value", 123);
        CHECK(cfg.get<int>("deeply.nested.value") == 123);

        cfg.set("root_key", std::string("hello"));
        CHECK(cfg.get<std::string>("root_key") == "hello");

        fs::remove(path);
    }

    TEST_CASE("Reload picks up file changes") {
        auto path = writeTempConfig(R"({"x": 1})");
        is::core::Config cfg(path);
        CHECK(cfg.get<int>("x") == 1);

        // Overwrite file
        {
            std::ofstream f(path);
            f << R"({"x": 99})";
        }
        cfg.reload();
        CHECK(cfg.get<int>("x") == 99);

        fs::remove(path);
    }

    TEST_CASE("Save and reload round-trip") {
        auto path = writeTempConfig(R"({})");
        is::core::Config cfg(path);

        cfg.set("test.value", 42);
        cfg.set("test.name", std::string("foo"));
        cfg.save();

        is::core::Config cfg2(path);
        CHECK(cfg2.get<int>("test.value") == 42);
        CHECK(cfg2.get<std::string>("test.name") == "foo");

        fs::remove(path);
    }

    TEST_CASE("Missing file uses empty defaults") {
        is::core::Config cfg("__nonexistent_file_12345.json");
        CHECK(cfg.get<int>("anything", -1) == -1);
    }

    TEST_CASE("Malformed JSON uses empty defaults") {
        auto path = writeTempConfig("{ not json at all }}}");
        is::core::Config cfg(path);
        CHECK(cfg.get<int>("key", 7) == 7);
        fs::remove(path);
    }
}
