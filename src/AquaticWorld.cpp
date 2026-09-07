#include "AquaticWorld.h"

#include <algorithm>
#include <cmath>

namespace
{
    std::uint32_t hash(std::uint32_t value)
    {
        value ^= value >> 16;
        value *= 0x7feb352du;
        value ^= value >> 15;
        value *= 0x846ca68bu;
        value ^= value >> 16;
        return value;
    }
}

AquaticWorld::AquaticWorld(AquaticWorldDefinition definition)
    : definition_(definition)
{
}

AquaticWorld AquaticWorld::createDefault()
{
    // Deliberately fixed for now. Later world generation can construct this
    // same definition from randomized or user-configured parameters.
    return AquaticWorld(AquaticWorldDefinition{});
}

const AquaticWorldDefinition& AquaticWorld::definition() const noexcept
{
    return definition_;
}

float AquaticWorld::waterVariationAt(int x, int y) const noexcept
{
    const auto ux = static_cast<std::uint32_t>(std::max(x, 0));
    const auto uy = static_cast<std::uint32_t>(std::max(y, 0));
    const std::uint32_t h = hash(definition_.seed ^ (ux * 0x9e3779b9u) ^ (uy * 0x85ebca6bu));
    const float grain = static_cast<float>(h & 0xffu) / 255.0f;
    const float wave = 0.5f + 0.5f * std::sin(static_cast<float>(x) * 0.075f + static_cast<float>(y) * 0.041f);
    return std::clamp(grain * 0.55f + wave * 0.45f, 0.0f, 1.0f);
}
