#include "AbiogenesisSystem.h"
#include "WorldTopology.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace
{
    constexpr std::array<const char*, 16> Triplets = {
        "AUG", "UAC", "GCU", "CGA", "CCA", "GGU", "UGG", "ACC",
        "AAA", "UUU", "GGA", "CCU", "CUU", "GAA", "UAA", "AUU"};

    constexpr float RnaLinkDistance = 0.018f;
    constexpr float LipidLinkDistance = 0.014f;
    constexpr float AtpHitDistance = 0.012f;
    constexpr float ReplicationCaptureDistance = 0.026f;
    constexpr float TemplatePairDistance = 0.009f;
    constexpr float StrandReleaseDistance = 0.034f;
}

PrimitiveParticle& AbiogenesisSystem::spawnParticle(PrimitiveKind kind, float x, float y, float vx, float vy)
{
    PrimitiveParticle particle;
    particle.id = nextId_++;
    particle.kind = kind;
    particle.body.x = WorldTopology::wrap01(x);
    particle.body.y = std::clamp(y, 0.002f, 0.998f);
    particle.body.vx = vx;
    particle.body.vy = vy;

    if (kind == PrimitiveKind::Lipid)
    {
        particle.body.radius = 0.0032f;
        particle.body.densityKgM3 = 900.0f;
        particle.lifetimeSeconds = 180.0f;
    }
    else if (kind == PrimitiveKind::RnaTriplet)
    {
        particle.body.radius = 0.0044f;
        particle.body.densityKgM3 = 1080.0f;
    }
    else if (kind == PrimitiveKind::Peptide)
    {
        particle.body.radius = 0.0028f;
        particle.body.densityKgM3 = 1060.0f;
    }
    else
    {
        particle.body.radius = 0.0022f;
        particle.body.densityKgM3 = 1040.0f;
        particle.lifetimeSeconds = 12.0f;
    }

    particles_.push_back(particle);
    return particles_.back();
}

void AbiogenesisSystem::reset()
{
    nextId_ = 1;
    lastSimulationSeconds_ = 0.0;
    ventNucleotideAccumulator_ = 0.0f;
    ventAtpAccumulator_ = 0.0f;
    rayAccumulator_ = 0.0f;
    solarAtpAccumulator_ = 0.0f;
    particles_.clear();
    rays_.clear();
    vents_ = {{0.18f, 0.985f, 0.2f}, {0.50f, 0.985f, 1.8f}, {0.82f, 0.985f, 3.3f}};

    for (int i = 0; i < 72; ++i)
    {
        const float x = 0.08f + static_cast<float>((i * 37) % 84) / 100.0f;
        const float y = 0.18f + static_cast<float>((i * 29) % 68) / 100.0f;
        spawnParticle(PrimitiveKind::Lipid, x, std::min(y, 0.92f));
    }
    for (int i = 0; i < 28; ++i)
    {
        const float x = 0.10f + static_cast<float>((i * 43) % 80) / 100.0f;
        const float y = 0.25f + static_cast<float>((i * 17) % 55) / 100.0f;
        spawnParticle(PrimitiveKind::Peptide, x, std::min(y, 0.90f));
    }
}

void AbiogenesisSystem::update(float realDt, double simulationSeconds, float surfaceSolarEnergy, float sunWorldX)
{
    const float dt = std::clamp(realDt, 0.0f, 0.033f);
    if (dt <= 0.0f) return;
    if (lastSimulationSeconds_ == 0.0) lastSimulationSeconds_ = simulationSeconds;

    emitVentProducts(dt, simulationSeconds);
    emitSolarProducts(dt, surfaceSolarEnergy, sunWorldX);
    updatePhysics(dt, simulationSeconds);
    updateRnaLinking(dt);
    updateRnaReplication(dt);
    updateLipids(dt);
    updateAtpAndPeptides(dt);
    updatePeptideReading(dt);
    updateEnergyRays(dt);
    cullExpired();
    lastSimulationSeconds_ = simulationSeconds;
}

