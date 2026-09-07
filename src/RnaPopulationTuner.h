#pragma once

class AbiogenesisSystem;

class RnaPopulationTuner
{
public:
    // The tuner is currently stateless, but main resets all simulation-side
    // helper systems together when starting a new world. Keep an explicit
    // reset seam so future RNA population state can be added safely.
    void reset() noexcept {}
    void update(AbiogenesisSystem& system);
};
