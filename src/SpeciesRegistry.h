#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

using SpeciesId = std::uint32_t;

struct SpeciesDefinition
{
    SpeciesId id = 0;
    std::string commonName;
    std::string scientificName;
    std::string dnaSummary;
    bool playerSpawnable = false;
    bool generatedBySimulation = false;
};

class SpeciesRegistry
{
public:
    SpeciesRegistry();

    [[nodiscard]] const SpeciesDefinition* find(SpeciesId id) const noexcept;
    [[nodiscard]] const std::vector<SpeciesDefinition>& all() const noexcept;
    [[nodiscard]] std::vector<const SpeciesDefinition*> spawnable() const;
    [[nodiscard]] std::vector<const SpeciesDefinition*> generated() const;

    // Reserved for future speciation. Generated species receive stable
    // pseudo-Latin binomials from the supplied seed.
    SpeciesId registerGeneratedSpecies(std::uint32_t seed, std::string dnaSummary);

    static std::string generateScientificName(std::uint32_t seed);

    static constexpr SpeciesId CladiaId = 1;

private:
    SpeciesId nextId_ = 2;
    std::vector<SpeciesDefinition> species_;
};
