#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

struct Cell
{
    std::uint32_t id = 0;
    float x = 0.0f;
    float y = 0.0f;
    float radius = 0.00008f;

    // Intentionally empty for now. Real biological components come later.
    std::vector<std::uint32_t> components;
};

class CellSystem
{
public:
    void seedPlaceholderOceanCells(std::uint32_t seed, std::size_t count = 180);

    [[nodiscard]] const std::vector<Cell>& cells() const noexcept;
    [[nodiscard]] const Cell* findById(std::uint32_t id) const noexcept;

private:
    std::vector<Cell> cells_;
};
