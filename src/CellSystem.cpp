#include "CellSystem.h"

#include <algorithm>
#include <array>

namespace
{
    constexpr std::array<char, 4> Bases = {'A', 'C', 'G', 'T'};
}

void CellSystem::clear() noexcept
{
    cells_.clear();
    nextId_ = 1;
}

Genome CellSystem::makeFounderGenome(SpeciesId speciesId)
{
    Genome genome;
    genome.chromosomeCount = 1;
    genome.bases.reserve(192);

    std::uint32_t state = 0x9E3779B9u ^ (speciesId * 0x85EBCA6Bu);
    for (int i = 0; i < 192; ++i)
    {
        state ^= state >> 16;
        state *= 0x7FEB352Du;
        state ^= state >> 15;
        state *= 0x846CA68Bu;
        state ^= state >> 16;
        genome.bases.push_back(Bases[state & 3u]);
        state += static_cast<std::uint32_t>(i + 1) * 0x9E3779B9u;
    }

    return genome;
}

Genome CellSystem::inheritGenome(const Genome& parent, std::uint32_t childId)
{
    Genome child = parent;
    if (child.bases.empty())
    {
        return child;
    }

    // Reserved mutation seam. A child currently receives one deterministic
    // point mutation so inheritance already behaves as sequence inheritance,
    // not as a species label. Reproduction itself is not implemented yet.
    const std::size_t index = static_cast<std::size_t>((childId * 2654435761u) % child.bases.size());
    char& base = child.bases[index];
    for (char candidate : Bases)
    {
        if (candidate != base)
        {
            base = candidate;
            break;
        }
    }
    return child;
}

std::uint32_t CellSystem::spawnCell(SpeciesId speciesId, float x, float y, std::uint32_t parentCellId)
{
    Cell cell;
    cell.id = nextId_++;
    cell.speciesId = speciesId;
    cell.x = std::clamp(x, 0.03f, 0.97f);
    cell.y = std::clamp(y, 0.005f, 0.995f);
    cell.radius = 0.012f;
    cell.controlPolicy = OrganismControlPolicy::GenesAndEnvironmentOnly;
    cell.parentCellId = parentCellId;

    if (const Cell* parent = findById(parentCellId))
    {
        cell.genome = inheritGenome(parent->genome, cell.id);
        cell.generation = parent->generation + 1;
        cell.lineageRootCellId = parent->lineageRootCellId == 0 ? parent->id : parent->lineageRootCellId;
    }
    else
    {
        cell.genome = makeFounderGenome(speciesId);
        cell.generation = 0;
        cell.lineageRootCellId = cell.id;
        cell.parentCellId = 0;
    }

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
