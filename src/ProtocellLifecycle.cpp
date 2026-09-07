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
    constexpr float RnaBondMaturitySeconds = 2.0f;
    constexpr float StableRnaBondDistance = 0.0145f;
    constexpr float MaxRnaBondRelativeSpeed = 0.10f;

    constexpr float LooseRnaLifetime = 75.0f;
    constexpr float LinkedRnaLifetime = 110.0f;
    constexpr float LoosePeptideLifetime = 90.0f;
    constexpr float LooseLipidLifetime = 150.0f;

    constexpr float VentRayDeathDepth = 0.035f;
    constexpr float VentRayLifetime = 7.2f;
    constexpr float VentRayRiseSpeed = -0.145f;

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
}

void ProtocellLifecycleSystem::reset(AbiogenesisSystem& system)
{
    seeded_ = false;
    seedMaterial(system);
    normalizeParticleScales(system);
}

void ProtocellLifecycleSystem::update(AbiogenesisSystem& system, float dt)
{
    if (dt <= 0.0f)
    {
        return;
    }

    if (!seeded_)
    {
        seedMaterial(system);
    }

    const float step = std::clamp(dt, 0.0f, 0.033f);
    pruneDanglingLinks(system);
    normalizeParticleScales(system);
    tuneEnergyRays(system);
    gateRnaBonding(system);
    shapeLipidAssemblies(system, step);
    classifyProtocells(system);
    applyMaterialLifetimes(system);
}

void ProtocellLifecycleSystem::seedMaterial(AbiogenesisSystem& system)
{
    if (seeded_)
    {
        return;
    }

    // Extra raw material is supplied as loose clouds, not as prebuilt cells or
    // membranes. Hydrophobic self-assembly must still organize the lipids.
    constexpr int LipidCount = 144;
    constexpr int PeptideCount = 36;
    constexpr int ClusterCount = 6;

    for (int i = 0; i < LipidCount; ++i)
    {
        const int cluster = i % ClusterCount;
        const int layer = i / ClusterCount;
        const float cx = 0.12f + static_cast<float>(cluster) * 0.152f;
        const float cy = 0.42f + static_cast<float>(cluster % 3) * 0.17f;
        const float angle = static_cast<float>(layer) * 2.399963f + static_cast<float>(cluster) * 0.71f;
        const float radius = 0.006f + static_cast<float>(layer % 8) * 0.0042f;
        system.spawnPrimitive(
            PrimitiveKind::Lipid,
            std::clamp(cx + std::cos(angle) * radius, 0.03f, 0.97f),
            std::clamp(cy + std::sin(angle) * radius, 0.08f, 0.92f));
    }

    for (int i = 0; i < PeptideCount; ++i)
    {
        const int cluster = i % ClusterCount;
        const int layer = i / ClusterCount;
        const float cx = 0.12f + static_cast<float>(cluster) * 0.152f;
        const float cy = 0.42f + static_cast<float>(cluster % 3) * 0.17f;
        const float angle = static_cast<float>(layer) * 1.91f + static_cast<float>(cluster);
        const float radius = 0.010f + static_cast<float>(layer % 4) * 0.006f;
        system.spawnPrimitive(
            PrimitiveKind::Peptide,
            std::clamp(cx + std::cos(angle) * radius, 0.03f, 0.97f),
            std::clamp(cy + std::sin(angle) * radius, 0.08f, 0.92f));
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
            particle.body.radius = 0.0025f;
            break;
        case PrimitiveKind::RnaTriplet:
            particle.body.radius = 0.0022f;
            break;
        case PrimitiveKind::Peptide:
            particle.body.radius = 0.0022f;
            break;
        case PrimitiveKind::Atp:
            particle.body.radius = 0.0018f;
            break;
        }
    }
}

void ProtocellLifecycleSystem::pruneDanglingLinks(AbiogenesisSystem& system)
{
    auto& particles = system.mutableParticles();
    std::unordered_set<std::uint32_t> ids;
    ids.reserve(particles.size() * 2);
    for (const PrimitiveParticle& p : particles)
    {
        ids.insert(p.id);
    }

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
        if (ray.solar)
        {
            continue;
        }

        // Vent energy now travels most of the water column before fading just
        // below the surface instead of disappearing immediately above the vent.
        ray.lifetime = std::max(ray.lifetime, VentRayLifetime);
        ray.vy = std::min(ray.vy, VentRayRiseSpeed);
        if (ray.y <= VentRayDeathDepth)
        {
            ray.age = ray.lifetime;
        }
    }
}

