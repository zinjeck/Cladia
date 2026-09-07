#pragma once

#include <cstdint>
#include <vector>

enum class OrganismControlPolicy
{
    GenesAndEnvironmentOnly
};

struct Cell
{
    std::uint32_t id = 0;
    float x = 0.5f;
    float y = 0.5f;
    float radius = 0.018f;
    OrganismControlPolicy controlPolicy = OrganismControlPolicy::GenesAndEnvironmentOnly;

    // Intentionally empty for now. Real biological components come later.
    std::vector<std::uint32_t> components;
};

class CellSystem
{
public:
    void clear() noexcept;
    std::uint32_t spawnCell(float x, float y);

    // Future movement belongs here and must be derived from genes, internal
    // state, and sensed environment. No player movement target API exists.
    void updateAutonomous(float simulationSeconds) noexcept;

    [[nodiscard]] const std::vector<Cell>& cells() const noexcept;
    [[nodiscard]] const Cell* findById(std::uint32_t id) const noexcept;

private:
    std::uint32_t nextId_ = 1;
    std::vector<Cell> cells_;
};
