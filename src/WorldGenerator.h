#pragma once

#include <SDL3/SDL.h>
#include <cstdint>

struct GeneratedWorld
{
    SDL_Texture* texture = nullptr;
    int width = 0;
    int height = 0;
    std::uint32_t seed = 0;
};

class WorldGenerator
{
public:
    static GeneratedWorld generate(SDL_Renderer* renderer, int width, int height, std::uint32_t seed);
    static void destroy(GeneratedWorld& world);
};
