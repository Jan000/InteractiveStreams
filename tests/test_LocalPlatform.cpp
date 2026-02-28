// ═══════════════════════════════════════════════════════════════════════════
// Tests: LocalPlatform – message injection, polling, connection
// ═══════════════════════════════════════════════════════════════════════════
#include <doctest/doctest.h>
#include "platform/local/LocalPlatform.h"

TEST_SUITE("LocalPlatform") {

    TEST_CASE("Initial state is disconnected") {
        is::platform::LocalPlatform lp;
        CHECK(lp.isConnected() == false);
        CHECK(lp.id() == "local");
        CHECK(lp.displayName() == "Local (Test)");
    }

    TEST_CASE("Connect and disconnect") {
        is::platform::LocalPlatform lp;
        // Disable console input to avoid blocking on stdin
        lp.configure(nlohmann::json{{"console_input", false}});

        CHECK(lp.connect() == true);
        CHECK(lp.isConnected() == true);

        lp.disconnect();
        CHECK(lp.isConnected() == false);
    }

    TEST_CASE("Inject and poll messages") {
        is::platform::LocalPlatform lp;
        lp.configure(nlohmann::json{{"console_input", false}});
        lp.connect();

        lp.injectMessage("Alice", "!join");
        lp.injectMessage("Bob", "!attack");

        auto msgs = lp.pollMessages();
        REQUIRE(msgs.size() == 2);

        CHECK(msgs[0].displayName == "Alice");
        CHECK(msgs[0].text == "!join");
        CHECK(msgs[0].platformId == "local");
        CHECK(msgs[0].userId == "local:Alice");

        CHECK(msgs[1].displayName == "Bob");
        CHECK(msgs[1].text == "!attack");
        CHECK(msgs[1].userId == "local:Bob");

        lp.disconnect();
    }

    TEST_CASE("Poll returns empty after drain") {
        is::platform::LocalPlatform lp;
        lp.configure(nlohmann::json{{"console_input", false}});
        lp.connect();

        lp.injectMessage("Test", "hello");
        auto msgs1 = lp.pollMessages();
        CHECK(msgs1.size() == 1);

        auto msgs2 = lp.pollMessages();
        CHECK(msgs2.empty());

        lp.disconnect();
    }

    TEST_CASE("sendMessage adds to log") {
        is::platform::LocalPlatform lp;
        lp.configure(nlohmann::json{{"console_input", false}});
        lp.connect();

        lp.sendMessage("Hello from system");
        lp.sendMessage("Another message");

        auto log = lp.getMessageLog();
        REQUIRE(log.size() == 2);
        CHECK(log[0] == "Hello from system");
        CHECK(log[1] == "Another message");

        lp.disconnect();
    }

    TEST_CASE("getStatus returns correct JSON") {
        is::platform::LocalPlatform lp;
        lp.configure(nlohmann::json{{"console_input", false}});
        lp.connect();

        auto status = lp.getStatus();
        CHECK(status["platform"] == "local");
        CHECK(status["displayName"] == "Local (Test)");
        CHECK(status["connected"] == true);

        lp.disconnect();
        auto status2 = lp.getStatus();
        CHECK(status2["connected"] == false);
    }

    TEST_CASE("Messages have valid timestamps") {
        is::platform::LocalPlatform lp;
        lp.configure(nlohmann::json{{"console_input", false}});
        lp.connect();

        lp.injectMessage("TimeTest", "!join");
        auto msgs = lp.pollMessages();
        REQUIRE(msgs.size() == 1);
        CHECK(msgs[0].timestamp > 0.0);

        lp.disconnect();
    }

    TEST_CASE("Multiple rapid injections are ordered") {
        is::platform::LocalPlatform lp;
        lp.configure(nlohmann::json{{"console_input", false}});
        lp.connect();

        for (int i = 0; i < 50; i++) {
            lp.injectMessage("User" + std::to_string(i), "!join");
        }

        auto msgs = lp.pollMessages();
        CHECK(msgs.size() == 50);

        // Check ordering
        for (int i = 0; i < 50; i++) {
            CHECK(msgs[i].displayName == "User" + std::to_string(i));
        }

        lp.disconnect();
    }
}
