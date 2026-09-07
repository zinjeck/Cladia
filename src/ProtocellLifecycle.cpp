#include "ProtocellLifecycle.h"

#include "AbiogenesisSystem.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
    constexpr float RnaBondMaturitySeconds = 3.0f;
    constexpr float StableRnaBondDistance = 0.0115f;
    constexpr float MaxRnaBondRelativeSpeed = 0.065f;

    constexpr float LooseRnaLifetime = 70.0f;
    constexpr float LinkedRnaLifetime = 115.0f;
    constexpr float LoosePeptideLifetime = 95.0f;
    constexpr float LooseLipidLifetime = 165.0f;

    constexpr float VentRayDeathDepth = 0.035f;
    constexpr float VentRayLifetime = 7.2f;
    constexpr float VentRayRiseSpeed = -0.145f;

    constexpr float RingSearchRadius = 0.090f;
    constexpr float RingLinkDistance = 0.028f;
    constexpr float RingSpacing = 0.0095f;

    float distanceSquared(const PrimitiveParticle& a, const PrimitiveParticle& b)
    {
        const float dx = a.body.x - b.body.x;
        const float dy = a.body.y - b.body.y;
        return dx * dx + dy * dy;
    }

    float length(float x, float y)
    {
        return std::sqrt(x * x + y * y);
    }

    void addUniqueLink(PrimitiveParticle& a, std::uint32_t id)
    {
        if (std::find(a.lipidLinks.begin(), a.lipidLinks.end(), id) == a.lipidLinks.end())
        {
            a.lipidLinks.push_back(id);
        }
    }
}

void ProtocellLifecycleSystem::reset(AbiogenesisSystem& system)
{
    seeded_ = false;
    seedMaterial(system);
    normalizeParticleScales(system);
}

void ProtocellLifecycleSystem::update(AbiogenesisSystem& system, float dt)
{
    if (dt <= 0.0f) return;
    if (!seeded_) seedMaterial(system);

    const float step = std::clamp(dt, 0.0f, 0.033f);
    normalizeParticleScales(system);
    tuneEnergyRays(system);
    limitFreshVentRna(system);
    pruneDanglingLinks(system);
    suspendAndDisperseRna(system, step);
    gateRnaBonding(system);
    shapeLipidAssemblies(system, step);
    classifyProtocells(system);
    applyMaterialLifetimes(system);
}

void ProtocellLifecycleSystem::seedMaterial(AbiogenesisSystem& system)
{
    if (seeded_) return;

    // Broad loose material field. Nothing starts pre-linked or pre-enclosed.
    // There is enough lipid for multiple RNA-centered vesicles to emerge.
    constexpr int LipidCount = 210;
    constexpr int PeptideCount = 48;

    for (int i = 0; i < LipidCount; ++i)
    {
        const float x = 0.05f + static_cast<float>((i * 47) % 90) / 100.0f;
        const float y = 0.16f + static_cast<float>((i * 31) % 72) / 100.0f;
        const float jitterX = std::sin(static_cast<float>(i) * 1.73f) * 0.010f;
        const float jitterY = std::cos(static_cast<float>(i) * 1.17f) * 0.010f;
        system.spawnPrimitive(PrimitiveKind::Lipid,
            std::clamp(x + jitterX, 0.03f, 0.97f),
            std::clamp(y + jitterY, 0.08f, 0.92f));
    }

    for (int i = 0; i < PeptideCount; ++i)
    {
        const float x = 0.07f + static_cast<float>((i * 41) % 86) / 100.0f;
        const float y = 0.18f + static_cast<float>((i * 23) % 68) / 100.0f;
        system.spawnPrimitive(PrimitiveKind::Peptide,
            std::clamp(x, 0.03f, 0.97f),
            std::clamp(y, 0.08f, 0.92f));
    }

    seeded_ = true;
}

