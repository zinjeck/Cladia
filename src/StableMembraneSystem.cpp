#include "StableMembraneSystem.h"

#include "AbiogenesisSystem.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <unordered_map>
#include <unordered_set>

namespace
{
    constexpr std::size_t MinStableRingLipids = 10;
    constexpr float MinStableRingRadius = 0.020f;
    constexpr float FailedMembraneLifetime = 150.0f;

    float length(float x, float y)
    {
        return std::sqrt(x * x + y * y);
    }
}

void StableMembraneSystem::reset()
{
    stableLipids_.clear();
    membraneAges_.clear();
    cells_.clear();
}

void StableMembraneSystem::preLifecycle(AbiogenesisSystem& system)
{
    // The lifecycle pass preserves particles flagged as protocell material.
    // Temporarily use that protection for previously stable membranes so their
    // links are not destroyed before this system can reassess them.
    for (PrimitiveParticle& p : system.mutableParticles())
    {
        if (p.kind == PrimitiveKind::Lipid && stableLipids_.contains(p.id))
        {
            p.inProtoCell = true;
        }
    }
}

void StableMembraneSystem::postLifecycle(AbiogenesisSystem& system, float dt)
{
    stabilizeClosedLoops(system, std::clamp(dt, 0.0f, 0.033f));
}

void StableMembraneSystem::stabilizeClosedLoops(AbiogenesisSystem& system, float dt)
{
    auto& particles = system.mutableParticles();
    std::unordered_map<std::uint32_t, PrimitiveParticle*> byId;
    byId.reserve(particles.size() * 2);
    for (PrimitiveParticle& p : particles) byId[p.id] = &p;

    std::unordered_set<std::uint32_t> nextStable;
    std::vector<EmergentCellSnapshot> nextCells;
    std::unordered_set<std::uint32_t> visited;

    for (PrimitiveParticle& start : particles)
    {
        if (start.kind != PrimitiveKind::Lipid || visited.contains(start.id) || start.lipidLinks.empty()) continue;

        std::vector<PrimitiveParticle*> component;
        std::vector<std::uint32_t> stack{start.id};
        visited.insert(start.id);
        bool closed = true;

        while (!stack.empty())
        {
            const std::uint32_t id = stack.back();
            stack.pop_back();
            auto it = byId.find(id);
            if (it == byId.end() || it->second->kind != PrimitiveKind::Lipid) continue;
            PrimitiveParticle* lipid = it->second;
            component.push_back(lipid);
            closed = closed && lipid->lipidLinks.size() == 2;
            for (std::uint32_t linked : lipid->lipidLinks)
            {
                auto jt = byId.find(linked);
                if (jt != byId.end() && jt->second->kind == PrimitiveKind::Lipid && visited.insert(linked).second)
                    stack.push_back(linked);
            }
        }

        if (!closed || component.size() < MinStableRingLipids) continue;

        float cx = 0.0f, cy = 0.0f;
        for (PrimitiveParticle* p : component) { cx += p->body.x; cy += p->body.y; }
        cx /= static_cast<float>(component.size());
        cy /= static_cast<float>(component.size());

        float radius = 0.0f;
        for (PrimitiveParticle* p : component) radius += length(p->body.x - cx, p->body.y - cy);
        radius /= static_cast<float>(component.size());
        if (radius < MinStableRingRadius) continue;

        const std::uint32_t membraneId = (*std::min_element(component.begin(), component.end(),
            [](const PrimitiveParticle* a, const PrimitiveParticle* b) { return a->id < b->id; }))->id;

        float& membraneAge = membraneAges_[membraneId];
        membraneAge += dt;

        for (PrimitiveParticle* lipid : component)
        {
            nextStable.insert(lipid->id);
            lipid->stableMembrane = true;
            lipid->inClosedLipidLoop = true;

            float dx = lipid->body.x - cx;
            float dy = lipid->body.y - cy;
            const float d = length(dx, dy) + 1e-6f;
            dx /= d; dy /= d;
            const float radialCorrection = (radius - d) * 1.55f;
            lipid->body.vx += dx * radialCorrection * dt;
            lipid->body.vy += dy * radialCorrection * dt;
        }

        const float interiorRadius = radius * 0.76f;
        const float interiorR2 = interiorRadius * interiorRadius;
        std::vector<std::uint32_t> rnaIds;
        std::vector<std::uint32_t> peptideIds;
        int linkedRna = 0;
        for (PrimitiveParticle& p : particles)
        {
            const float dx = p.body.x - cx;
            const float dy = p.body.y - cy;
            if (dx * dx + dy * dy > interiorR2) continue;
            if (p.kind == PrimitiveKind::RnaTriplet)
            {
                rnaIds.push_back(p.id);
                if (p.frontLink != 0 || p.backLink != 0) ++linkedRna;
            }
            else if (p.kind == PrimitiveKind::Peptide)
            {
                peptideIds.push_back(p.id);
            }
        }

        const bool isCell = linkedRna >= 3 && peptideIds.size() >= 2;
        if (isCell)
        {
            EmergentCellSnapshot cell;
            cell.id = membraneId;
            cell.centerX = cx;
            cell.centerY = cy;
            cell.radius = radius;
            cell.rnaIds = rnaIds;
            cell.peptideIds = peptideIds;
            for (PrimitiveParticle* lipid : component)
            {
                cell.lipidIds.push_back(lipid->id);
                lipid->inProtoCell = true;
                lipid->lifetimeSeconds = 0.0f;
            }
            for (PrimitiveParticle& p : particles)
            {
                const float dx = p.body.x - cx;
                const float dy = p.body.y - cy;
                if (dx * dx + dy * dy <= interiorR2 && p.kind != PrimitiveKind::Atp)
                {
                    p.inProtoCell = true;
                    p.lifetimeSeconds = 0.0f;
                }
            }
            nextCells.push_back(std::move(cell));
        }
        else
        {
            for (PrimitiveParticle* lipid : component)
            {
                lipid->inProtoCell = false;
                lipid->lifetimeSeconds = FailedMembraneLifetime;
                if (membraneAge >= FailedMembraneLifetime)
                    lipid->lifetimeSeconds = -1.0f;
            }
        }
    }

    for (PrimitiveParticle& p : particles)
    {
        if (p.kind == PrimitiveKind::Lipid && !nextStable.contains(p.id))
            p.stableMembrane = false;
    }

    stableLipids_ = std::move(nextStable);
    cells_ = std::move(nextCells);

    std::erase_if(membraneAges_, [&](const auto& item)
    {
        return !stableLipids_.contains(item.first) &&
            std::none_of(cells_.begin(), cells_.end(), [&](const EmergentCellSnapshot& c) { return c.id == item.first; });
    });
}
