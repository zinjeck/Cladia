#include "WorldGenerator.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace
{
    float smoothstep(float t)
    {
        return t * t * (3.0f - 2.0f * t);
    }

    std::uint32_t hash2d(int x, int y, std::uint32_t seed)
    {
        std::uint32_t h = static_cast<std::uint32_t>(x) * 0x8da6b343u;
        h ^= static_cast<std::uint32_t>(y) * 0xd8163841u;
        h ^= seed * 0xcb1ab31fu;
        h ^= h >> 13;
        h *= 0x85ebca6bu;
        h ^= h >> 16;
        return h;
    }

    float random01(int x, int y, std::uint32_t seed)
    {
        return static_cast<float>(hash2d(x, y, seed) & 0x00ffffffu) / 16777215.0f;
    }

    float valueNoise(float x, float y, std::uint32_t seed)
    {
        const int x0 = static_cast<int>(std::floor(x));
        const int y0 = static_cast<int>(std::floor(y));
        const int x1 = x0 + 1;
        const int y1 = y0 + 1;

        const float tx = smoothstep(x - static_cast<float>(x0));
        const float ty = smoothstep(y - static_cast<float>(y0));

        const float a = random01(x0, y0, seed);
        const float b = random01(x1, y0, seed);
        const float c = random01(x0, y1, seed);
        const float d = random01(x1, y1, seed);

        const float top = a + (b - a) * tx;
        const float bottom = c + (d - c) * tx;
        return top + (bottom - top) * ty;
    }

    float fractalNoise(float x, float y, std::uint32_t seed)
    {
        float total = 0.0f;
        float amplitude = 0.5f;
        float frequency = 1.0f;
        float normalizer = 0.0f;

        for (int octave = 0; octave < 6; ++octave)
        {
            total += valueNoise(x * frequency, y * frequency, seed + static_cast<std::uint32_t>(octave * 1013)) * amplitude;
            normalizer += amplitude;
            amplitude *= 0.5f;
            frequency *= 2.0f;
        }

        return total / normalizer;
    }

    SDL_Color biomeColor(float elevation, float moisture, float latitude)
    {
        if (elevation < 0.42f)
        {
            const float depth = std::clamp((0.42f - elevation) / 0.42f, 0.0f, 1.0f);
            return SDL_Color{
                static_cast<Uint8>(22 + 18 * (1.0f - depth)),
                static_cast<Uint8>(78 + 54 * (1.0f - depth)),
                static_cast<Uint8>(128 + 72 * (1.0f - depth)),
                255};
        }

        if (elevation > 0.80f)
        {
            const float snow = std::clamp((elevation - 0.80f) / 0.20f + latitude * 0.35f, 0.0f, 1.0f);
            const Uint8 c = static_cast<Uint8>(135 + 105 * snow);
            return SDL_Color{c, c, static_cast<Uint8>(c + (c < 245 ? 8 : 0)), 255};
        }

        if (latitude > 0.72f)
        {
            return SDL_Color{105, 135, 118, 255};
        }

        if (moisture < 0.30f)
        {
            return SDL_Color{194, 166, 92, 255};
        }

        if (moisture > 0.68f)
        {
            return SDL_Color{44, 116, 72, 255};
        }

        return SDL_Color{92, 148, 84, 255};
    }
}

GeneratedWorld WorldGenerator::generate(SDL_Renderer* renderer, int width, int height, std::uint32_t seed)
{
    GeneratedWorld world;
    world.width = width;
    world.height = height;
    world.seed = seed;

    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const float nx = static_cast<float>(x) / static_cast<float>(width);
            const float ny = static_cast<float>(y) / static_cast<float>(height);

            const float warpX = fractalNoise(nx * 3.0f + 17.3f, ny * 3.0f + 9.1f, seed + 33u) - 0.5f;
            const float warpY = fractalNoise(nx * 3.0f + 41.7f, ny * 3.0f + 27.5f, seed + 71u) - 0.5f;

            const float terrain = fractalNoise(
                nx * 4.0f + warpX * 0.85f,
                ny * 4.0f + warpY * 0.85f,
                seed);

            const float dx = nx - 0.5f;
            const float dy = ny - 0.5f;
            const float radial = std::sqrt(dx * dx + dy * dy) / 0.70710678f;
            const float continentalFalloff = std::clamp(1.0f - radial * radial * 0.80f, 0.0f, 1.0f);

            const float elevation = std::clamp(terrain * 0.92f + continentalFalloff * 0.22f - 0.10f, 0.0f, 1.0f);
            const float moisture = fractalNoise(nx * 5.0f + 90.0f, ny * 5.0f + 12.0f, seed + 911u);
            const float latitude = std::abs(ny - 0.5f) * 2.0f;

            const SDL_Color color = biomeColor(elevation, moisture, latitude);
            const std::uint32_t packed =
                (static_cast<std::uint32_t>(color.a) << 24) |
                (static_cast<std::uint32_t>(color.b) << 16) |
                (static_cast<std::uint32_t>(color.g) << 8) |
                static_cast<std::uint32_t>(color.r);

            pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] = packed;
        }
    }

    world.texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, width, height);
    if (world.texture != nullptr)
    {
        SDL_SetTextureScaleMode(world.texture, SDL_SCALEMODE_LINEAR);
        SDL_UpdateTexture(world.texture, nullptr, pixels.data(), width * static_cast<int>(sizeof(std::uint32_t)));
    }

    return world;
}

void WorldGenerator::destroy(GeneratedWorld& world)
{
    if (world.texture != nullptr)
    {
        SDL_DestroyTexture(world.texture);
        world.texture = nullptr;
    }
}
