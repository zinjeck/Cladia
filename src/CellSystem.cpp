#include "CellSystem.h"

#include <algorithm>
#include <random>

void CellSystem::seedPlaceholderOceanCells(std::uint32_t seed, std::size_t count)
{
    cells_.clear();
    cells_.reserve(count);

    std::mt19937 rng(seed ^ 0xC3115EEDu);
    std::uniform_real_distribution<float> yDistribution(0.08f, 0.92f);
    std::uniform_real_distribution<float> edgeDistribution(0.035f, 0.17f);
    std::uniform_real_distribution<float> jitter(-0.012f, 0.012f);
    std::uniform_real_distribution<float> radiusDistribution(0.000055f, 0.00011f);

    for (std::size_t index = 0; index < count; ++index)
    {
        const bool leftOcean = (index % 2u) == 0u;
        const float edge = edgeDistribution(rng);

        Cell cell;
        cell.id = static_cast<std::uint32_t>(index + 1u);
        cell.x = std::clamp(leftOcean ? edge : 1.0f - edge, 0.01f, 0.99f);
        cell.y = std::clamp(yDistribution(rng) + jitter(rng), 0.02f, 0.98f);
        cell.radius = radiusDistribution(rng);
        // No biological components are created in this pass.
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
