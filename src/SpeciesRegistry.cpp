#include "SpeciesRegistry.h"

#include <array>
#include <utility>

SpeciesRegistry::SpeciesRegistry()
{
    SpeciesDefinition cladia;
    cladia.id = CladiaId;
    cladia.commonName = "Cladia";
    cladia.scientificName = "Cladia originis";
    cladia.dnaSummary = "UNSEQUENCED STARTER GENOME";
    cladia.playerSpawnable = true;
    cladia.generatedBySimulation = false;
    species_.push_back(cladia);
}

const SpeciesDefinition* SpeciesRegistry::find(SpeciesId id) const noexcept
{
    for (const SpeciesDefinition& species : species_)
    {
        if (species.id == id)
        {
            return &species;
        }
    }
    return nullptr;
}

const std::vector<SpeciesDefinition>& SpeciesRegistry::all() const noexcept
{
    return species_;
}

std::vector<const SpeciesDefinition*> SpeciesRegistry::spawnable() const
{
    std::vector<const SpeciesDefinition*> result;
    for (const SpeciesDefinition& species : species_)
    {
        if (species.playerSpawnable)
        {
            result.push_back(&species);
        }
    }
    return result;
}

std::vector<const SpeciesDefinition*> SpeciesRegistry::generated() const
{
    std::vector<const SpeciesDefinition*> result;
    for (const SpeciesDefinition& species : species_)
    {
        if (species.generatedBySimulation)
        {
            result.push_back(&species);
        }
    }
    return result;
}

SpeciesId SpeciesRegistry::registerGeneratedSpecies(std::uint32_t seed, std::string dnaSummary)
{
    SpeciesDefinition species;
    species.id = nextId_++;
    species.commonName = "Generated Species";
    species.scientificName = generateScientificName(seed);
    species.dnaSummary = std::move(dnaSummary);
    species.playerSpawnable = true;
    species.generatedBySimulation = true;
    species_.push_back(species);
    return species.id;
}

std::string SpeciesRegistry::generateScientificName(std::uint32_t seed)
{
    static constexpr std::array<std::string_view, 12> genera = {
        "Aqualis", "Pelagora", "Neridia", "Cytella", "Thalassia", "Virella",
        "Marinella", "Oceonema", "Primoria", "Lucentia", "Spherella", "Minutia"};
    static constexpr std::array<std::string_view, 12> epithets = {
        "minor", "profunda", "simplex", "lucida", "caerulea", "pelagica",
        "tenuis", "primitiva", "fluida", "quieta", "marina", "nova"};

    const std::string_view genus = genera[seed % genera.size()];
    const std::string_view epithet = epithets[(seed / 17u + seed * 7u) % epithets.size()];
    return std::string(genus) + " " + std::string(epithet);
}