void ProtocellLifecycleSystem::gateRnaBonding(AbiogenesisSystem& system)
{
    auto& particles = system.mutableParticles();
    std::unordered_map<std::uint32_t, PrimitiveParticle*> byId;
    byId.reserve(particles.size());
    for (PrimitiveParticle& p : particles)
    {
        byId[p.id] = &p;
    }

    for (PrimitiveParticle& a : particles)
    {
        if (a.kind != PrimitiveKind::RnaTriplet || a.backLink == 0)
        {
            continue;
        }

        auto it = byId.find(a.backLink);
        if (it == byId.end())
        {
            a.backLink = 0;
            continue;
        }

        PrimitiveParticle& b = *it->second;

        // Replication-created daughter bonds are handled by the replication
        // machinery. This gate only controls spontaneous polymerization.
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
            const std::uint32_t bId = b.id;
            a.backLink = 0;
            if (b.frontLink == a.id)
            {
                b.frontLink = 0;
            }
            (void)bId;
        }
    }
}

void ProtocellLifecycleSystem::shapeLipidAssemblies(AbiogenesisSystem& system, float dt)
{
    auto& particles = system.mutableParticles();
    std::vector<PrimitiveParticle*> lipids;
    lipids.reserve(particles.size());
    for (PrimitiveParticle& p : particles)
    {
        if (p.kind == PrimitiveKind::Lipid)
        {
            lipids.push_back(&p);
        }
    }

    // Amphiphilic self-assembly is represented as short-range lipid cohesion
    // plus steric repulsion. This is the game-scale stand-in for the
    // hydrophobic effect that drives membrane/vesicle formation in water.
    for (std::size_t i = 0; i < lipids.size(); ++i)
    {
        for (std::size_t j = i + 1; j < lipids.size(); ++j)
        {
            PrimitiveParticle& a = *lipids[i];
            PrimitiveParticle& b = *lipids[j];
            float dx = b.body.x - a.body.x;
            float dy = b.body.y - a.body.y;
            const float d2 = dx * dx + dy * dy;
            if (d2 > 0.0030f || d2 < 1e-10f)
            {
                continue;
            }

            const float d = std::sqrt(d2);
            dx /= d;
            dy /= d;
            float force = 0.0f;
            if (d < 0.0065f)
            {
                force = -(0.0065f - d) * 2.2f;
            }
            else
            {
                force = std::min(0.012f, (d - 0.0065f) * 0.22f);
            }

            a.body.vx += dx * force * dt;
            a.body.vy += dy * force * dt;
            b.body.vx -= dx * force * dt;
            b.body.vy -= dy * force * dt;
        }
    }

    std::unordered_map<std::uint32_t, PrimitiveParticle*> byId;
    byId.reserve(lipids.size() * 2);
    for (PrimitiveParticle* lipid : lipids)
    {
        byId[lipid->id] = lipid;
    }

    std::unordered_set<std::uint32_t> visited;
    for (PrimitiveParticle* start : lipids)
    {
        if (visited.contains(start->id) || start->lipidLinks.empty())
        {
            continue;
        }

        std::vector<PrimitiveParticle*> component;
        std::vector<std::uint32_t> stack{start->id};
        visited.insert(start->id);
        while (!stack.empty())
        {
            const std::uint32_t id = stack.back();
            stack.pop_back();
            auto it = byId.find(id);
            if (it == byId.end()) continue;
            PrimitiveParticle* lipid = it->second;
            component.push_back(lipid);
            for (std::uint32_t linked : lipid->lipidLinks)
            {
                if (byId.contains(linked) && visited.insert(linked).second)
                {
                    stack.push_back(linked);
                }
            }
        }

        if (component.size() < 6)
        {
            continue;
        }

        float cx = 0.0f;
        float cy = 0.0f;
        for (PrimitiveParticle* lipid : component)
        {
            cx += lipid->body.x;
            cy += lipid->body.y;
        }
        cx /= static_cast<float>(component.size());
        cy /= static_cast<float>(component.size());

        const float targetRadius = std::clamp(
            static_cast<float>(component.size()) * 0.00145f,
            0.026f,
            0.056f);

        for (PrimitiveParticle* lipid : component)
        {
            float dx = lipid->body.x - cx;
            float dy = lipid->body.y - cy;
            float d = length(dx, dy);
            if (d < 1e-5f)
            {
                const float angle = static_cast<float>(lipid->id % 97u) / 97.0f * 2.0f * std::numbers::pi_v<float>;
                dx = std::cos(angle);
                dy = std::sin(angle);
                d = 1.0f;
            }
            dx /= d;
            dy /= d;

            const float radialForce = (targetRadius - d) * 0.65f;
            lipid->body.vx += dx * radialForce * dt;
            lipid->body.vy += dy * radialForce * dt;

            // A weak tangential bias helps flexible open chains curl rather than
            // collapsing into a straight bundle. The sign is deterministic.
            const float direction = (start->id & 1u) == 0u ? 1.0f : -1.0f;
            lipid->body.vx += -dy * 0.005f * direction * dt;
            lipid->body.vy += dx * 0.005f * direction * dt;
        }

        std::vector<PrimitiveParticle*> endpoints;
        for (PrimitiveParticle* lipid : component)
        {
            if (lipid->lipidLinks.size() < 2)
            {
                endpoints.push_back(lipid);
            }
        }
        if (endpoints.size() == 2)
        {
            PrimitiveParticle& a = *endpoints[0];
            PrimitiveParticle& b = *endpoints[1];
            float dx = b.body.x - a.body.x;
            float dy = b.body.y - a.body.y;
            const float d = length(dx, dy) + 1e-6f;
            a.body.vx += dx / d * 0.010f * dt;
            a.body.vy += dy / d * 0.010f * dt;
            b.body.vx -= dx / d * 0.010f * dt;
            b.body.vy -= dy / d * 0.010f * dt;
        }
    }
}

