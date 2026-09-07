#pragma once

#include "SpeciesRegistry.h"

#include <cstdint>
#include <string>
#include <vector>

enum class OrganismControlPolicy
{
    GenesAndEnvironmentOnly
};

struct Genome
{
    // Cladia models DNA as a mutable nucleotide sequence using the same four
    // bases as real DNA. The complementary strand is implied for now.
    std::string bases;
    std::uint32_t chromosomeCount = 1;
};

struct Cell
{
    std::uint32_t id = 0;
    SpeciesId speciesId = SpeciesRegistry::CladiaId;
    float x = 0.5f;
    float y = 0.5f;
    float radius = 0.018f;
    Genome genome;

    // Phylogeny data is stored on every organism even before reproduction and
    // speciation exist, so ancestry does not have to be retrofitted later.
    std::uint32_t parentCellId = 0;
    std::uint32_t lineageRootCellId = 0;
    std::uint32_t generation = 0;

    OrganismControlPolicy controlPolicy = OrganismControlPolicy::GenesAndEnvironmentOnly;
    std::vector<std::uint32_t> components;
};

class CellSystem
{
public:
    void clear() noexcept;
    std::uint32_t spawnCell(SpeciesId speciesId, float x, float y, std::uint32_t parentCellId = 0);

    // Future movement belongs here and must be derived from genes, internal
    // state, and sensed environment. No player movement target API exists.
    void updateAutonomous(float simulationSeconds) noexcept;

    [[nodiscard]] const std::vector<Cell>& cells() const noexcept;
    [[nodiscard]] const Cell* findById(std::uint32_t id) const noexcept;

private:
    [[nodiscard]] static Genome makeFounderGenome(SpeciesId speciesId);
    [[nodiscard]] static Genome inheritGenome(const Genome& parent, std::uint32_t childId);

    std::uint32_t nextId_ = 1;
    std::vector<Cell> cells_;
};
