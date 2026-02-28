// ═══════════════════════════════════════════════════════════════════════════
// Tests: ChatMessage – struct initialization, makeUserId
// ═══════════════════════════════════════════════════════════════════════════
#include <doctest/doctest.h>
#include "platform/ChatMessage.h"

TEST_SUITE("ChatMessage") {

    TEST_CASE("makeUserId combines platform and id") {
        auto uid = is::platform::ChatMessage::makeUserId("twitch", "viewer123");
        CHECK(uid == "twitch:viewer123");

        auto uid2 = is::platform::ChatMessage::makeUserId("youtube", "UCabc");
        CHECK(uid2 == "youtube:UCabc");

        auto uid3 = is::platform::ChatMessage::makeUserId("local", "console");
        CHECK(uid3 == "local:console");
    }

    TEST_CASE("Default values are correct") {
        is::platform::ChatMessage msg;
        CHECK(msg.platformId.empty());
        CHECK(msg.channelId.empty());
        CHECK(msg.userId.empty());
        CHECK(msg.displayName.empty());
        CHECK(msg.text.empty());
        CHECK(msg.isModerator == false);
        CHECK(msg.isSubscriber == false);
        CHECK(msg.timestamp == 0.0);
    }

    TEST_CASE("Fields can be assigned") {
        is::platform::ChatMessage msg;
        msg.platformId = "twitch";
        msg.channelId = "some_channel";
        msg.userId = is::platform::ChatMessage::makeUserId("twitch", "user42");
        msg.displayName = "CoolViewer";
        msg.text = "!join";
        msg.isModerator = true;
        msg.isSubscriber = true;
        msg.timestamp = 1234567890.5;

        CHECK(msg.platformId == "twitch");
        CHECK(msg.channelId == "some_channel");
        CHECK(msg.userId == "twitch:user42");
        CHECK(msg.displayName == "CoolViewer");
        CHECK(msg.text == "!join");
        CHECK(msg.isModerator == true);
        CHECK(msg.isSubscriber == true);
        CHECK(msg.timestamp == doctest::Approx(1234567890.5));
    }

    TEST_CASE("makeUserId handles empty strings") {
        auto uid = is::platform::ChatMessage::makeUserId("", "");
        CHECK(uid == ":");

        auto uid2 = is::platform::ChatMessage::makeUserId("platform", "");
        CHECK(uid2 == "platform:");
    }
}
