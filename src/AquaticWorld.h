#pragma once

#include <cstdint>

struct AquaticWorldDefinition
{
    std::uint32_t seed = 0xC1AD1A01u;
    int logicalWidth = 1600;
    int logicalHeight = 2200;
    float oceanDepthMeters = 6000.0f;
    float surfaceTemperatureC = 24.0f;
    float bottomTemperatureC = 2.0f;
    float salinityPpt = 35.0f;
    float nutrientBaseline = 0.50f;
};

struct EnvironmentSample
{
    float depthMeters = 0.0f;
    float temperatureC = 24.0f;
    float pressureAtm = 1.0f;
    float salinityPpt = 35.0f;
    float nutrientLevel = 0.50f;
    float sunlight = 0.0f;
    float solarEnergy = 0.0f;
};

class AquaticWorld
{
public:
    static AquaticWorld createDefault();

    [[nodiscard]] const AquaticWorldDefinition& definition() const noexcept;
    [[nodiscard]] float waterVariationAt(int x, int y) const noexcept;
    [[nodiscard]] EnvironmentSample environmentAt(float worldX, float oceanY, float surfaceSolarEnergy) const noexcept;

private:
    explicit AquaticWorld(AquaticWorldDefinition definition);

    AquaticWorldDefinition definition_{};
};
