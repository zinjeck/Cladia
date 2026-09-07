#include "RnaPopulationTuner.h"

#include "AbiogenesisSystem.h"

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

        // ProtocellLifecycle already keeps only ~1/5 of fresh vent RNA.
        // Keep only one third of those survivors here, giving an effective
        // fresh-vent survival rate of roughly 1/15 without touching replicated RNA.
        const std::uint32_t ventOrdinal = p.id / 5u;
        if ((ventOrdinal % 3u) != 0u)
        {
            return true;
        }

        // A high stop-codon share keeps spontaneous chains short. This changes
        // only fresh vent material; replicated triplets preserve their copied code.
        if (((ventOrdinal / 3u) % 2u) == 0u)
        {
            p.triplet = "UAA";
            p.stopTriplet = true;
        }

        return false;
    });

    // ATP is intentionally much sparser than before for performance. Only tune
    // brand-new ATP so particles already travelling through the world or being
    // consumed by peptides are not disturbed.
    std::erase_if(particles, [](PrimitiveParticle& p)
    {
        if (p.kind != PrimitiveKind::Atp) return false;
        if (p.ageSeconds > 0.45f || p.lifetimeSeconds < 0.0f) return false;

        // Keep roughly one out of every six fresh ATP particles. This sharply
        // reduces the active entity count while retaining a steady energy supply.
        if ((p.id % 6u) != 0u)
        {
            return true;
        }

        const float id = static_cast<float>(p.id);
        const bool fromVent = p.body.y > 0.80f;

        if (fromVent)
        {
            // Survivors fan out rapidly instead of rising in a narrow plume.
            p.body.vx = std::sin(id * 1.731f) * 0.20f
                      + std::cos(id * 0.617f) * 0.055f;
            p.body.vy = -0.115f - std::abs(std::cos(id * 1.113f)) * 0.095f;
        }
        else
        {
            // Surface-created ATP also spreads laterally so a smaller population
            // can cover more of the upper water column.
            p.body.vx = std::sin(id * 2.173f) * 0.17f;
            p.body.vy = 0.055f + std::abs(std::cos(id * 0.913f)) * 0.065f;
        }

        // Faster travel lets us shorten persistence as another performance guard.
        p.lifetimeSeconds = std::min(p.lifetimeSeconds, 8.0f);
        return false;
    });
}
