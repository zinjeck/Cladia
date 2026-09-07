#pragma once

#include <cstddef>

class AbiogenesisSystem;

class EntityBudgetSystem
{
public:
    void update(AbiogenesisSystem& system);

    [[nodiscard]] static constexpr std::size_t maxParticles() noexcept { return 420; }
    [[nodiscard]] static constexpr std::size_t maxEnergyRays() noexcept { return 160; }
};
