#include <doctest/doctest.h>
#include "games/color_conquest/Team.h"

using namespace is::games::color_conquest;

TEST_SUITE("Color Conquest - TeamData") {

    TEST_CASE("TeamData default state") {
        TeamData td;
        td.name = "Test";
        td.playerCount = 0;
        td.cellCount = 0;
        td.roundsWon = 0;

        CHECK(td.totalVotes() == 0);
        CHECK(td.topVote() == Direction::Up);  // Default when no votes
    }

    TEST_CASE("TeamData vote tallying") {
        TeamData td;
        td.name = "Red";

        td.votes[static_cast<int>(Direction::Up)]    = 5;
        td.votes[static_cast<int>(Direction::Down)]  = 3;
        td.votes[static_cast<int>(Direction::Left)]  = 1;
        td.votes[static_cast<int>(Direction::Right)] = 7;

        CHECK(td.totalVotes() == 16);
        CHECK(td.topVote() == Direction::Right);
    }

    TEST_CASE("TeamData topVote tie-break (first found wins)") {
        TeamData td;
        td.name = "Blue";

        // Equal votes for Up and Down: Up should win (lower index)
        td.votes[static_cast<int>(Direction::Up)]   = 3;
        td.votes[static_cast<int>(Direction::Down)] = 3;

        CHECK(td.topVote() == Direction::Up);
    }

    TEST_CASE("TeamData clearVotes") {
        TeamData td;
        td.name = "Green";

        td.votes[static_cast<int>(Direction::Up)] = 10;
        td.votes[static_cast<int>(Direction::Right)] = 5;
        CHECK(td.totalVotes() == 15);

        td.clearVotes();
        CHECK(td.totalVotes() == 0);
        for (int d = 0; d < 5; d++) {
            CHECK(td.votes[d] == 0);
        }
    }

    TEST_CASE("TeamData single direction vote") {
        TeamData td;
        td.name = "Yellow";

        td.votes[static_cast<int>(Direction::Left)] = 42;

        CHECK(td.totalVotes() == 42);
        CHECK(td.topVote() == Direction::Left);
    }
}

TEST_SUITE("Color Conquest - TeamId/Direction enums") {

    TEST_CASE("TeamId values") {
        CHECK(static_cast<int>(TeamId::None)   == 0);
        CHECK(static_cast<int>(TeamId::Red)    == 1);
        CHECK(static_cast<int>(TeamId::Blue)   == 2);
        CHECK(static_cast<int>(TeamId::Green)  == 3);
        CHECK(static_cast<int>(TeamId::Yellow) == 4);
    }

    TEST_CASE("Direction values") {
        CHECK(static_cast<int>(Direction::None)  == 0);
        CHECK(static_cast<int>(Direction::Up)    == 1);
        CHECK(static_cast<int>(Direction::Down)  == 2);
        CHECK(static_cast<int>(Direction::Left)  == 3);
        CHECK(static_cast<int>(Direction::Right) == 4);
    }
}
