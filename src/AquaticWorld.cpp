#include "AquaticWorld.h"

#include <algorithm>
#include <cmath>

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
    // Broad, low-frequency variation only. The ocean should read as smooth
    // water, not noisy terrain. Future world generation can replace this
    // while preserving the same AquaticWorld interface.
    const float fx = static_cast<float>(x);
    const float fy = static_cast<float>(y);
    const float broadA = 0.5f + 0.5f * std::sin(fx * 0.0105f + fy * 0.0045f);
    const float broadB = 0.5f + 0.5f * std::cos(fx * 0.0035f - fy * 0.0080f + 1.7f);
    const float broadC = 0.5f + 0.5f * std::sin((fx + fy) * 0.0022f + 0.8f);
    return std::clamp(broadA * 0.42f + broadB * 0.33f + broadC * 0.25f, 0.0f, 1.0f);
}
