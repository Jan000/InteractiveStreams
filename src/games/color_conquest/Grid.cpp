#include "games/color_conquest/Grid.h"
#include <algorithm>

namespace is::games::color_conquest {

void Grid::initialize(int width, int height) {
    m_width  = width;
    m_height = height;
    m_cells.assign(static_cast<size_t>(width) * height, TeamId::None);
}

void Grid::clear() {
    std::fill(m_cells.begin(), m_cells.end(), TeamId::None);
}

TeamId Grid::getCell(int x, int y) const {
    if (!inBounds(x, y)) return TeamId::None;
    return m_cells[index(x, y)];
}

void Grid::setCell(int x, int y, TeamId team) {
    if (!inBounds(x, y)) return;
    m_cells[index(x, y)] = team;
}

int Grid::countCells(TeamId team) const {
    return static_cast<int>(std::count(m_cells.begin(), m_cells.end(), team));
}

bool Grid::inBounds(int x, int y) const {
    return x >= 0 && x < m_width && y >= 0 && y < m_height;
}

std::vector<std::pair<int, int>> Grid::getFrontier(TeamId team, Direction dir) const {
    std::vector<std::pair<int, int>> frontier;
    if (team == TeamId::None) return frontier;

    // Direction offsets
    int dx = 0, dy = 0;
    switch (dir) {
        case Direction::Up:    dy = -1; break;
        case Direction::Down:  dy =  1; break;
        case Direction::Left:  dx = -1; break;
        case Direction::Right: dx =  1; break;
        default: return frontier;
    }

    // Find all cells owned by this team that have a neighbor in the given direction
    // which is NOT owned by this team (i.e., it's the expansion frontier).
    for (int y = 0; y < m_height; y++) {
        for (int x = 0; x < m_width; x++) {
            if (m_cells[index(x, y)] != team) continue;

            int nx = x + dx;
            int ny = y + dy;
            if (!inBounds(nx, ny)) continue;
            if (m_cells[index(nx, ny)] != team) {
                frontier.emplace_back(nx, ny);
            }
        }
    }

    // Remove duplicates (multiple team cells may point to same frontier cell)
    std::sort(frontier.begin(), frontier.end());
    frontier.erase(std::unique(frontier.begin(), frontier.end()), frontier.end());

    return frontier;
}

void Grid::placeStartingPositions() {
    // Each team gets a 3×3 block in a corner
    const int pad = 1;
    const int size = 3;

    // Red: top-left
    for (int y = pad; y < pad + size; y++)
        for (int x = pad; x < pad + size; x++)
            setCell(x, y, TeamId::Red);

    // Blue: top-right
    for (int y = pad; y < pad + size; y++)
        for (int x = m_width - pad - size; x < m_width - pad; x++)
            setCell(x, y, TeamId::Blue);

    // Green: bottom-left
    for (int y = m_height - pad - size; y < m_height - pad; y++)
        for (int x = pad; x < pad + size; x++)
            setCell(x, y, TeamId::Green);

    // Yellow: bottom-right
    for (int y = m_height - pad - size; y < m_height - pad; y++)
        for (int x = m_width - pad - size; x < m_width - pad; x++)
            setCell(x, y, TeamId::Yellow);
}

} // namespace is::games::color_conquest
