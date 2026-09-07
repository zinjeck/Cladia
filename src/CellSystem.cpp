#include "CellSystem.h"

#include <algorithm>
#include <random>

void CellSystem::seedPlaceholderOceanCells(std::uint32_t seed, std::size_t count)
{
    cells_.clear();
    cells_.reserve(count);

    std::mt19937 rng(seed ^ 0xC3115EEDu);
    std::uniform_real_distribution<float> positionDistribution(0.04f, 0.96f);
    std::uniform_real_distribution<float> radiusDistribution(0.008f, 0.016f);

    for (std::size_t index = 0; index < count; ++index)
    {
        Cell cell;
        cell.id = static_cast<std::uint32_t>(index + 1u);
        cell.x = positionDistribution(rng);
        cell.y = positionDistribution(rng);
        cell.radius = radiusDistribution(rng);
        // No biological components are created yet.
        cells_.push_back(cell);
    }
}

const std::vector<Cell>& CellSystem::cells() const noexcept
{
    return cells_;
}

const Cell* CellSystem::findById(std::uint32_t id) const noexcept
{
    const auto it = std::find_if(cells_.begin(), cells_.end(), [id](const Cell& cell)
    {
        return cell.id == id;
    });

    return it == cells_.end() ? nullptr : &*it;
}
