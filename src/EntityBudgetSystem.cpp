#include "EntityBudgetSystem.h"

#include "AbiogenesisSystem.h"

#include <algorithm>
#include <cstdint>
#include <unordered_set>
#include <vector>

namespace
{
    constexpr std::size_t MaxRna = 96;
    constexpr std::size_t MaxAtp = 48;
    constexpr std::size_t MaxLipids = 210;
    constexpr std::size_t MaxPeptides = 84;

    bool protectedParticle(const PrimitiveParticle& p)
    {
        if (p.inProtoCell) return true;
        if (p.stableMembrane) return true;
        if (p.replicationComplete) return true;
        if (p.templatePartnerId != 0 || p.replicaTripletId != 0) return true;
        return false;
    }

    int retentionPriority(const PrimitiveParticle& p)
    {
        if (p.inProtoCell) return 100;
        if (p.replicationComplete || p.templatePartnerId != 0 || p.replicaTripletId != 0) return 90;
        if (p.stableMembrane) return 80;

        switch (p.kind)
        {
        case PrimitiveKind::RnaTriplet:
            return (p.frontLink != 0 || p.backLink != 0) ? 55 : 10;
        case PrimitiveKind::Peptide:
            return (p.excited || p.readingTriplet != 0) ? 45 : 25;
        case PrimitiveKind::Lipid:
            return p.inClosedLipidLoop ? 50 : 20;
        case PrimitiveKind::Atp:
            return 5;
        }
        return 0;
    }

    void trimKind(std::vector<PrimitiveParticle>& particles, PrimitiveKind kind, std::size_t cap)
    {
        std::vector<const PrimitiveParticle*> ofKind;
        for (const PrimitiveParticle& p : particles)
            if (p.kind == kind) ofKind.push_back(&p);
        if (ofKind.size() <= cap) return;

        std::sort(ofKind.begin(), ofKind.end(), [](const PrimitiveParticle* a, const PrimitiveParticle* b)
        {
            const int pa = retentionPriority(*a);
            const int pb = retentionPriority(*b);
            if (pa != pb) return pa < pb;
            return a->id > b->id; // newest loose vent material is discarded first
        });

        std::unordered_set<std::uint32_t> removeIds;
        const std::size_t removeCount = ofKind.size() - cap;
        for (std::size_t i = 0; i < removeCount; ++i)
        {
            // Per-kind caps may remove non-protocell material, including linked
            // chains if a flood becomes extreme, but never active protocell contents.
            if (!ofKind[i]->inProtoCell) removeIds.insert(ofKind[i]->id);
        }

        std::erase_if(particles, [&removeIds](const PrimitiveParticle& p)
        {
            return removeIds.contains(p.id);
        });
    }
}

void EntityBudgetSystem::update(AbiogenesisSystem& system)
{
    auto& particles = system.mutableParticles();

    trimKind(particles, PrimitiveKind::RnaTriplet, MaxRna);
    trimKind(particles, PrimitiveKind::Atp, MaxAtp);
    trimKind(particles, PrimitiveKind::Lipid, MaxLipids);
    trimKind(particles, PrimitiveKind::Peptide, MaxPeptides);

    if (particles.size() > maxParticles())
    {
        std::vector<const PrimitiveParticle*> candidates;
        candidates.reserve(particles.size());
        for (const PrimitiveParticle& p : particles)
            if (!p.inProtoCell) candidates.push_back(&p);

        std::sort(candidates.begin(), candidates.end(), [](const PrimitiveParticle* a, const PrimitiveParticle* b)
        {
            const int pa = retentionPriority(*a);
            const int pb = retentionPriority(*b);
            if (pa != pb) return pa < pb;
            return a->id > b->id;
        });

        std::unordered_set<std::uint32_t> removeIds;
        std::size_t need = particles.size() - maxParticles();
        for (const PrimitiveParticle* p : candidates)
        {
            if (need == 0) break;
            removeIds.insert(p->id);
            --need;
        }

        std::erase_if(particles, [&removeIds](const PrimitiveParticle& p)
        {
            return removeIds.contains(p.id);
        });
    }

    auto& rays = system.mutableEnergyRays();
    if (rays.size() > maxEnergyRays())
    {
        // Keep the newest visual energy rays. These are presentation-only and
        // do not need to accumulate indefinitely.
        rays.erase(rays.begin(), rays.begin() + static_cast<std::ptrdiff_t>(rays.size() - maxEnergyRays()));
    }
}
