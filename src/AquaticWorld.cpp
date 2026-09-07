#include "AquaticWorld.h"

#include <algorithm>
#include <cmath>

AquaticWorld::AquaticWorld(AquaticWorldDefinition definition)
    : definition_(definition)
{
}

AquaticWorld AquaticWorld::createDefault()
{
    // Fixed world for now. A future generator can construct the same
    // definition from randomized or user-configured values.
    return AquaticWorld(AquaticWorldDefinition{});
}

const AquaticWorldDefinition& AquaticWorld::definition() const noexcept
{
    return definition_;
}

float AquaticWorld::waterVariationAt(int x, int y) const noexcept
{
    const float fx = static_cast<float>(x);
    const float fy = static_cast<float>(y);
    const float broadA = 0.5f + 0.5f * std::sin(fx * 0.0035f + fy * 0.0012f);
    const float broadB = 0.5f + 0.5f * std::cos(fx * 0.0015f - fy * 0.0020f + 1.7f);
    const float broadC = 0.5f + 0.5f * std::sin((fx + fy) * 0.0008f + 0.8f);
    return std::clamp(broadA * 0.42f + broadB * 0.33f + broadC * 0.25f, 0.0f, 1.0f);
}

EnvironmentSample AquaticWorld::environmentAt(float worldX, float oceanY, float surfaceSolarEnergy) const noexcept
{
    const float depth01 = std::clamp(oceanY, 0.0f, 1.0f);
    const float depthMeters = depth01 * definition_.oceanDepthMeters;

    // A simple thermocline-style curve: warmest near the surface, then
    // increasingly cold with depth. This is intentionally deterministic.
    const float temperatureBlend = std::pow(depth01, 0.58f);
    const float temperature = definition_.surfaceTemperatureC
        + (definition_.bottomTemperatureC - definition_.surfaceTemperatureC) * temperatureBlend;

    // Approximate seawater pressure increase: roughly one atmosphere per 10 m.
    const float pressure = 1.0f + depthMeters / 10.0f;

    // Light attenuates exponentially with depth. This is not a full spectral
    // ocean optics model yet, but preserves the real-world direction.
    const float lightTransmission = std::exp(-depthMeters / 180.0f);
    const float sunlight = std::clamp(surfaceSolarEnergy * lightTransmission, 0.0f, surfaceSolarEnergy);

    const float horizontalVariation = 0.5f + 0.5f * std::sin(worldX * 9.0f + depth01 * 4.0f);
    const float nutrient = std::clamp(
        definition_.nutrientBaseline + depth01 * 0.28f + (horizontalVariation - 0.5f) * 0.08f,
        0.0f,
        1.0f);

    return EnvironmentSample{
        depthMeters,
        temperature,
        pressure,
        definition_.salinityPpt,
        nutrient,
        sunlight,
        sunlight};
}
