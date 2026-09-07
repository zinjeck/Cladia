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
        if ((ventOrdinal % 3u) != 0u) return true;

        if (((ventOrdinal / 3u) % 2u) == 0u)
        {
            p.triplet = "UAA";
            p.stopTriplet = true;
        }
        return false;
    });

    // ATP now represents sparse vent-driven energy packets. Surface-created ATP
    // is removed so the visible flow is unambiguously vent -> upper ocean.
    std::erase_if(particles, [](PrimitiveParticle& p)
    {
        if (p.kind != PrimitiveKind::Atp) return false;
        if (p.ageSeconds > 0.45f || p.lifetimeSeconds < 0.0f) return false;

        const bool fromVent = p.body.y > 0.80f;
        if (!fromVent) return true;

        // Keep only a small fraction. Each survivor travels much farther, so
        // performance improves without making peptide charging impossible.
        if ((p.id % 8u) != 0u) return true;

        const float id = static_cast<float>(p.id);
        p.body.vx = std::sin(id * 1.731f) * 0.085f
                  + std::cos(id * 0.617f) * 0.035f;
        p.body.vy = -0.22f - std::abs(std::cos(id * 1.113f)) * 0.075f;
        p.lifetimeSeconds = std::min(p.lifetimeSeconds, 7.5f);
        return false;
    });

    // Water drag can bend the trajectory, but a vent ATP packet keeps rising
    // through the water column. It disappears just below the surface instead
    // of settling in the lower ocean or cruising sideways forever.
    for (PrimitiveParticle& p : particles)
    {
        if (p.kind != PrimitiveKind::Atp || p.lifetimeSeconds < 0.0f) continue;

        if (p.body.y <= 0.04f)
        {
            p.lifetimeSeconds = -1.0f;
            continue;
        }

        const float id = static_cast<float>(p.id);
        const float minimumRise = 0.155f + std::abs(std::sin(id * 0.413f)) * 0.035f;
        p.body.vy = std::min(p.body.vy, -minimumRise);

        // Broad but controlled lateral wandering distributes the sparse ATP
        // through different parts of the ocean on its climb.
        p.body.vx += std::sin(id * 0.73f + p.ageSeconds * 2.4f) * 0.0045f;
        p.body.vx = std::clamp(p.body.vx, -0.12f, 0.12f);
    }

    EntityBudgetSystem{}.update(system);
}