void ProtocellLifecycleSystem::normalizeParticleScales(AbiogenesisSystem& system)
{
    for (PrimitiveParticle& particle : system.mutableParticles())
    {
        switch (particle.kind)
        {
        case PrimitiveKind::Lipid:
            particle.body.radius = 0.0022f;
            particle.body.densityKgM3 = 985.0f;
            break;
        case PrimitiveKind::RnaTriplet:
            particle.body.radius = 0.00155f;
            // Game-scale suspension: vent plumes and colloidal motion keep free
            // RNA from simply carpeting the seafloor.
            particle.body.densityKgM3 = 1025.0f;
            break;
        case PrimitiveKind::Peptide:
            particle.body.radius = 0.0020f;
            break;
        case PrimitiveKind::Atp:
            particle.body.radius = 0.0017f;
            break;
        }
    }
}

void ProtocellLifecycleSystem::pruneDanglingLinks(AbiogenesisSystem& system)
{
    auto& particles = system.mutableParticles();
    std::unordered_set<std::uint32_t> ids;
    ids.reserve(particles.size() * 2);
    for (const PrimitiveParticle& p : particles) ids.insert(p.id);

    const auto missing = [&ids](std::uint32_t id)
    {
        return id != 0 && !ids.contains(id);
    };

    for (PrimitiveParticle& p : particles)
    {
        if (missing(p.frontLink)) p.frontLink = 0;
        if (missing(p.backLink)) p.backLink = 0;
        if (missing(p.templatePartnerId)) p.templatePartnerId = 0;
        if (missing(p.replicaTripletId)) p.replicaTripletId = 0;
        if (missing(p.readingTriplet)) p.readingTriplet = 0;
        std::erase_if(p.lipidLinks, [&ids](std::uint32_t id) { return !ids.contains(id); });
    }
}

void ProtocellLifecycleSystem::tuneEnergyRays(AbiogenesisSystem& system)
{
    for (EnergyRay& ray : system.mutableEnergyRays())
    {
        if (ray.solar) continue;
        ray.lifetime = std::max(ray.lifetime, VentRayLifetime);
        ray.vy = std::min(ray.vy, VentRayRiseSpeed);
        if (ray.y <= VentRayDeathDepth) ray.age = ray.lifetime;
    }
}

void ProtocellLifecycleSystem::limitFreshVentRna(AbiogenesisSystem& system)
{
    auto& particles = system.mutableParticles();

    // The core vent emitter is intentionally generous. Keep only one out of
    // every five brand-new, still-free RNA triplets near the vent floor. This
    // cuts effective vent RNA output by ~80% without touching replicated RNA.
    std::erase_if(particles, [](const PrimitiveParticle& p)
    {
        if (p.kind != PrimitiveKind::RnaTriplet) return false;
        if (p.ageSeconds > 0.35f || p.body.y < 0.90f) return false;
        if (p.frontLink != 0 || p.backLink != 0 || p.templatePartnerId != 0 || p.replicaTripletId != 0) return false;
        return (p.id % 5u) != 0u;
    });
}

void ProtocellLifecycleSystem::suspendAndDisperseRna(AbiogenesisSystem& system, float dt)
{
    for (PrimitiveParticle& rna : system.mutableParticles())
    {
        if (rna.kind != PrimitiveKind::RnaTriplet) continue;

        const bool linked = rna.frontLink != 0 || rna.backLink != 0;
        const float depth = std::clamp(rna.body.y, 0.0f, 1.0f);

        // Vent-plume carry is strongest near the bottom, then gives way to
        // weaker suspension and lateral Brownian/current-like drift aloft.
        const float upward = linked
            ? (0.008f + depth * 0.010f)
            : (0.020f + depth * 0.045f);
        rna.body.vy -= upward * dt;

        const float phase = static_cast<float>(rna.id) * 1.618f + rna.ageSeconds * 1.7f;
        rna.body.vx += std::sin(phase) * (linked ? 0.004f : 0.012f) * dt;
        rna.body.vy += std::cos(phase * 0.73f) * (linked ? 0.002f : 0.006f) * dt;

        // Prevent free triplets from becoming a sediment carpet.
        if (!linked && rna.body.y > 0.88f)
        {
            rna.body.vy = std::min(rna.body.vy, -0.018f);
        }
    }
}

