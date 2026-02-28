#pragma once

#include "games/color_conquest/Grid.h"
#include <string>
#include <array>

namespace is::games::color_conquest {

/// Data for a single team – vote counts, scores, player roster.
struct TeamData {
    std::string name;
    int         playerCount  = 0;
    int         cellCount    = 0;
    int         roundsWon    = 0;

    /// Vote tallies for the current round. Index by Direction (1–4).
    std::array<int, 5> votes = {};  // [None, Up, Down, Left, Right]

    void clearVotes() { votes.fill(0); }

    /// Get the direction with the most votes. Tie-break: first found.
    Direction topVote() const {
        int maxVotes = 0;
        Direction best = Direction::Up;
        for (int d = 1; d <= 4; d++) {
            if (votes[d] > maxVotes) {
                maxVotes = votes[d];
                best = static_cast<Direction>(d);
            }
        }
        return best;
    }

    int totalVotes() const {
        int sum = 0;
        for (int d = 1; d <= 4; d++) sum += votes[d];
        return sum;
    }
};

} // namespace is::games::color_conquest
