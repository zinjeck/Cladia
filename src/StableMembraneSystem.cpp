#include "StableMembraneSystem.h"

#include "AbiogenesisSystem.h"
#include "WorldTopology.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace
{
    constexpr std::size_t MinStableRingLipids = 10;
    constexpr float MinStableRingRadius = 0.020f;
    constexpr float FailedMembraneLifetime = 150.0f;
    constexpr float ReplicationCatalysisWindow = 7.5f;

    constexpr std::size_t InitialLooseLipidTarget = 28;
    constexpr std::size_t InitialLoosePeptideTarget = 6;
    constexpr std::size_t SupplyLipidCeiling = 150;
    constexpr std::size_t SupplyPeptideCeiling = 34;
    constexpr float FocusedLipidRate = 2.4f;
    constexpr float AmbientLipidRate = 0.35f;
    constexpr float FocusedPeptideRate = 0.34f;
    constexpr float AmbientPeptideRate = 0.08f;

    float length(float x, float y)
    {
        return std::sqrt(x * x + y * y);
    }

    float componentCenterX(const std::vector<PrimitiveParticle*>& component)
    {
        if (component.empty()) return 0.5f;
        const float ref = component.front()->body.x;
        float sum = 0.0f;
        for (const PrimitiveParticle* p : component)
            sum += WorldTopology::unwrapNear(ref, p->body.x);
        return WorldTopology::wrap01(sum / static_cast<float>(component.size()));
    }
}

void StableMembraneSystem::reset()
{
    stableLipids_.clear();
    membraneAges_.clear();
    replicationWindows_.clear();
    catalystByCell_.clear();
    catalystSawReplication_.clear();
    cells_.clear();
    lipidSupplyAccumulator_ = 0.0f;
    peptideSupplyAccumulator_ = 0.0f;
    supplySequence_ = 0;
    initialMaterialTrimmed_ = false;
}

void StableMembraneSystem::prepareReplication(AbiogenesisSystem& system, float dt)
{
    auto& particles = system.mutableParticles();
    std::unordered_map<std::uint32_t, PrimitiveParticle*> byId;
    byId.reserve(particles.size() * 2);
    for (PrimitiveParticle& p : particles) byId[p.id] = &p;

    std::unordered_set<std::uint32_t> allowedRna;
    std::unordered_set<std::uint32_t> liveCells;

    for (const EmergentCellSnapshot& cell : cells_)
    {
        liveCells.insert(cell.id);

        std::vector<PrimitiveParticle*> genuineLinkedRna;
        bool activeReplication = false;
        for (std::uint32_t id : cell.rnaIds)
        {
            auto it = byId.find(id);
            if (it == byId.end() || it->second->kind != PrimitiveKind::RnaTriplet) continue;
            PrimitiveParticle* rna = it->second;
            if (rna->frontLink != 0 || rna->backLink != 0)
                genuineLinkedRna.push_back(rna);
            if (rna->templatePartnerId != 0 || rna->replicaTripletId != 0 || rna->replicationComplete)
                activeReplication = true;
        }

        if (genuineLinkedRna.size() < 3)
            continue;

        float& window = replicationWindows_[cell.id];
        window = std::max(0.0f, window - std::clamp(dt, 0.0f, 0.25f));

        auto catalystIt = catalystByCell_.find(cell.id);
        PrimitiveParticle* currentCatalyst = nullptr;
        if (catalystIt != catalystByCell_.end())
        {
            auto it = byId.find(catalystIt->second);
            if (it != byId.end() && it->second->kind == PrimitiveKind::Peptide)
                currentCatalyst = it->second;
            else
                catalystByCell_.erase(catalystIt);
        }

        if (activeReplication)
        {
            window = std::max(window, 1.0f);
            catalystSawReplication_.insert(cell.id);
            if (currentCatalyst)
                currentCatalyst->peptideCatalysisActive = true;
        }
        else if (currentCatalyst && catalystSawReplication_.contains(cell.id))
        {
            // A peptide that spent ATP to catalyze one completed copy cycle is
            // chemically spent and is removed after the copied strands finish separating.
            currentCatalyst->peptideCatalysisActive = false;
            currentCatalyst->peptideCatalysisSpent = true;
            currentCatalyst->lifetimeSeconds = -1.0f;
            catalystByCell_.erase(cell.id);
            catalystSawReplication_.erase(cell.id);
            window = 0.0f;
            currentCatalyst = nullptr;
        }
        else if (window <= 0.0f && !currentCatalyst)
        {
            PrimitiveParticle* catalyst = nullptr;
            for (std::uint32_t id : cell.peptideIds)
            {
                auto it = byId.find(id);
                if (it != byId.end() && it->second->kind == PrimitiveKind::Peptide &&
                    it->second->atpCharge >= 1.0f && !it->second->peptideCatalysisSpent)
                {
                    catalyst = it->second;
                    break;
                }
            }

            if (catalyst)
            {
                catalyst->atpCharge -= 1.0f;
                catalyst->excited = catalyst->atpCharge > 0.0f;
                catalyst->peptideCatalysisActive = true;
                catalyst->peptideEnergyGraceSeconds = std::max(catalyst->peptideEnergyGraceSeconds, 12.0f);
                catalystByCell_[cell.id] = catalyst->id;
                catalystSawReplication_.erase(cell.id);
                window = ReplicationCatalysisWindow;
                currentCatalyst = catalyst;
            }
        }
        else if (currentCatalyst && window <= 0.0f && !activeReplication)
        {
            // ATP was spent but no copy ever began. The peptide is no longer
            // protected and gets only a brief chance to encounter fresh ATP.
            currentCatalyst->peptideCatalysisActive = false;
            currentCatalyst->peptideCatalysisSpent = true;
            currentCatalyst->peptideEnergyGraceSeconds = std::min(currentCatalyst->peptideEnergyGraceSeconds, 6.0f);
            catalystByCell_.erase(cell.id);
            catalystSawReplication_.erase(cell.id);
            currentCatalyst = nullptr;
        }

        if (activeReplication || window > 0.0f)
        {
            for (PrimitiveParticle* rna : genuineLinkedRna)
                allowedRna.insert(rna->id);
        }
    }

    for (PrimitiveParticle& p : particles)
    {
        if (p.kind != PrimitiveKind::RnaTriplet) continue;
        const bool alreadyCopying = p.templatePartnerId != 0 || p.replicaTripletId != 0 || p.replicationComplete;
        if (alreadyCopying) continue;

        if (allowedRna.contains(p.id) && (p.frontLink != 0 || p.backLink != 0))
            p.replicationCooldown = 0.0f;
        else
            p.replicationCooldown = std::max(p.replicationCooldown, 0.5f);
    }

    std::erase_if(replicationWindows_, [&](const auto& item) { return !liveCells.contains(item.first); });
    std::erase_if(catalystByCell_, [&](const auto& item) { return !liveCells.contains(item.first); });
    std::erase_if(catalystSawReplication_, [&](std::uint32_t id) { return !liveCells.contains(id); });
}