void ProtocellLifecycleSystem::gateRnaBonding(AbiogenesisSystem& system)
{
    auto& particles = system.mutableParticles();
    std::unordered_map<std::uint32_t, PrimitiveParticle*> byId;
    byId.reserve(particles.size());
    for (PrimitiveParticle& p : particles) byId[p.id] = &p;

    for (PrimitiveParticle& a : particles)
    {
        if (a.kind != PrimitiveKind::RnaTriplet || a.backLink == 0) continue;
        auto it = byId.find(a.backLink);
        if (it == byId.end()) { a.backLink = 0; continue; }
        PrimitiveParticle& b = *it->second;

        // Replication-created daughter bonds are not spontaneous chemistry.
        if (a.templatePartnerId != 0 || b.templatePartnerId != 0 ||
            a.replicaTripletId != 0 || b.replicaTripletId != 0)
        {
            continue;
        }

        const float dx = b.body.x - a.body.x;
        const float dy = b.body.y - a.body.y;
        const float d = length(dx, dy);
        const float rvx = b.body.vx - a.body.vx;
        const float rvy = b.body.vy - a.body.vy;
        const float relativeSpeed = length(rvx, rvy);

        const bool tooYoung = a.ageSeconds < RnaBondMaturitySeconds || b.ageSeconds < RnaBondMaturitySeconds;
        const bool unstableCollision = relativeSpeed > MaxRnaBondRelativeSpeed;
        const bool tooFar = d > StableRnaBondDistance;

        if (tooYoung || unstableCollision || tooFar)
        {
            a.backLink = 0;
            if (b.frontLink == a.id) b.frontLink = 0;
        }
    }
}

