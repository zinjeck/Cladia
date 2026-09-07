#include "RnaPopulationTuner.h"

#include "AbiogenesisSystem.h"
#include "EntityBudgetSystem.h"
#include "WorldTopology.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
    float length(float x, float y)
    {
        return std::sqrt(x * x + y * y);
    }

    void lockNearCompleteLipidRings(AbiogenesisSystem& system)
    {
        auto& particles = system.mutableParticles();
        std::unordered_map<std::uint32_t, PrimitiveParticle*> byId;
        byId.reserve(particles.size() * 2);
        for (PrimitiveParticle& p : particles) byId[p.id] = &p;

        std::unordered_set<std::uint32_t> seenRna;
        std::unordered_set<std::uint32_t> claimedLipids;

        for (PrimitiveParticle& seed : particles)
        {
            if (seed.kind != PrimitiveKind::RnaTriplet || seenRna.contains(seed.id)) continue;

            PrimitiveParticle* root = &seed;
            for (int guard = 0; guard < 128 && root->frontLink != 0; ++guard)
            {
                auto it = byId.find(root->frontLink);
                if (it == byId.end() || it->second->kind != PrimitiveKind::RnaTriplet) break;
                root = it->second;
            }

            std::vector<PrimitiveParticle*> chain;
            PrimitiveParticle* cursor = root;
            for (int guard = 0; guard < 256 && cursor && seenRna.insert(cursor->id).second; ++guard)
            {
                chain.push_back(cursor);
                if (cursor->backLink == 0) break;
                auto it = byId.find(cursor->backLink);
                cursor = (it != byId.end() && it->second->kind == PrimitiveKind::RnaTriplet) ? it->second : nullptr;
            }
            if (chain.size() < 2) continue;

            const float refX = chain.front()->body.x;
            float cxUnwrapped = 0.0f;
            float cy = 0.0f;
            for (const PrimitiveParticle* rna : chain)
            {
                cxUnwrapped += WorldTopology::unwrapNear(refX, rna->body.x);
                cy += rna->body.y;
            }
            const float cx = WorldTopology::wrap01(cxUnwrapped / static_cast<float>(chain.size()));
            cy /= static_cast<float>(chain.size());

            float extent = 0.0f;
            for (const PrimitiveParticle* rna : chain)
                extent = std::max(extent, length(WorldTopology::deltaX(cx, rna->body.x), rna->body.y - cy));

            const float targetRadius = std::clamp(extent + 0.028f, 0.032f, 0.056f);
            const int desiredCount = std::clamp(
                static_cast<int>((2.0f * std::numbers::pi_v<float> * targetRadius) / 0.010f), 12, 24);

            std::vector<PrimitiveParticle*> candidates;
            for (PrimitiveParticle& lipid : particles)
            {
                if (lipid.kind != PrimitiveKind::Lipid || lipid.inProtoCell || lipid.stableMembrane ||
                    lipid.lipidRebindCooldownSeconds > 0.0f || claimedLipids.contains(lipid.id))
                    continue;

                const float dx = WorldTopology::deltaX(cx, lipid.body.x);
                const float dy = lipid.body.y - cy;
                if (dx * dx + dy * dy <= 0.080f * 0.080f)
                    candidates.push_back(&lipid);
            }

            std::sort(candidates.begin(), candidates.end(), [cx, cy, targetRadius](const PrimitiveParticle* a, const PrimitiveParticle* b)
            {
                const float da = std::abs(length(WorldTopology::deltaX(cx, a->body.x), a->body.y - cy) - targetRadius);
                const float db = std::abs(length(WorldTopology::deltaX(cx, b->body.x), b->body.y - cy) - targetRadius);
                return da < db;
            });
            if (static_cast<int>(candidates.size()) > desiredCount)
                candidates.resize(static_cast<std::size_t>(desiredCount));
            if (candidates.size() < 10) continue;

            std::sort(candidates.begin(), candidates.end(), [cx, cy](const PrimitiveParticle* a, const PrimitiveParticle* b)
            {
                return std::atan2(a->body.y - cy, WorldTopology::deltaX(cx, a->body.x)) <
                       std::atan2(b->body.y - cy, WorldTopology::deltaX(cx, b->body.x));
            });

            // Once enough lipids have self-assembled around a real RNA link,
            // stop the endless orbiting state. Clear stray connectors and lock
            // each membrane lipid to exactly its two angular neighbours.
            std::unordered_set<std::uint32_t> selectedIds;
            for (PrimitiveParticle* lipid : candidates) selectedIds.insert(lipid->id);
            for (PrimitiveParticle& lipid : particles)
            {
                if (lipid.kind != PrimitiveKind::Lipid) continue;
                std::erase_if(lipid.lipidLinks, [&](std::uint32_t id) { return selectedIds.contains(id); });
            }

            const float n = static_cast<float>(candidates.size());
            for (std::size_t i = 0; i < candidates.size(); ++i)
            {
                PrimitiveParticle& lipid = *candidates[i];
                PrimitiveParticle& previous = *candidates[(i + candidates.size() - 1) % candidates.size()];
                PrimitiveParticle& next = *candidates[(i + 1) % candidates.size()];

                const float angle = 2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / n;
                const float tx = WorldTopology::wrap01(cx + std::cos(angle) * targetRadius);
                const float ty = cy + std::sin(angle) * targetRadius;
                lipid.body.x = WorldTopology::wrap01(lipid.body.x + WorldTopology::deltaX(lipid.body.x, tx) * 0.38f);
                lipid.body.y = std::clamp(lipid.body.y + (ty - lipid.body.y) * 0.38f, 0.002f, 0.998f);
                lipid.body.vx *= 0.55f;
                lipid.body.vy *= 0.55f;

                lipid.lipidLinks = {previous.id, next.id};
                lipid.inClosedLipidLoop = true;
                lipid.stableMembrane = true;
                // Temporary protection prevents the lifecycle pass immediately
                // erasing the newly closed membrane before it can classify it.
                lipid.inProtoCell = true;
                claimedLipids.insert(lipid.id);
            }
        }
    }
}

