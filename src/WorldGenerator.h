#pragma once

#include <SDL3/SDL.h>
#include <cstdint>

struct WorldSettings
{
    float averageElevation = 0.50f;
    float temperature = 0.50f;
    float waterLevel = 0.56f;
    float moisture = 0.50f;
    float continentScale = 0.50f;
};

struct GeneratedWorld
{
    SDL_Texture* texture = nullptr;
    int width = 0;
    int height = 0;
    float worldWidth = 20000.0f;
    float worldHeight = 11250.0f;
    std::uint32_t seed = 0;
};

class WorldGenerator
{
public:
    static GeneratedWorld generate(
        SDL_Renderer* renderer,
        int width,
        int height,
        std::uint32_t seed,
        const WorldSettings& settings);

    static void destroy(GeneratedWorld& world);
};
