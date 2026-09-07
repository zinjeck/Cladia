#pragma once

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class AbiogenesisSystem;

struct EmergentCellSnapshot
{
    std::uint32_t id = 0;
    float centerX = 0.5f;
    float centerY = 0.5f;
    float radius = 0.03f;
    std::vector<std::uint32_t> lipidIds;
    std::vector<std::uint32_t> rnaIds;
    std::vector<std::uint32_t> peptideIds;
};

class StableMembraneSystem
{
public:
    void reset();
    void preLifecycle(AbiogenesisSystem& system);
    void postLifecycle(AbiogenesisSystem& system, float dt);

    [[nodiscard]] const std::vector<EmergentCellSnapshot>& cells() const noexcept { return cells_; }

private:
    std::unordered_set<std::uint32_t> stableLipids_;
    std::unordered_map<std::uint32_t, float> membraneAges_;
    std::vector<EmergentCellSnapshot> cells_;

    void stabilizeClosedLoops(AbiogenesisSystem& system, float dt);
};
