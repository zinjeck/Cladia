#include "AbiogenesisSystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace
{
    constexpr std::array<const char*, 16> Triplets = {
        "AUG", "GCU", "CCA", "UGG", "AAA", "GGA", "CUU", "ACG",
        "UUC", "GUC", "CAU", "AGC", "UAC", "CGU", "UAA", "GAU"};

    constexpr float RnaLinkDistance = 0.018f;
    constexpr float LipidLinkDistance = 0.014f;
    constexpr float AtpHitDistance = 0.012f;
}

PrimitiveParticle& AbiogenesisSystem::spawnParticle(PrimitiveKind kind, float x, float y, float vx, float vy)
{
    PrimitiveParticle particle;
    particle.id = nextId_++;
    particle.kind = kind;
    particle.body.x = std::clamp(x, 0.0f, 1.0f);
    particle.body.y = std::clamp(y, 0.002f, 0.998f);
    particle.body.vx = vx;
    particle.body.vy = vy;

    switch (kind)
    {
    case PrimitiveKind::Lipid:
        particle.body.radius = 0.0032f;
        particle.body.densityKgM3 = 900.0f;
        particle.lifetimeSeconds = 180.0f;
        break;
    case PrimitiveKind::RnaTriplet:
        particle.body.radius = 0.0044f;
        particle.body.densityKgM3 = 1080.0f;
        particle.lifetimeSeconds = 0.0f;
        break;
    case PrimitiveKind::Peptide:
        particle.body.radius = 0.0028f;
        particle.body.densityKgM3 = 1060.0f;
        particle.lifetimeSeconds = 0.0f;
        break;
    case PrimitiveKind::Atp:
        particle.body.radius = 0.0022f;
        particle.body.densityKgM3 = 1040.0f;
        particle.lifetimeSeconds = 12.0f;
        break;
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

    // The world starts with primitive material, not completed cells.
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
            PrimitiveParticle& nucleotide = spawnParticle(
                PrimitiveKind::RnaTriplet,
                vent.x + std::sin(static_cast<float>(simulationSeconds * 0.01 + i)) * 0.008f,
                vent.y - 0.012f,
                std::sin(static_cast<float>(simulationSeconds * 0.017 + i * 2.0)) * 0.014f,
                -0.035f);
            const std::size_t index = static_cast<std::size_t>((nucleotide.id * 7u + static_cast<std::uint32_t>(i) * 11u) % Triplets.size());
            nucleotide.triplet = Triplets[index];
            nucleotide.stopTriplet = nucleotide.triplet == "UAA";
            nucleotide.createdAt = simulationSeconds;
        }
    }

    while (ventAtpAccumulator_ >= 1.0f)
    {
        ventAtpAccumulator_ -= 1.0f;
        for (std::size_t i = 0; i < vents_.size(); ++i)
        {
            const HydrothermalVent& vent = vents_[i];
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
            ray.solar = false;
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
        ray.x = std::clamp(sunWorldX + std::sin(static_cast<float>(rays_.size()) * 2.1f) * 0.23f, 0.02f, 0.98f);
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
        PrimitiveParticle& atp = spawnParticle(PrimitiveKind::Atp,
            std::clamp(sunWorldX + std::sin(static_cast<float>(nextId_)) * 0.22f, 0.03f, 0.97f),
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
    }
}

float AbiogenesisSystem::distanceSquared(const PrimitiveParticle& a, const PrimitiveParticle& b) noexcept
{
    const float dx = a.body.x - b.body.x;
    const float dy = a.body.y - b.body.y;
    return dx * dx + dy * dy;
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

void AbiogenesisSystem::updateRnaLinking(float dt)
{
    (void)dt;
    const float maxD2 = RnaLinkDistance * RnaLinkDistance;

    for (PrimitiveParticle& a : particles_)
    {
        if (a.kind != PrimitiveKind::RnaTriplet || a.backLink != 0 || a.stopTriplet) continue;

        PrimitiveParticle* best = nullptr;
        float bestD2 = maxD2;
        for (PrimitiveParticle& b : particles_)
        {
            if (&a == &b || b.kind != PrimitiveKind::RnaTriplet || b.frontLink != 0) continue;
            if (wouldCreateRnaCycle(a.id, b.id)) continue;
            const float d2 = distanceSquared(a, b);
            if (d2 < bestD2)
            {
                bestD2 = d2;
                best = &b;
            }
        }

        if (best)
        {
            a.backLink = best->id;
            best->frontLink = a.id;
        }
    }

    // Directional backbone springs. Sequence does not determine polymerization;
    // the front/back geometry represents the 5'-to-3' direction of RNA.
    for (PrimitiveParticle& a : particles_)
    {
        if (a.kind != PrimitiveKind::RnaTriplet || a.backLink == 0) continue;
        PrimitiveParticle* b = find(a.backLink);
        if (!b) continue;
        float dx = b->body.x - a.body.x;
        float dy = b->body.y - a.body.y;
        const float d = std::sqrt(dx * dx + dy * dy) + 1e-6f;
        const float error = d - 0.011f;
        const float force = error * 2.8f;
        dx /= d;
        dy /= d;
        a.body.vx += dx * force * dt;
        a.body.vy += dy * force * dt;
        b->body.vx -= dx * force * dt;
        b->body.vy -= dy * force * dt;
    }
}

void AbiogenesisSystem::updateLipids(float dt)
{
    const float maxD2 = LipidLinkDistance * LipidLinkDistance;

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
            if (d2 < bestD2)
            {
                bestD2 = d2;
                best = &b;
            }
        }
        if (best)
        {
            a.lipidLinks.push_back(best->id);
            best->lipidLinks.push_back(a.id);
        }
    }

    // Linked lipids contract gently. Degree-two chains naturally bend in the
    // turbulent field; when both ends of a chain meet they close into a loop.
    for (PrimitiveParticle& a : particles_)
    {
        if (a.kind != PrimitiveKind::Lipid) continue;
        for (std::uint32_t id : a.lipidLinks)
        {
            PrimitiveParticle* b = find(id);
            if (!b || b->id < a.id) continue;
            float dx = b->body.x - a.body.x;
            float dy = b->body.y - a.body.y;
            const float d = std::sqrt(dx * dx + dy * dy) + 1e-6f;
            const float error = d - 0.009f;
            const float force = error * 2.0f;
            a.body.vx += dx / d * force * dt;
            a.body.vy += dy / d * force * dt;
            b->body.vx -= dx / d * force * dt;
            b->body.vy -= dy / d * force * dt;
        }

        // RNA proximity slows lipid degradation, approximating stabilization by
        // encapsulated polymer without declaring the structure a cell.
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

    // Mark closed lipid networks when walking degree-two links returns to start.
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
            if (next == lipid.id && steps >= 4)
            {
                lipid.inClosedLipidLoop = true;
                break;
            }
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
            if (rna.kind != PrimitiveKind::RnaTriplet || rna.read) continue;
            if (rna.createdAt < oldest)
            {
                oldest = rna.createdAt;
                target = &rna;
            }
        }

        if (target)
        {
            float dx = target->body.x - peptide.body.x;
            float dy = target->body.y - peptide.body.y;
            const float d = std::sqrt(dx * dx + dy * dy) + 1e-6f;
            peptide.body.vx += dx / d * 0.065f * dt;
            peptide.body.vy += dy / d * 0.065f * dt;
            if (d < 0.010f && peptide.atpCharge >= 1.0f)
            {
                peptide.atpCharge -= 1.0f;
                peptide.readingTriplet = target->id;
                target->read = true;
            }
        }
    }
}

