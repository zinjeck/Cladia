#pragma once

struct ParticleBody
{
    float x = 0.5f;
    float y = 0.5f;
    float vx = 0.0f;
    float vy = 0.0f;
    float densityKgM3 = 1000.0f;
    float radius = 0.004f;
};

struct WaterSample
{
    float densityKgM3 = 1025.0f;
    float currentX = 0.0f;
    float currentY = 0.0f;
    float turbulenceX = 0.0f;
    float turbulenceY = 0.0f;
};

class PhysicsField
{
public:
    [[nodiscard]] WaterSample sample(float x, float y, double simulationSeconds) const noexcept;
    void integrate(ParticleBody& body, float dt, double simulationSeconds) const noexcept;
};