void AbiogenesisSystem::emitVentProducts(float dt, double simulationSeconds)
{
    ventNucleotideAccumulator_ += dt * 5.5f;
    ventAtpAccumulator_ += dt * 6.5f;
    rayAccumulator_ += dt * 22.0f;

    while (ventNucleotideAccumulator_ >= 1.0f)
    {
        ventNucleotideAccumulator_ -= 1.0f;
        for (std::size_t i = 0; i < vents_.size(); ++i)
        {
            const HydrothermalVent& vent = vents_[i];
            PrimitiveParticle& rna = spawnParticle(
                PrimitiveKind::RnaTriplet,
                vent.x + std::sin(static_cast<float>(simulationSeconds * 0.01 + i)) * 0.008f,
                vent.y - 0.012f,
                std::sin(static_cast<float>(simulationSeconds * 0.017 + i * 2.0)) * 0.014f,
                -0.035f);
            const std::size_t index = static_cast<std::size_t>((rna.id * 7u + static_cast<std::uint32_t>(i) * 11u) % Triplets.size());
            rna.triplet = Triplets[index];
            rna.stopTriplet = rna.triplet == "UAA";
            rna.createdAt = simulationSeconds;
        }
    }

    while (ventAtpAccumulator_ >= 1.0f)
    {
        ventAtpAccumulator_ -= 1.0f;
        for (const HydrothermalVent& vent : vents_)
        {
            PrimitiveParticle& atp = spawnParticle(PrimitiveKind::Atp, vent.x, vent.y - 0.015f);
            atp.body.vx = std::sin(static_cast<float>(atp.id) * 1.73f) * 0.055f;
            atp.body.vy = -0.06f - std::abs(std::cos(static_cast<float>(atp.id))) * 0.03f;
        }
    }

    while (rayAccumulator_ >= 1.0f)
    {
        rayAccumulator_ -= 1.0f;
        for (std::size_t i = 0; i < vents_.size(); ++i)
        {
            const HydrothermalVent& vent = vents_[i];
            EnergyRay ray;
            ray.x = vent.x;
            ray.y = vent.y - 0.008f;
            ray.vx = std::sin(static_cast<float>(simulationSeconds * 0.03 + i * 1.7)) * 0.025f;
            ray.vy = -0.10f - static_cast<float>(i) * 0.008f;
            ray.lifetime = 1.2f;
            rays_.push_back(ray);
        }
    }
}

void AbiogenesisSystem::emitSolarProducts(float dt, float solar, float sunWorldX)
{
    if (solar <= 0.0f) return;
    rayAccumulator_ += dt * (solar / 1000.0f) * 18.0f;
    solarAtpAccumulator_ += dt * (solar / 1000.0f) * 5.0f;

    while (rayAccumulator_ >= 1.0f)
    {
        rayAccumulator_ -= 1.0f;
        EnergyRay ray;
        ray.x = WorldTopology::wrap01(sunWorldX + std::sin(static_cast<float>(rays_.size()) * 2.1f) * 0.23f);
        ray.y = 0.002f;
        ray.vx = std::sin(static_cast<float>(rays_.size()) * 1.3f) * 0.018f;
        ray.vy = 0.16f;
        ray.lifetime = 1.0f;
        ray.solar = true;
        rays_.push_back(ray);
    }

    while (solarAtpAccumulator_ >= 1.0f)
    {
        solarAtpAccumulator_ -= 1.0f;
        PrimitiveParticle& atp = spawnParticle(
            PrimitiveKind::Atp,
            WorldTopology::wrap01(sunWorldX + std::sin(static_cast<float>(nextId_)) * 0.22f),
            0.018f);
        atp.body.vx = std::sin(static_cast<float>(atp.id) * 2.7f) * 0.07f;
        atp.body.vy = 0.03f + std::abs(std::cos(static_cast<float>(atp.id))) * 0.04f;
    }
}