void ProtocellLifecycleSystem::shapeLipidAssemblies(AbiogenesisSystem& system, float dt)
{
    auto& particles = system.mutableParticles();
    std::vector<PrimitiveParticle*> lipids;
    std::unordered_map<std::uint32_t, PrimitiveParticle*> byId;
    for (PrimitiveParticle& p : particles)
    {
        byId[p.id] = &p;
        if (p.kind == PrimitiveKind::Lipid) lipids.push_back(&p);
    }

    // Stop random lipid blobs from self-maintaining. Only already-recognized
    // protocell membranes keep their old links; loose lipids must organize
    // around an RNA target before receiving new chain links.
    for (PrimitiveParticle* lipid : lipids)
    {
        if (!lipid->inProtoCell)
        {
            lipid->lipidLinks.clear();
            lipid->inClosedLipidLoop = false;
        }
    }

    std::vector<std::vector<PrimitiveParticle*>> rnaTargets;
    std::unordered_set<std::uint32_t> seenRna;
    for (PrimitiveParticle& p : particles)
    {
        if (p.kind != PrimitiveKind::RnaTriplet || seenRna.contains(p.id)) continue;

        PrimitiveParticle* root = &p;
        int guard = 0;
        while (root->frontLink != 0 && guard++ < 128)
        {
            auto it = byId.find(root->frontLink);
            if (it == byId.end() || it->second->kind != PrimitiveKind::RnaTriplet) break;
            root = it->second;
        }

        std::vector<PrimitiveParticle*> chain;
        PrimitiveParticle* cursor = root;
        guard = 0;
        while (cursor && guard++ < 256 && seenRna.insert(cursor->id).second)
        {
            chain.push_back(cursor);
            if (cursor->backLink == 0) break;
            auto it = byId.find(cursor->backLink);
            cursor = (it != byId.end() && it->second->kind == PrimitiveKind::RnaTriplet) ? it->second : nullptr;
        }
        if (!chain.empty()) rnaTargets.push_back(std::move(chain));
    }

    std::unordered_set<std::uint32_t> assignedLipids;
    for (const auto& chain : rnaTargets)
    {
        float cx = 0.0f;
        float cy = 0.0f;
        for (const PrimitiveParticle* rna : chain)
        {
            cx += rna->body.x;
            cy += rna->body.y;
        }
        cx /= static_cast<float>(chain.size());
        cy /= static_cast<float>(chain.size());

        float extent = 0.0f;
        for (const PrimitiveParticle* rna : chain)
        {
            extent = std::max(extent, length(rna->body.x - cx, rna->body.y - cy));
        }
        const float targetRadius = std::clamp(extent + 0.026f, 0.030f, 0.058f);
        const int desiredCount = std::clamp(
            static_cast<int>((2.0f * std::numbers::pi_v<float> * targetRadius) / RingSpacing),
            16, 34);

        std::vector<PrimitiveParticle*> candidates;
        for (PrimitiveParticle* lipid : lipids)
        {
            if (lipid->inProtoCell || assignedLipids.contains(lipid->id)) continue;
            const float dx = lipid->body.x - cx;
            const float dy = lipid->body.y - cy;
            if (dx * dx + dy * dy <= RingSearchRadius * RingSearchRadius)
            {
                candidates.push_back(lipid);
            }
        }

        std::sort(candidates.begin(), candidates.end(), [cx, cy](const PrimitiveParticle* a, const PrimitiveParticle* b)
        {
            const float da = (a->body.x - cx) * (a->body.x - cx) + (a->body.y - cy) * (a->body.y - cy);
            const float db = (b->body.x - cx) * (b->body.x - cx) + (b->body.y - cy) * (b->body.y - cy);
            return da < db;
        });
        if (static_cast<int>(candidates.size()) > desiredCount) candidates.resize(static_cast<std::size_t>(desiredCount));
        if (candidates.size() < 8) continue;

        std::sort(candidates.begin(), candidates.end(), [cx, cy](const PrimitiveParticle* a, const PrimitiveParticle* b)
        {
            return std::atan2(a->body.y - cy, a->body.x - cx) < std::atan2(b->body.y - cy, b->body.x - cx);
        });

        const float n = static_cast<float>(candidates.size());
        for (std::size_t i = 0; i < candidates.size(); ++i)
        {
            PrimitiveParticle& lipid = *candidates[i];
            assignedLipids.insert(lipid.id);

            const float targetAngle = 2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / n;
            const float tx = cx + std::cos(targetAngle) * targetRadius;
            const float ty = cy + std::sin(targetAngle) * targetRadius;
            lipid.body.vx += (tx - lipid.body.x) * 1.20f * dt;
            lipid.body.vy += (ty - lipid.body.y) * 1.20f * dt;

            const float radialX = lipid.body.x - cx;
            const float radialY = lipid.body.y - cy;
            const float radialD = length(radialX, radialY) + 1e-6f;
            const float radialError = targetRadius - radialD;
            lipid.body.vx += radialX / radialD * radialError * 0.55f * dt;
            lipid.body.vy += radialY / radialD * radialError * 0.55f * dt;
        }

        // Links are only created between angular neighbors on the RNA-centered
        // perimeter. This produces a chain/ring rather than a compact blob.
        for (std::size_t i = 0; i < candidates.size(); ++i)
        {
            PrimitiveParticle& a = *candidates[i];
            PrimitiveParticle& b = *candidates[(i + 1) % candidates.size()];
            const float d = std::sqrt(distanceSquared(a, b));
            if (d <= RingLinkDistance)
            {
                addUniqueLink(a, b.id);
                addUniqueLink(b, a.id);
            }
        }

        bool closed = candidates.size() >= 12;
        for (PrimitiveParticle* lipid : candidates)
        {
            closed = closed && lipid->lipidLinks.size() == 2;
        }
        if (closed)
        {
            for (PrimitiveParticle* lipid : candidates) lipid->inClosedLipidLoop = true;
        }
    }

    // Loose lipids that are not participating in an RNA-centered ring repel at
    // very short range so they remain dispersed instead of forming blue clumps.
    for (std::size_t i = 0; i < lipids.size(); ++i)
    {
        PrimitiveParticle& a = *lipids[i];
        if (a.inProtoCell || assignedLipids.contains(a.id)) continue;
        for (std::size_t j = i + 1; j < lipids.size(); ++j)
        {
            PrimitiveParticle& b = *lipids[j];
            if (b.inProtoCell || assignedLipids.contains(b.id)) continue;
            float dx = b.body.x - a.body.x;
            float dy = b.body.y - a.body.y;
            const float d = length(dx, dy);
            if (d <= 1e-6f || d > 0.012f) continue;
            const float repel = (0.012f - d) * 0.80f;
            dx /= d;
            dy /= d;
            a.body.vx -= dx * repel * dt;
            a.body.vy -= dy * repel * dt;
            b.body.vx += dx * repel * dt;
            b.body.vy += dy * repel * dt;
        }
    }
}

