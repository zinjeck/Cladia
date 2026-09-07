#pragma once

#include "PhysicsField.h"

#include <cstdint>
#include <string>
#include <vector>

enum class PrimitiveKind
{
    Lipid,
    RnaTriplet,
    Peptide,
    Atp
};

struct PrimitiveParticle
{
    std::uint32_t id = 0;
    PrimitiveKind kind = PrimitiveKind::Lipid;
    ParticleBody body{};
    float ageSeconds = 0.0f;
    float lifetimeSeconds = 0.0f;

    std::string triplet;
    std::uint32_t frontLink = 0;
    std::uint32_t backLink = 0;
    bool stopTriplet = false;
    bool read = false;
    double createdAt = 0.0;

    std::uint32_t templatePartnerId = 0;
    std::uint32_t replicaTripletId = 0;
    bool replicationComplete = false;
    float replicationCooldown = 0.0f;

    std::vector<std::uint32_t> lipidLinks;
    bool inClosedLipidLoop = false;
    bool stableMembrane = false;
    float stabilizedByRna = 0.0f;
    float membraneStress = 0.0f;
    float cellFormationGlowSeconds = 0.0f;
    float lipidRebindCooldownSeconds = 0.0f;

    bool inProtoCell = false;

    float atpCharge = 0.0f;
    std::uint32_t readingTriplet = 0;
    bool excited = false;
    float peptideEnergyGraceSeconds = 32.0f;
    bool peptideCatalysisActive = false;
    bool peptideCatalysisSpent = false;
};

struct HydrothermalVent
{
    float x = 0.5f;
    float y = 0.985f;
    float phase = 0.0f;
};

struct EnergyRay
{
    float x = 0.5f;
    float y = 0.5f;
    float vx = 0.0f;
    float vy = 0.0f;
    float age = 0.0f;
    float lifetime = 1.0f;
    bool solar = false;
};

class AbiogenesisSystem
{
public:
    void reset();
    void update(float realDt, double simulationSeconds, float surfaceSolarEnergy, float sunWorldX);

    [[nodiscard]] const std::vector<PrimitiveParticle>& particles() const noexcept;
    [[nodiscard]] const std::vector<HydrothermalVent>& vents() const noexcept;
    [[nodiscard]] const std::vector<EnergyRay>& energyRays() const noexcept;

    [[nodiscard]] std::vector<PrimitiveParticle>& mutableParticles() noexcept { return particles_; }
    [[nodiscard]] std::vector<EnergyRay>& mutableEnergyRays() noexcept { return rays_; }
    PrimitiveParticle& spawnPrimitive(PrimitiveKind kind, float x, float y, float vx = 0.0f, float vy = 0.0f)
    {
        return spawnParticle(kind, x, y, vx, vy);
    }

private:
    std::uint32_t nextId_ = 1;
    double lastSimulationSeconds_ = 0.0;
    float ventNucleotideAccumulator_ = 0.0f;
    float ventAtpAccumulator_ = 0.0f;
    float rayAccumulator_ = 0.0f;
    float solarAtpAccumulator_ = 0.0f;
    std::vector<PrimitiveParticle> particles_;
    std::vector<HydrothermalVent> vents_;
    std::vector<EnergyRay> rays_;
    PhysicsField physics_;

    PrimitiveParticle& spawnParticle(PrimitiveKind kind, float x, float y, float vx = 0.0f, float vy = 0.0f);
    void emitVentProducts(float dt, double simulationSeconds);
    void emitSolarProducts(float dt, float solar, float sunWorldX);
    void updatePhysics(float dt, double simulationSeconds);
    void updateRnaLinking(float dt);
    void updateRnaReplication(float dt);
    void updateLipids(float dt);
    void updateAtpAndPeptides(float dt);
    void updatePeptideReading(float dt);
    void updateEnergyRays(float dt);
    void cullExpired();

    void applyReplicationRepulsion(float dt, const std::vector<std::uint32_t>& templateChain);
    void stressAndSplitNearbyLipidLoop(float centerX, float centerY, float axisX, float axisY, float strength);
    void breakLipidBond(std::uint32_t aId, std::uint32_t bId);

    PrimitiveParticle* find(std::uint32_t id) noexcept;
    const PrimitiveParticle* find(std::uint32_t id) const noexcept;
    bool wouldCreateRnaCycle(std::uint32_t leftId, std::uint32_t rightId) const noexcept;
    [[nodiscard]] std::vector<std::uint32_t> rnaChainFrom(std::uint32_t rootId) const;
    [[nodiscard]] bool chainHasActiveTemplatePairing(const std::vector<std::uint32_t>& chain) const;
    [[nodiscard]] static std::string complementaryTriplet(const std::string& triplet);
    static float distanceSquared(const PrimitiveParticle& a, const PrimitiveParticle& b) noexcept;
};