void AbiogenesisSystem::updatePhysics(float dt, double simulationSeconds)
{
    for (PrimitiveParticle& particle : particles_)
    {
        physics_.integrate(particle.body, dt, simulationSeconds);
        particle.ageSeconds += dt;
        particle.replicationCooldown = std::max(0.0f, particle.replicationCooldown - dt);
        particle.cellFormationGlowSeconds = std::max(0.0f, particle.cellFormationGlowSeconds - dt);
    }
}

float AbiogenesisSystem::distanceSquared(const PrimitiveParticle& a, const PrimitiveParticle& b) noexcept
{
    return WorldTopology::distanceSquared(a.body.x, a.body.y, b.body.x, b.body.y);
}

PrimitiveParticle* AbiogenesisSystem::find(std::uint32_t id) noexcept
{
    if (id == 0) return nullptr;
    for (PrimitiveParticle& p : particles_) if (p.id == id) return &p;
    return nullptr;
}

const PrimitiveParticle* AbiogenesisSystem::find(std::uint32_t id) const noexcept
{
    if (id == 0) return nullptr;
    for (const PrimitiveParticle& p : particles_) if (p.id == id) return &p;
    return nullptr;
}

bool AbiogenesisSystem::wouldCreateRnaCycle(std::uint32_t leftId, std::uint32_t rightId) const noexcept
{
    std::uint32_t cursor = rightId;
    for (int steps = 0; steps < 256 && cursor != 0; ++steps)
    {
        if (cursor == leftId) return true;
        const PrimitiveParticle* node = find(cursor);
        cursor = node ? node->backLink : 0;
    }
    return false;
}

std::vector<std::uint32_t> AbiogenesisSystem::rnaChainFrom(std::uint32_t rootId) const
{
    std::vector<std::uint32_t> chain;
    std::uint32_t cursor = rootId;
    for (int steps = 0; steps < 256 && cursor != 0; ++steps)
    {
        const PrimitiveParticle* node = find(cursor);
        if (!node || node->kind != PrimitiveKind::RnaTriplet) break;
        chain.push_back(cursor);
        cursor = node->backLink;
    }
    return chain;
}

bool AbiogenesisSystem::chainHasActiveTemplatePairing(const std::vector<std::uint32_t>& chain) const
{
    for (std::uint32_t id : chain)
    {
        const PrimitiveParticle* p = find(id);
        if (p && (p->replicaTripletId != 0 || p->templatePartnerId != 0)) return true;
    }
    return false;
}

std::string AbiogenesisSystem::complementaryTriplet(const std::string& triplet)
{
    std::string result;
    result.reserve(triplet.size());
    for (char base : triplet)
    {
        if (base == 'A') result.push_back('U');
        else if (base == 'U') result.push_back('A');
        else if (base == 'C') result.push_back('G');
        else if (base == 'G') result.push_back('C');
        else result.push_back('A');
    }
    return result;
}

void AbiogenesisSystem::updateRnaLinking(float dt)
{
    const float maxD2 = RnaLinkDistance * RnaLinkDistance;
    for (PrimitiveParticle& a : particles_)
    {
        if (a.kind != PrimitiveKind::RnaTriplet || a.backLink != 0 || a.stopTriplet || a.templatePartnerId != 0) continue;
        PrimitiveParticle* best = nullptr;
        float bestD2 = maxD2;
        for (PrimitiveParticle& b : particles_)
        {
            if (&a == &b || b.kind != PrimitiveKind::RnaTriplet || b.frontLink != 0 || b.templatePartnerId != 0) continue;
            if (wouldCreateRnaCycle(a.id, b.id)) continue;
            const float d2 = distanceSquared(a, b);
            if (d2 < bestD2) { bestD2 = d2; best = &b; }
        }
        if (best)
        {
            a.backLink = best->id;
            best->frontLink = a.id;
        }
    }

    for (PrimitiveParticle& a : particles_)
    {
        if (a.kind != PrimitiveKind::RnaTriplet || a.backLink == 0) continue;
        PrimitiveParticle* b = find(a.backLink);
        if (!b) continue;
        float dx = WorldTopology::deltaX(a.body.x, b->body.x);
        float dy = b->body.y - a.body.y;
        const float d = std::sqrt(dx * dx + dy * dy) + 1e-6f;
        const float force = (d - 0.011f) * 2.8f;
        dx /= d; dy /= d;
        a.body.vx += dx * force * dt;
        a.body.vy += dy * force * dt;
        b->body.vx -= dx * force * dt;
        b->body.vy -= dy * force * dt;
    }
}

