#pragma once

#include <cstdint>

struct AquaticWorldDefinition
{
    std::uint32_t seed = 0xC1AD1A01u;
    int logicalWidth = 320;
    int logicalHeight = 180;
    float temperatureC = 24.0f;
    float salinityPpt = 35.0f;
    float nutrientBaseline = 0.50f;
};

class AquaticWorld
{
public:
    static AquaticWorld createDefault();

    [[nodiscard]] const AquaticWorldDefinition& definition() const noexcept;
    [[nodiscard]] float waterVariationAt(int x, int y) const noexcept;

private:
    explicit AquaticWorld(AquaticWorldDefinition definition);

    AquaticWorldDefinition definition_{};
};
