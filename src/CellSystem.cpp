#include "CellSystem.h"

#include <algorithm>
#include <sstream>

void CellSystem::clear() noexcept
{
    cells_.clear();
    nextId_ = 1;
}

std::uint32_t CellSystem::spawnCell(SpeciesId speciesId, float x, float y)
{
    Cell cell;
    cell.id = nextId_++;
    cell.speciesId = speciesId;
    cell.x = std::clamp(x, 0.03f, 0.97f);
    cell.y = std::clamp(y, 0.08f, 0.97f);
    cell.radius = 0.018f;
    cell.controlPolicy = OrganismControlPolicy::GenesAndEnvironmentOnly;

    std::ostringstream dna;
    dna << "CLD-" << speciesId << '-' << cell.id << " / UNSEQUENCED";
    cell.dna = dna.str();

    cells_.push_back(cell);
    return cell.id;
}

void CellSystem::updateAutonomous(float simulationSeconds) noexcept
{
    (void)simulationSeconds;
    // Intentionally no movement yet. Future organism motion must come from
    // genes and environmental response, never direct player commands.
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
