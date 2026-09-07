#pragma once

#include <cmath>

namespace WorldTopology
{
    inline float wrap01(float x) noexcept
    {
        x = std::fmod(x, 1.0f);
        if (x < 0.0f) x += 1.0f;
        return x;
    }

    inline float deltaX(float fromX, float toX) noexcept
    {
        float dx = toX - fromX;
        if (dx > 0.5f) dx -= 1.0f;
        else if (dx < -0.5f) dx += 1.0f;
        return dx;
    }

    inline float distanceSquared(float ax, float ay, float bx, float by) noexcept
    {
        const float dx = deltaX(ax, bx);
        const float dy = by - ay;
        return dx * dx + dy * dy;
    }

    inline float unwrapNear(float referenceX, float x) noexcept
    {
        return referenceX + deltaX(referenceX, x);
    }
}