void AbiogenesisSystem::updatePeptideReading(float dt)
{
    for (PrimitiveParticle& peptide : particles_)
    {
        if (peptide.kind != PrimitiveKind::Peptide || peptide.readingTriplet == 0) continue;
        PrimitiveParticle* current = find(peptide.readingTriplet);
        if (!current)
        {
            peptide.readingTriplet = 0;
            continue;
        }

        peptide.body.x += (current->body.x - peptide.body.x) * std::clamp(dt * 12.0f, 0.0f, 1.0f);
        peptide.body.y += (current->body.y - peptide.body.y) * std::clamp(dt * 12.0f, 0.0f, 1.0f);

        if (current->stopTriplet)
        {
            // UAA terminates the reading pass. This is the first codon meaning
            // explicitly defined for Cladia; all other triplets remain inert.
            peptide.readingTriplet = 0;
            peptide.excited = peptide.atpCharge > 0.0f;
            continue;
        }

        if (current->backLink != 0)
        {
            PrimitiveParticle* next = find(current->backLink);
            if (next)
            {
                next->read = true;
                peptide.readingTriplet = next->id;
            }
        }
    }
}

void AbiogenesisSystem::updateEnergyRays(float dt)
{
    for (EnergyRay& ray : rays_)
    {
        ray.x += ray.vx * dt;
        ray.y += ray.vy * dt;
        ray.age += dt;
    }
    std::erase_if(rays_, [](const EnergyRay& ray) { return ray.age >= ray.lifetime; });
}

void AbiogenesisSystem::cullExpired()
{
    std::erase_if(particles_, [](const PrimitiveParticle& particle)
    {
        return particle.lifetimeSeconds < 0.0f ||
            (particle.lifetimeSeconds > 0.0f && particle.ageSeconds >= particle.lifetimeSeconds);
    });
}

PrimitiveParticle* AbiogenesisSystem::find(std::uint32_t id) noexcept
{
    if (id == 0) return nullptr;
    for (PrimitiveParticle& particle : particles_) if (particle.id == id) return &particle;
    return nullptr;
}

const PrimitiveParticle* AbiogenesisSystem::find(std::uint32_t id) const noexcept
{
    if (id == 0) return nullptr;
    for (const PrimitiveParticle& particle : particles_) if (particle.id == id) return &particle;
    return nullptr;
}

const std::vector<PrimitiveParticle>& AbiogenesisSystem::particles() const noexcept { return particles_; }
const std::vector<HydrothermalVent>& AbiogenesisSystem::vents() const noexcept { return vents_; }
const std::vector<EnergyRay>& AbiogenesisSystem::energyRays() const noexcept { return rays_; }