void AbiogenesisSystem::updateRnaReplication(float dt)
{
    const float captureD2 = ReplicationCaptureDistance * ReplicationCaptureDistance;
    std::vector<std::uint32_t> roots;
    for (const PrimitiveParticle& p : particles_)
    {
        if (p.kind == PrimitiveKind::RnaTriplet && p.frontLink == 0 && p.backLink != 0 && p.replicationCooldown <= 0.0f)
            roots.push_back(p.id);
    }

    for (std::uint32_t rootId : roots)
    {
        const std::vector<std::uint32_t> chain = rnaChainFrom(rootId);
        if (chain.size() < 3) continue;

        bool complete = true;
        std::uint32_t previousReplica = 0;
        for (std::uint32_t templateId : chain)
        {
            PrimitiveParticle* templ = find(templateId);
            if (!templ) { complete = false; break; }
            PrimitiveParticle* replica = find(templ->replicaTripletId);

            if (!replica)
            {
                complete = false;
                PrimitiveParticle* candidate = nullptr;
                float bestD2 = captureD2;
                const std::string needed = complementaryTriplet(templ->triplet);
                for (PrimitiveParticle& free : particles_)
                {
                    if (free.kind != PrimitiveKind::RnaTriplet || free.id == templ->id) continue;
                    if (free.triplet != needed) continue;
                    if (free.frontLink != 0 || free.backLink != 0 || free.templatePartnerId != 0 || free.replicaTripletId != 0) continue;
                    const float d2 = distanceSquared(*templ, free);
                    if (d2 < bestD2) { bestD2 = d2; candidate = &free; }
                }

                if (candidate)
                {
                    templ->replicaTripletId = candidate->id;
                    candidate->templatePartnerId = templ->id;
                    candidate->replicationCooldown = 1.0f;
                    if (previousReplica != 0)
                    {
                        PrimitiveParticle* previous = find(previousReplica);
                        if (previous && previous->backLink == 0 && !previous->stopTriplet)
                        {
                            previous->backLink = candidate->id;
                            candidate->frontLink = previous->id;
                        }
                    }
                    replica = candidate;
                }
            }

            if (replica)
            {
                float dx = WorldTopology::deltaX(replica->body.x, templ->body.x);
                float dy = templ->body.y - replica->body.y;
                const float d = std::sqrt(dx * dx + dy * dy) + 1e-6f;
                const float force = (d - TemplatePairDistance) * 4.0f;
                dx /= d; dy /= d;
                templ->body.vx -= dx * force * dt * 0.5f;
                templ->body.vy -= dy * force * dt * 0.5f;
                replica->body.vx += dx * force * dt;
                replica->body.vy += dy * force * dt;
                previousReplica = replica->id;
            }
            else
            {
                previousReplica = 0;
            }
        }

        if (complete)
        {
            for (std::uint32_t id : chain) if (PrimitiveParticle* p = find(id)) p->replicationComplete = true;
            applyReplicationRepulsion(dt, chain);
        }
    }
}