void ProtocellLifecycleSystem::classifyProtocells(AbiogenesisSystem& system)
{
    auto& particles = system.mutableParticles();
    for (PrimitiveParticle& p : particles)
    {
        p.inProtoCell = false;
    }

    std::unordered_map<std::uint32_t, PrimitiveParticle*> lipidById;
    for (PrimitiveParticle& p : particles)
    {
        if (p.kind == PrimitiveKind::Lipid)
        {
            lipidById[p.id] = &p;
        }
    }

    std::unordered_set<std::uint32_t> visited;
    for (auto& [startId, start] : lipidById)
    {
        if (visited.contains(startId)) continue;

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
            {
                if (lipidById.contains(linked) && visited.insert(linked).second)
                {
                    stack.push_back(linked);
                }
            }
        }

        if (component.size() < 10)
        {
            continue;
        }

        bool closed = true;
        float cx = 0.0f;
        float cy = 0.0f;
        for (PrimitiveParticle* lipid : component)
        {
            closed = closed && lipid->lipidLinks.size() == 2 && lipid->inClosedLipidLoop;
            cx += lipid->body.x;
            cy += lipid->body.y;
        }
        if (!closed)
        {
            continue;
        }

        cx /= static_cast<float>(component.size());
        cy /= static_cast<float>(component.size());
        float radius = 0.0f;
        for (PrimitiveParticle* lipid : component)
        {
            radius += length(lipid->body.x - cx, lipid->body.y - cy);
        }
        radius /= static_cast<float>(component.size());
        if (radius < 0.022f)
        {
            continue;
        }

        const float interiorRadius = radius * 0.78f;
        const float interiorR2 = interiorRadius * interiorRadius;
        int linkedRnaTriplets = 0;
        int peptides = 0;
        for (const PrimitiveParticle& p : particles)
        {
            const float dx = p.body.x - cx;
            const float dy = p.body.y - cy;
            if (dx * dx + dy * dy > interiorR2)
            {
                continue;
            }
            if (p.kind == PrimitiveKind::RnaTriplet && (p.frontLink != 0 || p.backLink != 0))
            {
                ++linkedRnaTriplets;
            }
            else if (p.kind == PrimitiveKind::Peptide)
            {
                ++peptides;
            }
        }

        // The status emerges from enclosure geometry and contents. Nothing is
        // converted into a Cell object here.
        if (linkedRnaTriplets < 3 || peptides < 2)
        {
            continue;
        }

        for (PrimitiveParticle* lipid : component)
        {
            lipid->inProtoCell = true;
        }
        for (PrimitiveParticle& p : particles)
        {
            if (p.kind == PrimitiveKind::Atp) continue;
            const float dx = p.body.x - cx;
            const float dy = p.body.y - cy;
            if (dx * dx + dy * dy <= interiorR2)
            {
                p.inProtoCell = true;
            }
        }
    }
}

void ProtocellLifecycleSystem::applyMaterialLifetimes(AbiogenesisSystem& system)
{
    for (PrimitiveParticle& p : system.mutableParticles())
    {
        if (p.kind == PrimitiveKind::Atp)
        {
            continue;
        }

        if (p.inProtoCell)
        {
            // Enclosed, chemically richer assemblies persist. If enclosure is
            // later lost they return to ordinary degradation pressure.
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
            p.lifetimeSeconds = (p.frontLink != 0 || p.backLink != 0)
                ? LinkedRnaLifetime
                : LooseRnaLifetime;
            break;
        case PrimitiveKind::Peptide:
            p.lifetimeSeconds = LoosePeptideLifetime;
            break;
        case PrimitiveKind::Atp:
            break;
        }
    }
}