void StableMembraneSystem::preLifecycle(AbiogenesisSystem& system)
{
    for (PrimitiveParticle& p : system.mutableParticles())
    {
        if (p.kind == PrimitiveKind::Lipid && stableLipids_.contains(p.id))
            p.inProtoCell = true;
    }
}

void StableMembraneSystem::postLifecycle(AbiogenesisSystem& system, float dt)
{
    const float step = std::clamp(dt, 0.0f, 0.033f);
    trimInitialMaterialOnce(system);
    stabilizeClosedLoops(system, step);
    updatePeptideEnergyLifecycle(system, step);
    emitContinuousMaterial(system, step);
}

void StableMembraneSystem::trimInitialMaterialOnce(AbiogenesisSystem& system)
{
    if (initialMaterialTrimmed_) return;

    std::size_t keptLooseLipids = 0;
    std::size_t keptPeptides = 0;
    auto& particles = system.mutableParticles();
    std::erase_if(particles, [&](const PrimitiveParticle& p)
    {
        if (p.kind == PrimitiveKind::Lipid && !p.inProtoCell && !p.stableMembrane)
        {
            if (keptLooseLipids++ >= InitialLooseLipidTarget) return true;
        }
        else if (p.kind == PrimitiveKind::Peptide && !p.inProtoCell)
        {
            if (keptPeptides++ >= InitialLoosePeptideTarget) return true;
        }
        return false;
    });

    initialMaterialTrimmed_ = true;
}

