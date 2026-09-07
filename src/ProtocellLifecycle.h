#pragma once

class AbiogenesisSystem;

class ProtocellLifecycleSystem
{
public:
    void reset(AbiogenesisSystem& system);
    void update(AbiogenesisSystem& system, float dt);

private:
    bool seeded_ = false;

    void seedMaterial(AbiogenesisSystem& system);
    void normalizeParticleScales(AbiogenesisSystem& system);
    void pruneDanglingLinks(AbiogenesisSystem& system);
    void tuneEnergyRays(AbiogenesisSystem& system);
    void gateRnaBonding(AbiogenesisSystem& system);
    void shapeLipidAssemblies(AbiogenesisSystem& system, float dt);
    void classifyProtocells(AbiogenesisSystem& system);
    void applyMaterialLifetimes(AbiogenesisSystem& system);
};