void AbiogenesisSystem::applyReplicationRepulsion(float dt, const std::vector<std::uint32_t>& chain)
{
    float tx = 0.0f, ty = 0.0f, rx = 0.0f, ry = 0.0f, separation = 0.0f;
    float referenceX = 0.0f;
    bool haveReference = false;
    int count = 0;
    for (std::uint32_t id : chain)
    {
        PrimitiveParticle* t = find(id);
        PrimitiveParticle* r = t ? find(t->replicaTripletId) : nullptr;
        if (!t || !r) continue;
        if (!haveReference) { referenceX = t->body.x; haveReference = true; }
        tx += WorldTopology::unwrapNear(referenceX, t->body.x);
        ty += t->body.y;
        rx += WorldTopology::unwrapNear(referenceX, r->body.x);
        ry += r->body.y;
        separation += std::sqrt(distanceSquared(*t, *r));
        ++count;
    }
    if (count == 0) return;

    tx /= count; ty /= count; rx /= count; ry /= count; separation /= count;
    float ax = rx - tx;
    float ay = ry - ty;
    float length = std::sqrt(ax * ax + ay * ay);
    if (length < 0.0001f) { ax = 1.0f; ay = 0.0f; length = 1.0f; }
    ax /= length; ay /= length;

    for (std::uint32_t id : chain)
    {
        PrimitiveParticle* t = find(id);
        PrimitiveParticle* r = t ? find(t->replicaTripletId) : nullptr;
        if (!t || !r) continue;
        t->body.vx -= ax * 0.060f * dt;
        t->body.vy -= ay * 0.060f * dt;
        r->body.vx += ax * 0.060f * dt;
        r->body.vy += ay * 0.060f * dt;
    }

    const float centerX = WorldTopology::wrap01((tx + rx) * 0.5f);
    const float centerY = (ty + ry) * 0.5f;
    stressAndSplitNearbyLipidLoop(centerX, centerY, ax, ay,
        std::clamp(separation / StrandReleaseDistance, 0.0f, 1.5f));

    if (separation >= StrandReleaseDistance)
    {
        for (std::uint32_t id : chain)
        {
            PrimitiveParticle* t = find(id);
            PrimitiveParticle* r = t ? find(t->replicaTripletId) : nullptr;
            if (!t || !r) continue;
            t->replicaTripletId = 0;
            t->replicationComplete = false;
            t->replicationCooldown = 5.0f;
            r->templatePartnerId = 0;
            r->replicationComplete = false;
            r->replicationCooldown = 5.0f;
        }
    }
}

void AbiogenesisSystem::breakLipidBond(std::uint32_t aId, std::uint32_t bId)
{
    PrimitiveParticle* a = find(aId);
    PrimitiveParticle* b = find(bId);
    if (!a || !b) return;
    std::erase(a->lipidLinks, bId);
    std::erase(b->lipidLinks, aId);
    a->inClosedLipidLoop = false;
    b->inClosedLipidLoop = false;
}

void AbiogenesisSystem::stressAndSplitNearbyLipidLoop(float cx, float cy, float ax, float ay, float strength)
{
    if (strength <= 0.1f) return;
    std::vector<PrimitiveParticle*> nearby;
    for (PrimitiveParticle& lipid : particles_)
    {
        if (lipid.kind != PrimitiveKind::Lipid || !lipid.inClosedLipidLoop) continue;
        const float dx = WorldTopology::deltaX(cx, lipid.body.x);
        const float dy = lipid.body.y - cy;
        if (dx * dx + dy * dy > 0.0064f) continue;
        lipid.membraneStress = std::min(2.0f, lipid.membraneStress + strength * 0.025f);
        const float side = dx * ax + dy * ay;
        lipid.body.vx += ax * side * strength * 0.45f;
        lipid.body.vy += ay * side * strength * 0.45f;
        nearby.push_back(&lipid);
    }

    if (nearby.size() < 8) return;
    const auto stressedIt = std::max_element(nearby.begin(), nearby.end(), [](const PrimitiveParticle* a, const PrimitiveParticle* b)
    {
        return a->membraneStress < b->membraneStress;
    });
    if (stressedIt == nearby.end() || (*stressedIt)->membraneStress < 0.85f) return;

    PrimitiveParticle* negative = nullptr;
    PrimitiveParticle* positive = nullptr;
    float low = std::numeric_limits<float>::max();
    float high = -std::numeric_limits<float>::max();
    for (PrimitiveParticle* lipid : nearby)
    {
        const float projection = WorldTopology::deltaX(cx, lipid->body.x) * ax + (lipid->body.y - cy) * ay;
        if (projection < low) { low = projection; negative = lipid; }
        if (projection > high) { high = projection; positive = lipid; }
    }

    if (negative && !negative->lipidLinks.empty()) breakLipidBond(negative->id, negative->lipidLinks.front());
    if (positive && !positive->lipidLinks.empty()) breakLipidBond(positive->id, positive->lipidLinks.front());
    for (PrimitiveParticle* lipid : nearby)
    {
        lipid->membraneStress = 0.0f;
        lipid->inClosedLipidLoop = false;
        lipid->stableMembrane = false;
    }
}

