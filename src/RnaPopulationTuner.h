#pragma once

#include <cstdint>

class AbiogenesisSystem;

class RnaPopulationTuner
{
public:
    void reset() noexcept { frameCounter_ = 0; }
    void update(AbiogenesisSystem& system);

private:
    std::uint64_t frameCounter_ = 0;
};
