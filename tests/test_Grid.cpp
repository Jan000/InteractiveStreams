#include <doctest/doctest.h>
#include "games/color_conquest/Grid.h"

using namespace is::games::color_conquest;

TEST_SUITE("Color Conquest - Grid") {

    TEST_CASE("Grid initialization") {
        Grid grid;
        grid.initialize(10, 8);

        CHECK(grid.width() == 10);
        CHECK(grid.height() == 8);
        CHECK(grid.totalCells() == 80);

        // All cells should start as None
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 10; x++) {
                CHECK(grid.getCell(x, y) == TeamId::None);
            }
        }
    }

    TEST_CASE("Grid set and get cell") {
        Grid grid;
        grid.initialize(5, 5);

        grid.setCell(2, 3, TeamId::Red);
        CHECK(grid.getCell(2, 3) == TeamId::Red);

        grid.setCell(0, 0, TeamId::Blue);
        CHECK(grid.getCell(0, 0) == TeamId::Blue);

        grid.setCell(4, 4, TeamId::Green);
        CHECK(grid.getCell(4, 4) == TeamId::Green);

        // Other cells remain None
        CHECK(grid.getCell(1, 1) == TeamId::None);
    }

    TEST_CASE("Grid out-of-bounds returns None") {
        Grid grid;
        grid.initialize(5, 5);
        grid.setCell(2, 2, TeamId::Red);

        CHECK(grid.getCell(-1, 0) == TeamId::None);
        CHECK(grid.getCell(0, -1) == TeamId::None);
        CHECK(grid.getCell(5, 0) == TeamId::None);
        CHECK(grid.getCell(0, 5) == TeamId::None);
        CHECK(grid.getCell(100, 100) == TeamId::None);
    }

    TEST_CASE("Grid out-of-bounds setCell is ignored") {
        Grid grid;
        grid.initialize(5, 5);

        // Should not crash
        grid.setCell(-1, 0, TeamId::Red);
        grid.setCell(5, 0, TeamId::Red);
        grid.setCell(0, -1, TeamId::Red);
        grid.setCell(0, 5, TeamId::Red);

        // All cells still None
        CHECK(grid.countCells(TeamId::Red) == 0);
    }

    TEST_CASE("Grid inBounds check") {
        Grid grid;
        grid.initialize(10, 8);

        CHECK(grid.inBounds(0, 0));
        CHECK(grid.inBounds(9, 7));
        CHECK(grid.inBounds(5, 4));

        CHECK_FALSE(grid.inBounds(-1, 0));
        CHECK_FALSE(grid.inBounds(0, -1));
        CHECK_FALSE(grid.inBounds(10, 0));
        CHECK_FALSE(grid.inBounds(0, 8));
    }

    TEST_CASE("Grid countCells") {
        Grid grid;
        grid.initialize(5, 5);

        CHECK(grid.countCells(TeamId::Red) == 0);
        CHECK(grid.countCells(TeamId::Blue) == 0);

        grid.setCell(0, 0, TeamId::Red);
        grid.setCell(1, 0, TeamId::Red);
        grid.setCell(2, 0, TeamId::Red);
        grid.setCell(0, 1, TeamId::Blue);
        grid.setCell(1, 1, TeamId::Blue);

        CHECK(grid.countCells(TeamId::Red) == 3);
        CHECK(grid.countCells(TeamId::Blue) == 2);
        CHECK(grid.countCells(TeamId::Green) == 0);
        CHECK(grid.countCells(TeamId::None) == 20);  // 25 - 5 occupied
    }

    TEST_CASE("Grid clear") {
        Grid grid;
        grid.initialize(5, 5);

        grid.setCell(2, 2, TeamId::Red);
        grid.setCell(3, 3, TeamId::Blue);
        CHECK(grid.countCells(TeamId::Red) == 1);
        CHECK(grid.countCells(TeamId::Blue) == 1);

        grid.clear();

        CHECK(grid.countCells(TeamId::Red) == 0);
        CHECK(grid.countCells(TeamId::Blue) == 0);
        CHECK(grid.getCell(2, 2) == TeamId::None);
    }

    TEST_CASE("Grid placeStartingPositions") {
        Grid grid;
        grid.initialize(24, 40);

        grid.placeStartingPositions();

        // Each team should have a 3x3 block = 9 cells
        CHECK(grid.countCells(TeamId::Red) == 9);
        CHECK(grid.countCells(TeamId::Blue) == 9);
        CHECK(grid.countCells(TeamId::Green) == 9);
        CHECK(grid.countCells(TeamId::Yellow) == 9);

        // Red should be in top-left corner (pad=1)
        CHECK(grid.getCell(1, 1) == TeamId::Red);
        CHECK(grid.getCell(2, 2) == TeamId::Red);
        CHECK(grid.getCell(3, 3) == TeamId::Red);

        // Blue should be in top-right corner
        int bx = 24 - 1 - 3;  // 20
        CHECK(grid.getCell(bx, 1) == TeamId::Blue);

        // Green should be in bottom-left corner
        int gy = 40 - 1 - 3;  // 36
        CHECK(grid.getCell(1, gy) == TeamId::Green);

        // Yellow should be in bottom-right corner
        CHECK(grid.getCell(bx, gy) == TeamId::Yellow);
    }

    TEST_CASE("Grid getFrontier - expand right") {
        Grid grid;
        grid.initialize(10, 5);

        // Place a 2x2 red block at (1,1)
        grid.setCell(1, 1, TeamId::Red);
        grid.setCell(2, 1, TeamId::Red);
        grid.setCell(1, 2, TeamId::Red);
        grid.setCell(2, 2, TeamId::Red);

        auto frontier = grid.getFrontier(TeamId::Red, Direction::Right);

        // Should expand to the right: (3,1) and (3,2)
        CHECK(frontier.size() == 2);
        bool has_3_1 = false, has_3_2 = false;
        for (auto [x, y] : frontier) {
            if (x == 3 && y == 1) has_3_1 = true;
            if (x == 3 && y == 2) has_3_2 = true;
        }
        CHECK(has_3_1);
        CHECK(has_3_2);
    }

    TEST_CASE("Grid getFrontier - expand down") {
        Grid grid;
        grid.initialize(10, 10);

        grid.setCell(3, 3, TeamId::Blue);
        grid.setCell(4, 3, TeamId::Blue);

        auto frontier = grid.getFrontier(TeamId::Blue, Direction::Down);

        // Should expand downward: (3,4) and (4,4)
        CHECK(frontier.size() == 2);
        bool has_3_4 = false, has_4_4 = false;
        for (auto [x, y] : frontier) {
            if (x == 3 && y == 4) has_3_4 = true;
            if (x == 4 && y == 4) has_4_4 = true;
        }
        CHECK(has_3_4);
        CHECK(has_4_4);
    }

    TEST_CASE("Grid getFrontier - edge of grid clipped") {
        Grid grid;
        grid.initialize(5, 5);

        // Place cells at right edge
        grid.setCell(4, 2, TeamId::Green);

        auto frontier = grid.getFrontier(TeamId::Green, Direction::Right);

        // Can't expand right at x=4 (max is 4), should be empty
        CHECK(frontier.empty());
    }

    TEST_CASE("Grid getFrontier - no duplicate cells") {
        Grid grid;
        grid.initialize(10, 10);

        // L-shaped territory: two cells share the same frontier cell when expanding right
        grid.setCell(3, 3, TeamId::Red);
        grid.setCell(3, 4, TeamId::Red);

        auto frontier = grid.getFrontier(TeamId::Red, Direction::Right);

        // (4,3) and (4,4) should appear — no duplicates
        CHECK(frontier.size() == 2);
    }
}