void AbiogenesisSystem::updateLipids(float dt)
{
    const float maxD2 = LipidLinkDistance * LipidLinkDistance;
    for (PrimitiveParticle& lipid : particles_)
    {
        if (lipid.kind == PrimitiveKind::Lipid)
        {
            lipid.inClosedLipidLoop = false;
            lipid.membraneStress = std::max(0.0f, lipid.membraneStress - dt * 0.05f);
        }
    }

    for (PrimitiveParticle& a : particles_)
    {
        if (a.kind != PrimitiveKind::Lipid || a.lipidLinks.size() >= 2) continue;
        PrimitiveParticle* best = nullptr;
        float bestD2 = maxD2;
        for (PrimitiveParticle& b : particles_)
        {
            if (&a == &b || b.kind != PrimitiveKind::Lipid || b.lipidLinks.size() >= 2) continue;
            if (std::find(a.lipidLinks.begin(), a.lipidLinks.end(), b.id) != a.lipidLinks.end()) continue;
            const float d2 = distanceSquared(a, b);
            if (d2 < bestD2) { bestD2 = d2; best = &b; }
        }
        if (best)
        {
            a.lipidLinks.push_back(best->id);
            best->lipidLinks.push_back(a.id);
        }
    }

    for (PrimitiveParticle& a : particles_)
    {
        if (a.kind != PrimitiveKind::Lipid) continue;
        for (std::uint32_t id : a.lipidLinks)
        {
            PrimitiveParticle* b = find(id);
            if (!b || b->id < a.id) continue;
            float dx = WorldTopology::deltaX(a.body.x, b->body.x);
            float dy = b->body.y - a.body.y;
            const float d = std::sqrt(dx * dx + dy * dy) + 1e-6f;
            const float force = (d - 0.009f) * 2.0f;
            a.body.vx += dx / d * force * dt;
            a.body.vy += dy / d * force * dt;
            b->body.vx -= dx / d * force * dt;
            b->body.vy -= dy / d * force * dt;
        }

        a.stabilizedByRna = 0.0f;
        for (const PrimitiveParticle& rna : particles_)
        {
            if (rna.kind == PrimitiveKind::RnaTriplet && distanceSquared(a, rna) < 0.0012f)
            {
                a.stabilizedByRna = 1.0f;
                break;
            }
        }
        if (a.stabilizedByRna > 0.0f) a.ageSeconds = std::max(0.0f, a.ageSeconds - dt * 0.80f);
    }

    for (PrimitiveParticle& lipid : particles_)
    {
        if (lipid.kind != PrimitiveKind::Lipid || lipid.lipidLinks.size() != 2) continue;
        std::uint32_t previous = lipid.id;
        std::uint32_t cursor = lipid.lipidLinks.front();
        for (int steps = 0; steps < 64; ++steps)
        {
            PrimitiveParticle* node = find(cursor);
            if (!node || node->kind != PrimitiveKind::Lipid || node->lipidLinks.size() != 2) break;
            const std::uint32_t next = node->lipidLinks[0] == previous ? node->lipidLinks[1] : node->lipidLinks[0];
            if (next == lipid.id && steps >= 4) { lipid.inClosedLipidLoop = true; break; }
            previous = cursor;
            cursor = next;
        }
    }
}

