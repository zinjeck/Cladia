#include "RnaPopulationTuner.h"

#include "AbiogenesisSystem.h"

#include <algorithm>

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
}