void StableMembraneSystem::emitContinuousMaterial(AbiogenesisSystem& system, float dt)
{
    if (dt <= 0.0f) return;

    auto& particles = system.mutableParticles();
    std::vector<const PrimitiveParticle*> linkedRna;
    std::size_t lipidCount = 0;
    std::size_t peptideCount = 0;

    for (const PrimitiveParticle& p : particles)
    {
        if (p.kind == PrimitiveKind::Lipid) ++lipidCount;
        else if (p.kind == PrimitiveKind::Peptide) ++peptideCount;
        else if (p.kind == PrimitiveKind::RnaTriplet && (p.frontLink != 0 || p.backLink != 0))
            linkedRna.push_back(&p);
    }

    const bool focused = !linkedRna.empty();
    lipidSupplyAccumulator_ += dt * (focused ? FocusedLipidRate : AmbientLipidRate);
    peptideSupplyAccumulator_ += dt * (focused ? FocusedPeptideRate : AmbientPeptideRate);

    while (lipidSupplyAccumulator_ >= 1.0f && lipidCount < SupplyLipidCeiling)
    {
        lipidSupplyAccumulator_ -= 1.0f;
        ++supplySequence_;

        float cx = 0.10f + static_cast<float>((supplySequence_ * 37u) % 80u) / 100.0f;
        float cy = 0.18f + static_cast<float>((supplySequence_ * 23u) % 70u) / 100.0f;
        if (focused)
        {
            const PrimitiveParticle* target = linkedRna[supplySequence_ % linkedRna.size()];
            cx = target->body.x;
            cy = target->body.y;
        }

        const float angle = static_cast<float>(supplySequence_) * 2.399963f;
        const float radius = focused ? (0.030f + static_cast<float>(supplySequence_ % 5u) * 0.004f) : 0.018f;
        PrimitiveParticle& lipid = system.spawnPrimitive(
            PrimitiveKind::Lipid,
            WorldTopology::wrap01(cx + std::cos(angle) * radius),
            std::clamp(cy + std::sin(angle) * radius, 0.06f, 0.94f),
            std::cos(angle) * 0.006f,
            std::sin(angle) * 0.006f);
        lipid.lifetimeSeconds = 165.0f;
        ++lipidCount;
    }

    while (peptideSupplyAccumulator_ >= 1.0f && peptideCount < SupplyPeptideCeiling)
    {
        peptideSupplyAccumulator_ -= 1.0f;
        ++supplySequence_;

        float cx = 0.12f + static_cast<float>((supplySequence_ * 41u) % 76u) / 100.0f;
        float cy = 0.20f + static_cast<float>((supplySequence_ * 29u) % 66u) / 100.0f;
        if (focused)
        {
            const PrimitiveParticle* target = linkedRna[supplySequence_ % linkedRna.size()];
            cx = target->body.x;
            cy = target->body.y;
        }

        const float angle = static_cast<float>(supplySequence_) * 1.618034f;
        PrimitiveParticle& peptide = system.spawnPrimitive(
            PrimitiveKind::Peptide,
            WorldTopology::wrap01(cx + std::cos(angle) * 0.018f),
            std::clamp(cy + std::sin(angle) * 0.018f, 0.06f, 0.94f),
            std::cos(angle) * 0.004f,
            std::sin(angle) * 0.004f);
        peptide.peptideEnergyGraceSeconds = 32.0f;
        peptide.lifetimeSeconds = 0.0f;
        ++peptideCount;
    }
}

void StableMembraneSystem::updatePeptideEnergyLifecycle(AbiogenesisSystem& system, float dt)
{
    for (PrimitiveParticle& p : system.mutableParticles())
    {
        if (p.kind != PrimitiveKind::Peptide || p.lifetimeSeconds < 0.0f) continue;

        if (p.atpCharge > 0.0f)
        {
            p.peptideEnergyGraceSeconds = std::max(p.peptideEnergyGraceSeconds, 28.0f);
            continue;
        }

        if (p.peptideCatalysisActive)
            continue;

        p.peptideEnergyGraceSeconds = std::max(0.0f, p.peptideEnergyGraceSeconds - dt);
        if (p.peptideEnergyGraceSeconds <= 0.0f)
            p.lifetimeSeconds = -1.0f;
    }
}

void StableMembraneSystem::stabilizeClosedLoops(AbiogenesisSystem& system, float dt)
{
    auto& particles = system.mutableParticles();
    std::unordered_map<std::uint32_t, PrimitiveParticle*> byId;
    byId.reserve(particles.size() * 2);
    for (PrimitiveParticle& p : particles) byId[p.id] = &p;

    std::unordered_set<std::uint32_t> previousCellIds;
    for (const EmergentCellSnapshot& cell : cells_) previousCellIds.insert(cell.id);

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

        const float cx = componentCenterX(component);
        float cy = 0.0f;
        for (PrimitiveParticle* p : component) cy += p->body.y;
        cy /= static_cast<float>(component.size());

        float radius = 0.0f;
        for (PrimitiveParticle* p : component)
            radius += length(WorldTopology::deltaX(cx, p->body.x), p->body.y - cy);
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

            float dx = WorldTopology::deltaX(cx, lipid->body.x);
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
            const float dx = WorldTopology::deltaX(cx, p.body.x);
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

            const bool newlyFormed = !previousCellIds.contains(membraneId);
            for (PrimitiveParticle* lipid : component)
            {
                cell.lipidIds.push_back(lipid->id);
                lipid->inProtoCell = true;
                lipid->lifetimeSeconds = 0.0f;
                if (newlyFormed) lipid->cellFormationGlowSeconds = 3.0f;
            }

            for (PrimitiveParticle& p : particles)
            {
                const float dx = WorldTopology::deltaX(cx, p.body.x);
                const float dy = p.body.y - cy;
                if (dx * dx + dy * dy > interiorR2 || p.kind == PrimitiveKind::Atp) continue;

                p.inProtoCell = true;
                if (p.kind == PrimitiveKind::Peptide)
                {
                    // Peptides remain mortal even inside a cell. ATP or an active
                    // copy cycle is what keeps them around.
                    p.lifetimeSeconds = 0.0f;
                }
                else
                {
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
                if (membraneAge >= FailedMembraneLifetime) lipid->lifetimeSeconds = -1.0f;
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
