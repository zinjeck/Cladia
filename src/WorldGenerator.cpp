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

    SDL_Color biomeColor(float elevation, float waterLevel, float moisture, float temperature, float latitude)
    {
        if (elevation < waterLevel)
        {
            const float depth = std::clamp((waterLevel - elevation) / std::max(waterLevel, 0.01f), 0.0f, 1.0f);
            return SDL_Color{
                static_cast<Uint8>(18 + 30 * (1.0f - depth)),
                static_cast<Uint8>(64 + 75 * (1.0f - depth)),
                static_cast<Uint8>(118 + 92 * (1.0f - depth)),
                255};
        }

        const float thermalLatitude = std::clamp(latitude + (0.5f - temperature) * 0.65f, 0.0f, 1.0f);

        if (elevation > waterLevel + 0.29f)
        {
            const float snow = std::clamp((elevation - waterLevel - 0.29f) / 0.18f + thermalLatitude * 0.45f, 0.0f, 1.0f);
            const Uint8 c = static_cast<Uint8>(132 + 108 * snow);
            return SDL_Color{c, c, static_cast<Uint8>(std::min(255, static_cast<int>(c) + 8)), 255};
        }

        if (thermalLatitude > 0.76f)
        {
            return SDL_Color{111, 139, 124, 255};
        }

        const float adjustedMoisture = std::clamp(moisture, 0.0f, 1.0f);
        if (adjustedMoisture < 0.31f)
        {
            return temperature > 0.62f
                ? SDL_Color{201, 172, 94, 255}
                : SDL_Color{165, 157, 102, 255};
        }

        if (adjustedMoisture > 0.69f)
        {
            return temperature > 0.58f
                ? SDL_Color{39, 117, 68, 255}
                : SDL_Color{51, 111, 72, 255};
        }

        return temperature > 0.58f
            ? SDL_Color{101, 151, 78, 255}
            : SDL_Color{88, 143, 86, 255};
    }
}

GeneratedWorld WorldGenerator::generate(
    SDL_Renderer* renderer,
    int width,
    int height,
    std::uint32_t seed,
    const WorldSettings& settings)
{
    GeneratedWorld world;
    world.width = width;
    world.height = height;
    world.seed = seed;

    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));

    const float continentFrequency = 1.65f + settings.continentScale * 2.10f;
    const float elevationBias = (settings.averageElevation - 0.5f) * 0.34f;
    const float effectiveWaterLevel = 0.46f + settings.waterLevel * 0.20f;

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const float nx = static_cast<float>(x) / static_cast<float>(width);
            const float ny = static_cast<float>(y) / static_cast<float>(height);

            const float warpX = fractalNoise(nx * 2.4f + 17.3f, ny * 2.4f + 9.1f, seed + 33u) - 0.5f;
            const float warpY = fractalNoise(nx * 2.4f + 41.7f, ny * 2.4f + 27.5f, seed + 71u) - 0.5f;

            const float broadContinents = fractalNoise(
                nx * continentFrequency + warpX * 0.78f,
                ny * continentFrequency + warpY * 0.78f,
                seed);

            const float detail = fractalNoise(
                nx * 7.5f + warpX * 0.35f,
                ny * 7.5f + warpY * 0.35f,
                seed + 401u);

            const float edgeX = std::min(nx, 1.0f - nx);
            const float edgeY = std::min(ny, 1.0f - ny);
            const float edgeDistance = std::min(edgeX, edgeY);
            const float oceanMargin = smoothstep(std::clamp(edgeDistance / 0.12f, 0.0f, 1.0f));

            const float elevation = std::clamp(
                broadContinents * 0.78f + detail * 0.22f + elevationBias - (1.0f - oceanMargin) * 0.30f,
                0.0f,
                1.0f);

            float moisture = fractalNoise(nx * 4.2f + 90.0f, ny * 4.2f + 12.0f, seed + 911u);
            moisture = std::clamp(moisture + (settings.moisture - 0.5f) * 0.55f, 0.0f, 1.0f);
            const float latitude = std::abs(ny - 0.5f) * 2.0f;

            const SDL_Color color = biomeColor(elevation, effectiveWaterLevel, moisture, settings.temperature, latitude);
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
