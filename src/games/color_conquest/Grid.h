#pragma once

#include <vector>
#include <cstdint>

namespace is::games::color_conquest {

/// Team identifiers (0-3 for the four teams, None for unclaimed cells).
enum class TeamId : uint8_t {
    None  = 0,
    Red   = 1,
    Blue  = 2,
    Green = 3,
    Yellow = 4
};

/// Cardinal directions for voting.
enum class Direction : uint8_t {
    None  = 0,
    Up    = 1,
    Down  = 2,
    Left  = 3,
    Right = 4
};

/// The game grid – a 2D array of cells, each owned by a team (or unclaimed).
/// Designed for efficient batch operations: expansion, counting, rendering.
class Grid {
public:
    Grid() = default;

    /// Initialize grid with given dimensions. All cells start as None.
    void initialize(int width, int height);

    /// Reset all cells to None.
    void clear();

    /// Get/set cell ownership.
    TeamId getCell(int x, int y) const;
    void setCell(int x, int y, TeamId team);

    /// Grid dimensions.
    int width() const { return m_width; }
    int height() const { return m_height; }

    /// Count cells owned by a specific team. O(grid_size).
    int countCells(TeamId team) const;

    /// Get total cell count.
    int totalCells() const { return m_width * m_height; }

    /// Check if coordinates are in bounds.
    bool inBounds(int x, int y) const;

    /// Get the frontier cells for a team in a given direction.
    /// Returns list of (x, y) pairs that the team would expand into.
    /// Only expands into unclaimed or enemy cells adjacent to team's territory.
    std::vector<std::pair<int, int>> getFrontier(TeamId team, Direction dir) const;

    /// Place team starting positions. Each team gets a corner cluster.
    void placeStartingPositions();

private:
    int index(int x, int y) const { return y * m_width + x; }

    int m_width  = 0;
    int m_height = 0;
    std::vector<TeamId> m_cells;
};

} // namespace is::games::color_conquest