void RnaPopulationTuner::update(AbiogenesisSystem& system)
{
    ++frameCounter_;
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

    std::erase_if(particles, [](PrimitiveParticle& p)
    {
        if (p.kind != PrimitiveKind::Atp) return false;
        if (p.ageSeconds > 0.45f || p.lifetimeSeconds < 0.0f) return false;

        const bool fromVent = p.body.y > 0.80f;
        if (!fromVent) return true;
        if ((p.id % 8u) != 0u) return true;

        const float id = static_cast<float>(p.id);
        p.body.vx = std::sin(id * 1.731f) * 0.085f + std::cos(id * 0.617f) * 0.035f;
        p.body.vy = -0.22f - std::abs(std::cos(id * 1.113f)) * 0.075f;
        p.lifetimeSeconds = std::min(p.lifetimeSeconds, 7.5f);
        return false;
    });

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
        p.body.vx += std::sin(id * 0.73f + p.ageSeconds * 2.4f) * 0.0045f;
        p.body.vx = std::clamp(p.body.vx, -0.12f, 0.12f);
    }

    lockNearCompleteLipidRings(system);

    // Long-run maintenance counts only FREE usable material. Lipids and
    // peptides already committed to membranes/cells do not block replenishment.
    std::vector<const PrimitiveParticle*> linkedRna;
    std::size_t freeLipids = 0;
    std::size_t freePeptides = 0;
    for (const PrimitiveParticle& p : particles)
    {
        if (p.kind == PrimitiveKind::Lipid && !p.inProtoCell && !p.stableMembrane)
            ++freeLipids;
        else if (p.kind == PrimitiveKind::Peptide && !p.inProtoCell && !p.peptideCatalysisActive)
            ++freePeptides;
        else if (p.kind == PrimitiveKind::RnaTriplet && (p.frontLink != 0 || p.backLink != 0))
            linkedRna.push_back(&p);
    }

    if (frameCounter_ % 12u == 0u && freeLipids < 92)
    {
        float cx = 0.10f + static_cast<float>((frameCounter_ * 37u) % 80u) / 100.0f;
        float cy = 0.18f + static_cast<float>((frameCounter_ * 23u) % 70u) / 100.0f;
        if (!linkedRna.empty())
        {
            const PrimitiveParticle* target = linkedRna[frameCounter_ % linkedRna.size()];
            cx = target->body.x;
            cy = target->body.y;
        }
        const float angle = static_cast<float>(frameCounter_) * 2.399963f;
        PrimitiveParticle& lipid = system.spawnPrimitive(
            PrimitiveKind::Lipid,
            WorldTopology::wrap01(cx + std::cos(angle) * 0.044f),
            std::clamp(cy + std::sin(angle) * 0.044f, 0.05f, 0.95f),
            std::cos(angle) * 0.004f,
            std::sin(angle) * 0.004f);
        lipid.lifetimeSeconds = 165.0f;
    }

    if (frameCounter_ % 75u == 0u && freePeptides < 20)
    {
        float cx = 0.12f + static_cast<float>((frameCounter_ * 41u) % 76u) / 100.0f;
        float cy = 0.20f + static_cast<float>((frameCounter_ * 29u) % 66u) / 100.0f;
        if (!linkedRna.empty())
        {
            const PrimitiveParticle* target = linkedRna[frameCounter_ % linkedRna.size()];
            cx = target->body.x;
            cy = target->body.y;
        }
        const float angle = static_cast<float>(frameCounter_) * 1.618034f;
        PrimitiveParticle& peptide = system.spawnPrimitive(
            PrimitiveKind::Peptide,
            WorldTopology::wrap01(cx + std::cos(angle) * 0.020f),
            std::clamp(cy + std::sin(angle) * 0.020f, 0.05f, 0.95f),
            std::cos(angle) * 0.003f,
            std::sin(angle) * 0.003f);
        peptide.peptideEnergyGraceSeconds = 32.0f;
        peptide.lifetimeSeconds = 0.0f;
    }

    EntityBudgetSystem{}.update(system);
}