void ProtocellLifecycleSystem::classifyProtocells(AbiogenesisSystem& system)
{
    auto& particles = system.mutableParticles();
    for (PrimitiveParticle& p : particles) p.inProtoCell = false;

    std::unordered_map<std::uint32_t, PrimitiveParticle*> lipidById;
    for (PrimitiveParticle& p : particles)
        if (p.kind == PrimitiveKind::Lipid) lipidById[p.id] = &p;

    std::unordered_set<std::uint32_t> visited;
    for (auto& [startId, start] : lipidById)
    {
        if (visited.contains(startId) || start->lipidLinks.empty()) continue;

        std::vector<PrimitiveParticle*> component;
        std::vector<std::uint32_t> stack{startId};
        visited.insert(startId);
        while (!stack.empty())
        {
            const std::uint32_t id = stack.back();
            stack.pop_back();
            PrimitiveParticle* lipid = lipidById[id];
            component.push_back(lipid);
            for (std::uint32_t linked : lipid->lipidLinks)
                if (lipidById.contains(linked) && visited.insert(linked).second) stack.push_back(linked);
        }

        if (component.size() < 12) continue;
        bool closed = true;
        float cx = 0.0f, cy = 0.0f;
        for (PrimitiveParticle* lipid : component)
        {
            closed = closed && lipid->lipidLinks.size() == 2 && lipid->inClosedLipidLoop;
            cx += lipid->body.x;
            cy += lipid->body.y;
        }
        if (!closed) continue;
        cx /= static_cast<float>(component.size());
        cy /= static_cast<float>(component.size());

        float radius = 0.0f;
        for (PrimitiveParticle* lipid : component) radius += length(lipid->body.x - cx, lipid->body.y - cy);
        radius /= static_cast<float>(component.size());
        if (radius < 0.026f) continue;

        const float interiorR2 = (radius * 0.78f) * (radius * 0.78f);
        int linkedRnaTriplets = 0;
        int peptides = 0;
        for (const PrimitiveParticle& p : particles)
        {
            const float dx = p.body.x - cx;
            const float dy = p.body.y - cy;
            if (dx * dx + dy * dy > interiorR2) continue;
            if (p.kind == PrimitiveKind::RnaTriplet && (p.frontLink != 0 || p.backLink != 0)) ++linkedRnaTriplets;
            else if (p.kind == PrimitiveKind::Peptide) ++peptides;
        }
        if (linkedRnaTriplets < 3 || peptides < 2) continue;

        for (PrimitiveParticle* lipid : component) lipid->inProtoCell = true;
        for (PrimitiveParticle& p : particles)
        {
            if (p.kind == PrimitiveKind::Atp) continue;
            const float dx = p.body.x - cx;
            const float dy = p.body.y - cy;
            if (dx * dx + dy * dy <= interiorR2) p.inProtoCell = true;
        }
    }
}

void ProtocellLifecycleSystem::applyMaterialLifetimes(AbiogenesisSystem& system)
{
    for (PrimitiveParticle& p : system.mutableParticles())
    {
        if (p.kind == PrimitiveKind::Atp) continue;
        if (p.inProtoCell)
        {
            p.lifetimeSeconds = 0.0f;
            p.ageSeconds = std::min(p.ageSeconds, 15.0f);
            continue;
        }

        switch (p.kind)
        {
        case PrimitiveKind::Lipid:
            p.lifetimeSeconds = LooseLipidLifetime;
            break;
        case PrimitiveKind::RnaTriplet:
            p.lifetimeSeconds = (p.frontLink != 0 || p.backLink != 0) ? LinkedRnaLifetime : LooseRnaLifetime;
            break;
        case PrimitiveKind::Peptide:
            p.lifetimeSeconds = LoosePeptideLifetime;
            break;
        case PrimitiveKind::Atp:
            break;
        }
    }
}
