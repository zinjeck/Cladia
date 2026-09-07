#include "RnaPopulationTuner.h"

#include "AbiogenesisSystem.h"
#include "EntityBudgetSystem.h"

#include <algorithm>
#include <cmath>

void RnaPopulationTuner::update(AbiogenesisSystem& system)
{
    auto& particles = system.mutableParticles();

    std::erase_if(particles, [](PrimitiveParticle& p)
    {
        if (p.kind != PrimitiveKind::RnaTriplet) return false;
        if (p.ageSeconds > 0.55f || p.body.y < 0.88f) return false;
        if (p.frontLink != 0 || p.backLink != 0 || p.templatePartnerId != 0 || p.replicaTripletId != 0) return false;

        const std::uint32_t ventOrdinal = p.id / 5u;
        if ((ventOrdinal % 3u) != 0u)
        {
            return true;
        }

        if (((ventOrdinal / 3u) % 2u) == 0u)
        {
            p.triplet = "UAA";
            p.stopTriplet = true;
        }

        return false;
    });

    std::erase_if(particles, [](PrimitiveParticle& p)
    {
        if (p.kind != PrimitiveKind::Atp) return false;
        if (p.ageSeconds > 0.45f || p.lifetimeSeconds < 0.0f) return false;

        if ((p.id % 6u) != 0u)
        {
            return true;
        }

        const float id = static_cast<float>(p.id);
        const bool fromVent = p.body.y > 0.80f;

        if (fromVent)
        {
            p.body.vx = std::sin(id * 1.731f) * 0.20f
                      + std::cos(id * 0.617f) * 0.055f;
            p.body.vy = -0.115f - std::abs(std::cos(id * 1.113f)) * 0.095f;
        }
        else
        {
            p.body.vx = std::sin(id * 2.173f) * 0.17f;
            p.body.vy = 0.055f + std::abs(std::cos(id * 0.913f)) * 0.065f;
        }

        p.lifetimeSeconds = std::min(p.lifetimeSeconds, 8.0f);
        return false;
    });

    // Water drag is allowed to shape ATP motion, but ATP may not stall. A
    // deterministic minimum cruise speed lets each survivor cross the map
    // during its short lifetime instead of stopping halfway.
    for (PrimitiveParticle& p : particles)
    {
        if (p.kind != PrimitiveKind::Atp || p.lifetimeSeconds < 0.0f) continue;

        const float id = static_cast<float>(p.id);
        const float minimumCruise = 0.145f + std::abs(std::sin(id * 0.413f)) * 0.035f;
        if (std::abs(p.body.vx) < minimumCruise)
        {
            float direction = 0.0f;
            if (std::abs(p.body.vx) > 0.004f)
                direction = p.body.vx > 0.0f ? 1.0f : -1.0f;
            else
                direction = (p.id & 1u) == 0u ? 1.0f : -1.0f;
            p.body.vx = direction * minimumCruise;
        }
    }

    // The budget runs every frame after the fresh-material tuning above. This
    // gives the simulation a hard population ceiling even if vents emit a burst.
    EntityBudgetSystem{}.update(system);
}
