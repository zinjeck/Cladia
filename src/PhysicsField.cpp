#include "PhysicsField.h"

#include <algorithm>
#include <cmath>

WaterSample PhysicsField::sample(float x, float y, double simulationSeconds) const noexcept
{
    const float t = static_cast<float>(simulationSeconds * 0.00005);
    const float depth = std::clamp(y, 0.0f, 1.0f);

    WaterSample sample;
    sample.densityKgM3 = 1023.0f + depth * 4.0f;
    sample.currentX = std::sin(x * 11.0f + depth * 4.0f + t) * (0.010f + 0.015f * (1.0f - depth));
    sample.currentY = std::cos(x * 7.0f - depth * 9.0f + t * 0.8f) * 0.0035f;
    sample.turbulenceX = std::sin(x * 41.0f + depth * 53.0f + t * 4.0f) * 0.0045f;
    sample.turbulenceY = std::cos(x * 37.0f - depth * 47.0f + t * 3.0f) * 0.0045f;
    return sample;
}

void PhysicsField::integrate(ParticleBody& body, float dt, double simulationSeconds) const noexcept
{
    if (dt <= 0.0f)
    {
        return;
    }

    WaterSample water = sample(body.x, body.y, simulationSeconds);

    const float densityDelta = (water.densityKgM3 - body.densityKgM3) / 1025.0f;
    const float buoyancyAcceleration = -densityDelta * 0.030f;

    const float targetVX = water.currentX + water.turbulenceX;
    const float targetVY = water.currentY + water.turbulenceY + buoyancyAcceleration;

    const float dragRate = 2.2f + std::clamp(body.radius * 180.0f, 0.0f, 2.5f);
    const float blend = std::clamp(dragRate * dt, 0.0f, 1.0f);
    body.vx += (targetVX - body.vx) * blend;
    body.vy += (targetVY - body.vy) * blend;

    body.x += body.vx * dt;
    body.y += body.vy * dt;

    if (body.x < 0.0f) body.x += 1.0f;
    if (body.x > 1.0f) body.x -= 1.0f;

    if (body.y < 0.002f)
    {
        body.y = 0.002f;
        body.vy = std::abs(body.vy) * 0.15f;
    }
    else if (body.y > 0.998f)
    {
        body.y = 0.998f;
        body.vy = -std::abs(body.vy) * 0.15f;
    }
}