void AbiogenesisSystem::updateAtpAndPeptides(float dt)
{
    for (PrimitiveParticle& atp : particles_)
    {
        if (atp.kind != PrimitiveKind::Atp || atp.lifetimeSeconds < 0.0f) continue;
        for (PrimitiveParticle& peptide : particles_)
        {
            if (peptide.kind != PrimitiveKind::Peptide) continue;
            if (distanceSquared(atp, peptide) <= AtpHitDistance * AtpHitDistance)
            {
                peptide.atpCharge = std::min(3.0f, peptide.atpCharge + 1.0f);
                peptide.excited = true;
                atp.lifetimeSeconds = -1.0f;
                break;
            }
        }
    }

    for (PrimitiveParticle& peptide : particles_)
    {
        if (peptide.kind != PrimitiveKind::Peptide || !peptide.excited || peptide.readingTriplet != 0) continue;
        PrimitiveParticle* target = nullptr;
        double oldest = std::numeric_limits<double>::max();
        for (PrimitiveParticle& rna : particles_)
        {
            if (rna.kind != PrimitiveKind::RnaTriplet || rna.read || rna.templatePartnerId != 0) continue;
            if (rna.createdAt < oldest) { oldest = rna.createdAt; target = &rna; }
        }
        if (!target) continue;

        float dx = WorldTopology::deltaX(peptide.body.x, target->body.x);
        float dy = target->body.y - peptide.body.y;
        const float d = std::sqrt(dx * dx + dy * dy) + 1e-6f;
        peptide.body.vx += dx / d * 0.065f * dt;
        peptide.body.vy += dy / d * 0.065f * dt;
        if (d < 0.010f && peptide.atpCharge >= 1.0f)
        {
            peptide.readingTriplet = target->id;
            target->read = true;
        }
    }
}

void AbiogenesisSystem::updatePeptideReading(float dt)
{
    for (PrimitiveParticle& peptide : particles_)
    {
        if (peptide.kind != PrimitiveKind::Peptide || peptide.readingTriplet == 0) continue;
        PrimitiveParticle* current = find(peptide.readingTriplet);
        if (!current) { peptide.readingTriplet = 0; continue; }

        const float t = std::clamp(dt * 12.0f, 0.0f, 1.0f);
        const float dx = WorldTopology::deltaX(peptide.body.x, current->body.x);
        peptide.body.x = WorldTopology::wrap01(peptide.body.x + dx * t);
        peptide.body.y += (current->body.y - peptide.body.y) * t;
        if (current->stopTriplet)
        {
            peptide.readingTriplet = 0;
            peptide.excited = peptide.atpCharge > 0.0f;
        }
        else if (current->backLink != 0)
        {
            PrimitiveParticle* next = find(current->backLink);
            if (next) { next->read = true; peptide.readingTriplet = next->id; }
        }
    }
}

void AbiogenesisSystem::updateEnergyRays(float dt)
{
    for (EnergyRay& ray : rays_)
    {
        ray.x = WorldTopology::wrap01(ray.x + ray.vx * dt);
        ray.y += ray.vy * dt;
        ray.age += dt;
    }
    std::erase_if(rays_, [](const EnergyRay& ray) { return ray.age >= ray.lifetime; });
}

void AbiogenesisSystem::cullExpired()
{
    std::erase_if(particles_, [](const PrimitiveParticle& p)
    {
        return p.lifetimeSeconds < 0.0f || (p.lifetimeSeconds > 0.0f && p.ageSeconds >= p.lifetimeSeconds);
    });
}

const std::vector<PrimitiveParticle>& AbiogenesisSystem::particles() const noexcept { return particles_; }
const std::vector<HydrothermalVent>& AbiogenesisSystem::vents() const noexcept { return vents_; }
const std::vector<EnergyRay>& AbiogenesisSystem::energyRays() const noexcept { return rays_; }
