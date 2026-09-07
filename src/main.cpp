#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "AquaticWorld.h"
#include "CellSystem.h"
#include "SimulationClock.h"
#include "SpeciesRegistry.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <numbers>
#include <sstream>
#include <string>
#include <string_view>

namespace
{
    constexpr float SkyTop = -0.22f;
    constexpr float OceanSurface = 0.0f;
    constexpr float OceanBottom = 1.0f;
    constexpr float WorldHeight = OceanBottom - SkyTop;

    enum class Screen
    {
        MainMenu,
        Game,
        CellEditor,
        SpeciesLibrary
    };

    struct Button
    {
        SDL_FRect rect{};
        std::string_view label{};
    };

    struct Camera
    {
        float centerX = 0.5f;
        float centerY = 0.44f;
        float zoom = 1.8f;
    };

    TTF_Font* uiFont = nullptr;

    void drawText(SDL_Renderer* renderer, std::string_view text, float x, float y, float pointSize, SDL_Color color)
    {
        if (uiFont == nullptr || text.empty()) return;
        if (!TTF_SetFontSize(uiFont, pointSize)) return;

        SDL_Surface* surface = TTF_RenderText_Blended(uiFont, text.data(), text.size(), color);
        if (surface == nullptr) return;

        SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
        if (texture != nullptr)
        {
            SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
            SDL_FRect destination{x, y, static_cast<float>(surface->w), static_cast<float>(surface->h)};
            SDL_RenderTexture(renderer, texture, nullptr, &destination);
            SDL_DestroyTexture(texture);
        }
        SDL_DestroySurface(surface);
    }

    float textWidth(std::string_view text, float pointSize)
    {
        if (uiFont == nullptr || text.empty()) return 0.0f;
        if (!TTF_SetFontSize(uiFont, pointSize)) return 0.0f;
        int width = 0;
        int height = 0;
        return TTF_GetStringSize(uiFont, text.data(), text.size(), &width, &height)
            ? static_cast<float>(width)
            : 0.0f;
    }

    bool pointInside(const SDL_FRect& rect, float x, float y)
    {
        return x >= rect.x && x <= rect.x + rect.w && y >= rect.y && y <= rect.y + rect.h;
    }

    void drawButton(SDL_Renderer* renderer, const Button& button, float mouseX, float mouseY, bool active = false)
    {
        const bool hovered = pointInside(button.rect, mouseX, mouseY);
        const SDL_Color fill = active
            ? SDL_Color{52, 132, 154, 255}
            : hovered ? SDL_Color{38, 79, 94, 255} : SDL_Color{25, 52, 64, 255};

        SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
        SDL_RenderFillRect(renderer, &button.rect);
        SDL_SetRenderDrawColor(renderer, 72, 126, 143, 255);
        SDL_RenderRect(renderer, &button.rect);

        constexpr float textSize = 16.0f;
        drawText(renderer, button.label,
            button.rect.x + (button.rect.w - textWidth(button.label, textSize)) * 0.5f,
            button.rect.y + 10.0f,
            textSize,
            SDL_Color{226, 239, 241, 255});
    }

    float cameraViewWidth(const Camera& camera)
    {
        return 1.0f / camera.zoom;
    }

    float cameraViewHeight(const Camera& camera)
    {
        return WorldHeight / camera.zoom;
    }

    void clampCamera(Camera& camera)
    {
        camera.zoom = std::clamp(camera.zoom, 1.0f, 4.0f);
        const float halfW = cameraViewWidth(camera) * 0.5f;
        const float halfH = cameraViewHeight(camera) * 0.5f;
        camera.centerX = std::clamp(camera.centerX, halfW, 1.0f - halfW);
        camera.centerY = std::clamp(camera.centerY, SkyTop + halfH, OceanBottom - halfH);
    }

    SDL_FPoint worldToScreen(float x, float y, const Camera& camera, int width, int height)
    {
        const float viewW = cameraViewWidth(camera);
        const float viewH = cameraViewHeight(camera);
        const float left = camera.centerX - viewW * 0.5f;
        const float top = camera.centerY - viewH * 0.5f;
        return SDL_FPoint{
            ((x - left) / viewW) * static_cast<float>(width),
            58.0f + ((y - top) / viewH) * static_cast<float>(height - 58)};
    }

    SDL_FPoint screenToWorld(float x, float y, const Camera& camera, int width, int height)
    {
        const float viewW = cameraViewWidth(camera);
        const float viewH = cameraViewHeight(camera);
        const float left = camera.centerX - viewW * 0.5f;
        const float top = camera.centerY - viewH * 0.5f;
        const float u = std::clamp(x / static_cast<float>(std::max(width, 1)), 0.0f, 1.0f);
        const float v = std::clamp((y - 58.0f) / static_cast<float>(std::max(height - 58, 1)), 0.0f, 1.0f);
        return SDL_FPoint{left + u * viewW, top + v * viewH};
    }

    void changeZoom(Camera& camera, float factor, float mouseX, float mouseY, int width, int height)
    {
        if (width <= 0 || height <= 58) return;

        const SDL_FPoint before = screenToWorld(mouseX, mouseY, camera, width, height);
        camera.zoom = std::clamp(camera.zoom * factor, 1.0f, 4.0f);

        const float viewW = cameraViewWidth(camera);
        const float viewH = cameraViewHeight(camera);
        const float u = std::clamp(mouseX / static_cast<float>(width), 0.0f, 1.0f);
        const float v = std::clamp((mouseY - 58.0f) / static_cast<float>(height - 58), 0.0f, 1.0f);
        camera.centerX = before.x - (u - 0.5f) * viewW;
        camera.centerY = before.y - (v - 0.5f) * viewH;
        clampCamera(camera);
    }

    void updateCamera(Camera& camera, float dt, int width, int height, float mouseX, float mouseY)
    {
        const bool* keys = SDL_GetKeyboardState(nullptr);
        float dx = 0.0f;
        float dy = 0.0f;
        if (keys[SDL_SCANCODE_A]) dx -= 1.0f;
        if (keys[SDL_SCANCODE_D]) dx += 1.0f;
        if (keys[SDL_SCANCODE_W]) dy -= 1.0f;
        if (keys[SDL_SCANCODE_S]) dy += 1.0f;

        constexpr float edge = 14.0f;
        if (mouseX <= edge) dx -= 1.0f;
        if (mouseX >= static_cast<float>(width) - edge) dx += 1.0f;
        if (mouseY >= 58.0f && mouseY <= 58.0f + edge) dy -= 1.0f;
        if (mouseY >= static_cast<float>(height) - edge) dy += 1.0f;
        if (dx == 0.0f && dy == 0.0f) return;

        const float length = std::sqrt(dx * dx + dy * dy);
        const float horizontalSpeed = 0.46f / camera.zoom;
        const float verticalSpeed = 0.56f / camera.zoom;
        camera.centerX += (dx / length) * horizontalSpeed * dt;
        camera.centerY += (dy / length) * verticalSpeed * dt;
        clampCamera(camera);
    }

    float timeOfDayFraction(const SimulationClock& clock)
    {
        constexpr double SecondsPerDay = 86400.0;
        const double seconds = std::fmod(clock.elapsedSimulationSeconds(), SecondsPerDay);
        return static_cast<float>(seconds / SecondsPerDay);
    }

    float surfaceSolarEnergy(const SimulationClock& clock)
    {
        const float t = timeOfDayFraction(clock);
        if (t < 0.25f || t >= 0.75f) return 0.0f;
        const float dayProgress = (t - 0.25f) / 0.50f;
        return std::sin(dayProgress * std::numbers::pi_v<float>) * 1000.0f;
    }

    SDL_FPoint celestialWorldPosition(const SimulationClock& clock, bool sun)
    {
        const float t = timeOfDayFraction(clock);
        float progress = 0.0f;
        if (sun)
        {
            progress = std::clamp((t - 0.25f) / 0.50f, 0.0f, 1.0f);
        }
        else
        {
            progress = t >= 0.75f ? (t - 0.75f) / 0.50f : (t + 0.25f) / 0.50f;
        }

        return SDL_FPoint{
            0.06f + progress * 0.88f,
            -0.075f - std::sin(progress * std::numbers::pi_v<float>) * 0.095f};
    }

    bool sunVisibleByTime(const SimulationClock& clock)
    {
        const float t = timeOfDayFraction(clock);
        return t >= 0.25f && t < 0.75f;
    }

    void drawFilledCircle(SDL_Renderer* renderer, float cx, float cy, float radius, SDL_Color color)
    {
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
        const int r = static_cast<int>(std::ceil(radius));
        for (int y = -r; y <= r; ++y)
        {
            const float half = std::sqrt(std::max(0.0f, radius * radius - static_cast<float>(y * y)));
            SDL_RenderLine(renderer, cx - half, cy + static_cast<float>(y), cx + half, cy + static_cast<float>(y));
        }
    }

    void drawHollowCircle(SDL_Renderer* renderer, float centerX, float centerY, float radius, SDL_Color color)
    {
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
        constexpr int segments = 32;
        float lastX = centerX + radius;
        float lastY = centerY;
        for (int i = 1; i <= segments; ++i)
        {
            const float angle = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * std::numbers::pi_v<float>;
            const float x = centerX + std::cos(angle) * radius;
            const float y = centerY + std::sin(angle) * radius;
            SDL_RenderLine(renderer, lastX, lastY, x, y);
            lastX = x;
            lastY = y;
        }
    }

    void renderOceanColumn(SDL_Renderer* renderer, const AquaticWorld& world, const SimulationClock& clock,
        const Camera& camera, int width, int height)
    {
        SDL_SetRenderDrawColor(renderer, 5, 16, 27, 255);
        SDL_RenderClear(renderer);

        const float solar = surfaceSolarEnergy(clock);
        const float daylight = std::clamp(solar / 1000.0f, 0.0f, 1.0f);
        const float viewW = cameraViewWidth(camera);
        const float viewH = cameraViewHeight(camera);
        const float left = camera.centerX - viewW * 0.5f;
        const float top = camera.centerY - viewH * 0.5f;
        constexpr int bandHeight = 4;

        for (int sy = 58; sy < height; sy += bandHeight)
        {
            const float v = static_cast<float>(sy - 58) / static_cast<float>(std::max(height - 58, 1));
            const float wy = top + v * viewH;

            if (wy < OceanSurface)
            {
                const float skyDepth = std::clamp((wy - SkyTop) / (OceanSurface - SkyTop), 0.0f, 1.0f);
                const Uint8 r = static_cast<Uint8>(6 + daylight * (42.0f + skyDepth * 24.0f));
                const Uint8 g = static_cast<Uint8>(14 + daylight * (78.0f + skyDepth * 30.0f));
                const Uint8 b = static_cast<Uint8>(27 + daylight * (126.0f + skyDepth * 38.0f));
                SDL_SetRenderDrawColor(renderer, r, g, b, 255);
            }
            else
            {
                const float depth = std::clamp(wy, 0.0f, 1.0f);
                const EnvironmentSample env = world.environmentAt(0.5f, depth, solar);
                const float light = std::clamp(env.sunlight / 1000.0f, 0.0f, 1.0f);
                const int sampleY = static_cast<int>(depth * static_cast<float>(world.definition().logicalHeight));
                const int sampleX = static_cast<int>((left + 0.5f * viewW) * static_cast<float>(world.definition().logicalWidth));
                const float variation = world.waterVariationAt(sampleX, sampleY) - 0.5f;

                const float deep = std::pow(depth, 0.72f);
                const Uint8 r = static_cast<Uint8>(std::clamp(7.0f + light * 14.0f + variation * 4.0f - deep * 4.0f, 2.0f, 255.0f));
                const Uint8 g = static_cast<Uint8>(std::clamp(37.0f + light * 42.0f + variation * 8.0f - deep * 25.0f, 8.0f, 255.0f));
                const Uint8 b = static_cast<Uint8>(std::clamp(56.0f + light * 58.0f + variation * 10.0f - deep * 30.0f, 15.0f, 255.0f));
                SDL_SetRenderDrawColor(renderer, r, g, b, 255);
            }

            SDL_FRect band{0.0f, static_cast<float>(sy), static_cast<float>(width), static_cast<float>(bandHeight + 1)};
            SDL_RenderFillRect(renderer, &band);
        }

        const SDL_FPoint surfaceLeft = worldToScreen(0.0f, OceanSurface, camera, width, height);
        const SDL_FPoint surfaceRight = worldToScreen(1.0f, OceanSurface, camera, width, height);
        if (surfaceLeft.y >= 58.0f && surfaceLeft.y <= static_cast<float>(height))
        {
            SDL_SetRenderDrawColor(renderer, 111, 178, 191, static_cast<Uint8>(120 + daylight * 90.0f));
            SDL_RenderLine(renderer, surfaceLeft.x, surfaceLeft.y, surfaceRight.x, surfaceRight.y);

            if (solar > 0.0f)
            {
                SDL_SetRenderDrawColor(renderer, 226, 232, 169, 22);
                const SDL_FPoint sunWorld = celestialWorldPosition(clock, true);
                const SDL_FPoint sunScreen = worldToScreen(sunWorld.x, sunWorld.y, camera, width, height);
                SDL_FRect beam{sunScreen.x - 55.0f, surfaceLeft.y, 110.0f, std::min(220.0f, static_cast<float>(height) - surfaceLeft.y)};
                SDL_RenderFillRect(renderer, &beam);
            }
        }

        const bool sun = sunVisibleByTime(clock);
        const SDL_FPoint bodyWorld = celestialWorldPosition(clock, sun);
        const SDL_FPoint bodyScreen = worldToScreen(bodyWorld.x, bodyWorld.y, camera, width, height);
        if (bodyScreen.y >= 58.0f && bodyScreen.y <= static_cast<float>(height))
        {
            if (sun)
            {
                drawFilledCircle(renderer, bodyScreen.x, bodyScreen.y, 18.0f, SDL_Color{245, 222, 126, 255});
            }
            else
            {
                drawFilledCircle(renderer, bodyScreen.x, bodyScreen.y, 15.0f, SDL_Color{199, 211, 218, 255});
                drawFilledCircle(renderer, bodyScreen.x + 6.0f, bodyScreen.y - 3.0f, 13.0f, SDL_Color{8, 18, 30, 255});
            }
        }
    }

    void drawCell(SDL_Renderer* renderer, const Cell& cell, const Camera& camera, int width, int height, bool selected)
    {
        const SDL_FPoint position = worldToScreen(cell.x, cell.y, camera, width, height);
        const float radius = std::max(6.0f, cell.radius * static_cast<float>(width) * camera.zoom);
        if (position.x < -radius || position.x > static_cast<float>(width) + radius ||
            position.y < 58.0f - radius || position.y > static_cast<float>(height) + radius) return;

        drawHollowCircle(renderer, position.x, position.y, radius,
            selected ? SDL_Color{231, 244, 202, 255} : SDL_Color{139, 212, 198, 255});
        if (selected) drawHollowCircle(renderer, position.x, position.y, radius + 3.0f, SDL_Color{74, 138, 143, 220});
    }

    std::string clockText(const SimulationClock& clock)
    {
        std::ostringstream stream;
        stream << "DAY " << clock.day() << "  "
               << std::setfill('0') << std::setw(2) << clock.hour() << ':'
               << std::setfill('0') << std::setw(2) << clock.minute();
        return stream.str();
    }

    std::string numberText(float value, int precision)
    {
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(precision) << value;
        return stream.str();
    }

    void renderMainMenu(SDL_Renderer* renderer, int width, int height, float mouseX, float mouseY,
        Button& playButton, Button& editorButton, Button& libraryButton)
    {
        SDL_SetRenderDrawColor(renderer, 8, 25, 35, 255);
        SDL_RenderClear(renderer);
        for (int y = 0; y < height; y += 8)
        {
            const float t = static_cast<float>(y) / static_cast<float>(std::max(height, 1));
            SDL_SetRenderDrawColor(renderer, 8, static_cast<Uint8>(30 + t * 18.0f), static_cast<Uint8>(42 + t * 24.0f), 255);
            SDL_FRect band{0.0f, static_cast<float>(y), static_cast<float>(width), 9.0f};
            SDL_RenderFillRect(renderer, &band);
        }

        const float titleSize = width < 900 ? 60.0f : 78.0f;
        drawText(renderer, "CLADIA",
            (static_cast<float>(width) - textWidth("CLADIA", titleSize)) * 0.5f,
            static_cast<float>(height) * 0.18f,
            titleSize, SDL_Color{221, 241, 237, 255});

        constexpr float subtitleSize = 18.0f;
        const std::string_view subtitle = "CELLULAR EVOLUTION SIMULATION";
        drawText(renderer, subtitle,
            (static_cast<float>(width) - textWidth(subtitle, subtitleSize)) * 0.5f,
            static_cast<float>(height) * 0.32f,
            subtitleSize, SDL_Color{126, 171, 176, 255});

        const float x = (static_cast<float>(width) - 260.0f) * 0.5f;
        playButton = Button{SDL_FRect{x, static_cast<float>(height) * 0.48f, 260.0f, 54.0f}, "PLAY"};
        editorButton = Button{SDL_FRect{x, playButton.rect.y + 66.0f, 260.0f, 48.0f}, "CELL EDITOR"};
        libraryButton = Button{SDL_FRect{x, editorButton.rect.y + 60.0f, 260.0f, 48.0f}, "SPECIES LIBRARY"};
        drawButton(renderer, playButton, mouseX, mouseY);
        drawButton(renderer, editorButton, mouseX, mouseY);
        drawButton(renderer, libraryButton, mouseX, mouseY);
    }

    void renderCellEditor(SDL_Renderer* renderer, int width, int height, float mouseX, float mouseY, Button& backButton)
    {
        SDL_SetRenderDrawColor(renderer, 8, 23, 32, 255);
        SDL_RenderClear(renderer);
        backButton = Button{SDL_FRect{18.0f, 14.0f, 86.0f, 36.0f}, "BACK"};
        drawButton(renderer, backButton, mouseX, mouseY);
        drawText(renderer, "CELL EDITOR", 124.0f, 18.0f, 28.0f, SDL_Color{224, 239, 237, 255});
        SDL_FRect canvas{32.0f, 78.0f, static_cast<float>(width) * 0.66f, static_cast<float>(height) - 118.0f};
        SDL_SetRenderDrawColor(renderer, 13, 38, 49, 255);
        SDL_RenderFillRect(renderer, &canvas);
        SDL_SetRenderDrawColor(renderer, 43, 88, 99, 255);
        SDL_RenderRect(renderer, &canvas);
        drawText(renderer, "EMPTY CELL", canvas.x + 20.0f, canvas.y + 18.0f, 18.0f, SDL_Color{151, 190, 190, 255});
        drawText(renderer, "COMPONENTS", canvas.x + canvas.w + 34.0f, canvas.y, 18.0f, SDL_Color{195, 218, 214, 255});
        drawText(renderer, "NONE YET", canvas.x + canvas.w + 34.0f, canvas.y + 34.0f, 16.0f, SDL_Color{111, 151, 153, 255});
    }

    void renderSpeciesLibrary(SDL_Renderer* renderer, const SpeciesRegistry& speciesRegistry,
        int width, int height, float mouseX, float mouseY, Button& backButton)
    {
        SDL_SetRenderDrawColor(renderer, 8, 23, 32, 255);
        SDL_RenderClear(renderer);
        backButton = Button{SDL_FRect{18.0f, 14.0f, 86.0f, 36.0f}, "BACK"};
        drawButton(renderer, backButton, mouseX, mouseY);
        drawText(renderer, "SPECIES LIBRARY", 124.0f, 20.0f, 30.0f, SDL_Color{224, 239, 237, 255});
        drawText(renderer, "SPECIES GENERATED THROUGH SPECIATION WILL APPEAR HERE", 30.0f, 78.0f, 16.0f, SDL_Color{121, 161, 164, 255});
        const auto generated = speciesRegistry.generated();
        if (generated.empty())
        {
            SDL_FRect emptyPanel{30.0f, 126.0f, std::min(620.0f, static_cast<float>(width) - 60.0f), 100.0f};
            SDL_SetRenderDrawColor(renderer, 12, 37, 47, 255);
            SDL_RenderFillRect(renderer, &emptyPanel);
            SDL_SetRenderDrawColor(renderer, 42, 84, 94, 255);
            SDL_RenderRect(renderer, &emptyPanel);
            drawText(renderer, "NO GENERATED SPECIES YET", emptyPanel.x + 22.0f, emptyPanel.y + 24.0f, 21.0f, SDL_Color{184, 211, 207, 255});
        }
        else
        {
            float y = 126.0f;
            for (const SpeciesDefinition* species : generated)
            {
                drawText(renderer, species->scientificName, 32.0f, y, 20.0f, SDL_Color{203, 226, 220, 255});
                y += 32.0f;
            }
        }
    }

    void renderCellInfoPanel(SDL_Renderer* renderer, const Cell& cell, const SpeciesRegistry& speciesRegistry,
        const AquaticWorld& world, const SimulationClock& clock, int width, int height)
    {
        const SpeciesDefinition* species = speciesRegistry.find(cell.speciesId);
        const float panelWidth = std::min(440.0f, static_cast<float>(width) * 0.42f);
        const float panelHeight = std::min(590.0f, static_cast<float>(height) - 82.0f);
        SDL_FRect panel{static_cast<float>(width) - panelWidth - 12.0f, 70.0f, panelWidth, panelHeight};
        SDL_SetRenderDrawColor(renderer, 8, 25, 34, 247);
        SDL_RenderFillRect(renderer, &panel);
        SDL_SetRenderDrawColor(renderer, 61, 113, 121, 255);
        SDL_RenderRect(renderer, &panel);

        float y = panel.y + 16.0f;
        drawText(renderer, "DNA", panel.x + 18.0f, y, 24.0f, SDL_Color{228, 241, 237, 255});
        y += 36.0f;
        drawText(renderer, std::to_string(cell.genome.bases.size()) + " BP   1 GAME-SCALE CHROMOSOME",
            panel.x + 18.0f, y, 13.0f, SDL_Color{128, 170, 168, 255});
        y += 26.0f;

        constexpr std::size_t BasesPerLine = 32;
        for (std::size_t offset = 0; offset < cell.genome.bases.size(); offset += BasesPerLine)
        {
            drawText(renderer, cell.genome.bases.substr(offset, BasesPerLine), panel.x + 18.0f, y, 14.0f,
                SDL_Color{184, 214, 207, 255});
            y += 21.0f;
        }

        y += 10.0f;
        SDL_SetRenderDrawColor(renderer, 40, 78, 86, 255);
        SDL_RenderLine(renderer, panel.x + 18.0f, y, panel.x + panel.w - 18.0f, y);
        y += 15.0f;
        drawText(renderer, "PHYLOGENY", panel.x + 18.0f, y, 21.0f, SDL_Color{228, 241, 237, 255});
        y += 33.0f;
        drawText(renderer, species ? species->scientificName : "UNKNOWN SPECIES", panel.x + 18.0f, y, 15.0f,
            SDL_Color{163, 201, 194, 255});
        y += 25.0f;
        drawText(renderer, cell.parentCellId == 0 ? "ORIGIN  FOUNDER" : "ORIGIN  INHERITED",
            panel.x + 18.0f, y, 14.0f, SDL_Color{133, 174, 171, 255});
        y += 23.0f;
        drawText(renderer, "GENERATION  " + std::to_string(cell.generation), panel.x + 18.0f, y, 14.0f,
            SDL_Color{133, 174, 171, 255});

        y += 34.0f;
        SDL_SetRenderDrawColor(renderer, 40, 78, 86, 255);
        SDL_RenderLine(renderer, panel.x + 18.0f, y, panel.x + panel.w - 18.0f, y);
        y += 15.0f;
        drawText(renderer, "ENVIRONMENT", panel.x + 18.0f, y, 21.0f, SDL_Color{228, 241, 237, 255});
        y += 33.0f;

        const EnvironmentSample env = world.environmentAt(cell.x, cell.y, surfaceSolarEnergy(clock));
        drawText(renderer, "DEPTH  " + numberText(env.depthMeters, 0) + " m", panel.x + 18.0f, y, 14.0f, SDL_Color{151, 190, 187, 255});
        y += 23.0f;
        drawText(renderer, "TEMPERATURE  " + numberText(env.temperatureC, 1) + " C", panel.x + 18.0f, y, 14.0f, SDL_Color{151, 190, 187, 255});
        y += 23.0f;
        drawText(renderer, "PRESSURE  " + numberText(env.pressureAtm, 1) + " atm", panel.x + 18.0f, y, 14.0f, SDL_Color{151, 190, 187, 255});
        y += 23.0f;
        drawText(renderer, "SALINITY  " + numberText(env.salinityPpt, 1) + " ppt", panel.x + 18.0f, y, 14.0f, SDL_Color{151, 190, 187, 255});
        y += 23.0f;
        drawText(renderer, "NUTRIENTS  " + numberText(env.nutrientLevel * 100.0f, 0) + "%", panel.x + 18.0f, y, 14.0f, SDL_Color{151, 190, 187, 255});
        y += 23.0f;
        drawText(renderer, "SOLAR ENERGY  " + numberText(env.solarEnergy, 1), panel.x + 18.0f, y, 14.0f,
            env.solarEnergy > 0.0f ? SDL_Color{218, 217, 154, 255} : SDL_Color{110, 143, 151, 255});
    }

    void renderSpawnPanel(SDL_Renderer* renderer, float mouseX, float mouseY, bool dropdownOpen,
        Button& dropdownButton, Button& cladiaOptionButton)
    {
        SDL_FRect panel{104.0f, 66.0f, 250.0f, dropdownOpen ? 132.0f : 86.0f};
        SDL_SetRenderDrawColor(renderer, 9, 28, 37, 246);
        SDL_RenderFillRect(renderer, &panel);
        SDL_SetRenderDrawColor(renderer, 58, 108, 119, 255);
        SDL_RenderRect(renderer, &panel);
        drawText(renderer, "SPAWN SPECIES", panel.x + 14.0f, panel.y + 10.0f, 16.0f, SDL_Color{203, 226, 221, 255});
        dropdownButton = Button{SDL_FRect{panel.x + 12.0f, panel.y + 38.0f, panel.w - 24.0f, 34.0f}, "SELECT SPECIES"};
        drawButton(renderer, dropdownButton, mouseX, mouseY, dropdownOpen);
        if (dropdownOpen)
        {
            cladiaOptionButton = Button{SDL_FRect{panel.x + 12.0f, panel.y + 78.0f, panel.w - 24.0f, 34.0f}, "CLADIA"};
            drawButton(renderer, cladiaOptionButton, mouseX, mouseY);
        }
    }
}

int main()
{
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    if (!TTF_Init()) { SDL_Quit(); return 1; }

    SDL_Window* window = SDL_CreateWindow("Cladia", 1280, 720, SDL_WINDOW_RESIZABLE);
    if (window == nullptr) { TTF_Quit(); SDL_Quit(); return 1; }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (renderer == nullptr)
    {
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    SDL_SetRenderVSync(renderer, 1);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    uiFont = TTF_OpenFont("Arimo-Regular.ttf", 24.0f);
    if (uiFont == nullptr)
    {
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    bool running = true;
    Screen screen = Screen::MainMenu;
    AquaticWorld world = AquaticWorld::createDefault();
    SpeciesRegistry speciesRegistry;
    CellSystem cells;
    SimulationClock clock;
    Camera camera;

    std::uint32_t selectedCellId = 0;
    bool spawnPanelOpen = false;
    bool speciesDropdownOpen = false;
    bool placementMode = false;
    SpeciesId placementSpecies = 0;

    Button playButton, editorButton, libraryButton;
    Button gameBackButton, editorBackButton, libraryBackButton;
    Button spawnButton, dropdownButton, cladiaOptionButton;
    Button pauseButton, speed1Button, speed2Button, speed4Button, speed8Button;

    auto previousFrame = std::chrono::steady_clock::now();

    while (running)
    {
        const auto now = std::chrono::steady_clock::now();
        const float dt = std::clamp(std::chrono::duration<float>(now - previousFrame).count(), 0.0f, 0.05f);
        previousFrame = now;

        int width = 0;
        int height = 0;
        SDL_GetWindowSizeInPixels(window, &width, &height);
        float mouseX = 0.0f;
        float mouseY = 0.0f;
        SDL_GetMouseState(&mouseX, &mouseY);

        if (screen == Screen::Game)
        {
            updateCamera(camera, dt, width, height, mouseX, mouseY);
            clock.update(dt);
            cells.updateAutonomous(clock.paused() ? 0.0f : dt * clock.speed());
        }

        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT)
            {
                running = false;
                continue;
            }

            if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)
            {
                if (screen == Screen::MainMenu) running = false;
                else screen = Screen::MainMenu;
                selectedCellId = 0;
                placementMode = false;
                placementSpecies = 0;
                spawnPanelOpen = false;
                speciesDropdownOpen = false;
                continue;
            }

            if (screen == Screen::Game && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_RIGHT)
            {
                if (placementMode)
                {
                    placementMode = false;
                    placementSpecies = 0;
                    spawnPanelOpen = false;
                    speciesDropdownOpen = false;
                }
                continue;
            }

            if (screen == Screen::Game && event.type == SDL_EVENT_MOUSE_WHEEL)
            {
                const float factor = event.wheel.y > 0.0f ? 1.2f : event.wheel.y < 0.0f ? (1.0f / 1.2f) : 1.0f;
                changeZoom(camera, factor, mouseX, mouseY, width, height);
                continue;
            }

            if (screen == Screen::Game && event.type == SDL_EVENT_KEY_DOWN)
            {
                if (event.key.key == SDLK_PLUS || event.key.key == SDLK_EQUALS || event.key.key == SDLK_KP_PLUS)
                    changeZoom(camera, 1.2f, static_cast<float>(width) * 0.5f, static_cast<float>(height) * 0.5f, width, height);
                else if (event.key.key == SDLK_MINUS || event.key.key == SDLK_KP_MINUS)
                    changeZoom(camera, 1.0f / 1.2f, static_cast<float>(width) * 0.5f, static_cast<float>(height) * 0.5f, width, height);
                else if (event.key.key == SDLK_SPACE)
                    clock.togglePaused();
            }

            if (event.type != SDL_EVENT_MOUSE_BUTTON_DOWN || event.button.button != SDL_BUTTON_LEFT) continue;

            if (screen == Screen::MainMenu)
            {
                if (pointInside(playButton.rect, event.button.x, event.button.y))
                {
                    cells.clear();
                    clock = SimulationClock{};
                    camera = Camera{};
                    selectedCellId = 0;
                    spawnPanelOpen = false;
                    speciesDropdownOpen = false;
                    placementMode = false;
                    placementSpecies = 0;
                    screen = Screen::Game;
                }
                else if (pointInside(editorButton.rect, event.button.x, event.button.y)) screen = Screen::CellEditor;
                else if (pointInside(libraryButton.rect, event.button.x, event.button.y)) screen = Screen::SpeciesLibrary;
                continue;
            }

            if (screen == Screen::CellEditor)
            {
                if (pointInside(editorBackButton.rect, event.button.x, event.button.y)) screen = Screen::MainMenu;
                continue;
            }

            if (screen == Screen::SpeciesLibrary)
            {
                if (pointInside(libraryBackButton.rect, event.button.x, event.button.y)) screen = Screen::MainMenu;
                continue;
            }

            if (screen != Screen::Game) continue;

            if (pointInside(gameBackButton.rect, event.button.x, event.button.y))
            {
                screen = Screen::MainMenu;
                selectedCellId = 0;
                placementMode = false;
                placementSpecies = 0;
                spawnPanelOpen = false;
                speciesDropdownOpen = false;
                continue;
            }

            if (pointInside(spawnButton.rect, event.button.x, event.button.y))
            {
                spawnPanelOpen = !spawnPanelOpen;
                speciesDropdownOpen = false;
                continue;
            }

            if (spawnPanelOpen && pointInside(dropdownButton.rect, event.button.x, event.button.y))
            {
                speciesDropdownOpen = !speciesDropdownOpen;
                continue;
            }

            if (spawnPanelOpen && speciesDropdownOpen && pointInside(cladiaOptionButton.rect, event.button.x, event.button.y))
            {
                placementSpecies = SpeciesRegistry::CladiaId;
                placementMode = true;
                spawnPanelOpen = false;
                speciesDropdownOpen = false;
                selectedCellId = 0;
                continue;
            }

            if (pointInside(pauseButton.rect, event.button.x, event.button.y)) { clock.togglePaused(); continue; }
            if (pointInside(speed1Button.rect, event.button.x, event.button.y)) { clock.setSpeed(1.0f); clock.setPaused(false); continue; }
            if (pointInside(speed2Button.rect, event.button.x, event.button.y)) { clock.setSpeed(2.0f); clock.setPaused(false); continue; }
            if (pointInside(speed4Button.rect, event.button.x, event.button.y)) { clock.setSpeed(4.0f); clock.setPaused(false); continue; }
            if (pointInside(speed8Button.rect, event.button.x, event.button.y)) { clock.setSpeed(8.0f); clock.setPaused(false); continue; }

            if (event.button.y < 58.0f) continue;

            if (placementMode && placementSpecies != 0)
            {
                const SDL_FPoint worldPoint = screenToWorld(event.button.x, event.button.y, camera, width, height);
                if (worldPoint.y >= OceanSurface && worldPoint.y <= OceanBottom)
                    cells.spawnCell(placementSpecies, worldPoint.x, worldPoint.y);
                continue;
            }

            std::uint32_t closestId = 0;
            float closestDistance = 1.0e9f;
            for (const Cell& cell : cells.cells())
            {
                const SDL_FPoint position = worldToScreen(cell.x, cell.y, camera, width, height);
                const float dx = position.x - event.button.x;
                const float dy = position.y - event.button.y;
                const float distance = std::sqrt(dx * dx + dy * dy);
                const float hitRadius = std::max(10.0f, cell.radius * static_cast<float>(width) * camera.zoom * 1.4f);
                if (distance <= hitRadius && distance < closestDistance)
                {
                    closestDistance = distance;
                    closestId = cell.id;
                }
            }
            selectedCellId = closestId;
        }

        if (screen == Screen::MainMenu)
        {
            renderMainMenu(renderer, width, height, mouseX, mouseY, playButton, editorButton, libraryButton);
        }
        else if (screen == Screen::CellEditor)
        {
            renderCellEditor(renderer, width, height, mouseX, mouseY, editorBackButton);
        }
        else if (screen == Screen::SpeciesLibrary)
        {
            renderSpeciesLibrary(renderer, speciesRegistry, width, height, mouseX, mouseY, libraryBackButton);
        }
        else
        {
            renderOceanColumn(renderer, world, clock, camera, width, height);
            for (const Cell& cell : cells.cells()) drawCell(renderer, cell, camera, width, height, cell.id == selectedCellId);

            if (placementMode && placementSpecies != 0 && mouseY >= 58.0f)
            {
                const SDL_FPoint worldPoint = screenToWorld(mouseX, mouseY, camera, width, height);
                const bool valid = worldPoint.y >= OceanSurface && worldPoint.y <= OceanBottom;
                drawHollowCircle(renderer, mouseX, mouseY, 13.0f,
                    valid ? SDL_Color{213, 235, 207, 185} : SDL_Color{225, 119, 119, 190});
                drawText(renderer, valid ? "LEFT CLICK TO PLACE   RIGHT CLICK TO CANCEL" : "CELLS CAN ONLY SPAWN IN WATER",
                    104.0f, 68.0f, 14.0f, SDL_Color{201, 224, 218, 255});
            }

            SDL_FRect topBar{0.0f, 0.0f, static_cast<float>(width), 58.0f};
            SDL_SetRenderDrawColor(renderer, 7, 23, 32, 245);
            SDL_RenderFillRect(renderer, &topBar);
            SDL_SetRenderDrawColor(renderer, 34, 82, 94, 255);
            SDL_FRect divider{0.0f, 57.0f, static_cast<float>(width), 1.0f};
            SDL_RenderFillRect(renderer, &divider);

            gameBackButton = Button{SDL_FRect{10.0f, 10.0f, 80.0f, 38.0f}, "BACK"};
            spawnButton = Button{SDL_FRect{100.0f, 10.0f, 130.0f, 38.0f}, "SPAWN CELL"};
            pauseButton = Button{SDL_FRect{static_cast<float>(width) - 330.0f, 10.0f, 52.0f, 38.0f}, clock.paused() ? "PLAY" : "PAUSE"};
            speed1Button = Button{SDL_FRect{static_cast<float>(width) - 270.0f, 10.0f, 54.0f, 38.0f}, "1X"};
            speed2Button = Button{SDL_FRect{static_cast<float>(width) - 210.0f, 10.0f, 54.0f, 38.0f}, "2X"};
            speed4Button = Button{SDL_FRect{static_cast<float>(width) - 150.0f, 10.0f, 54.0f, 38.0f}, "4X"};
            speed8Button = Button{SDL_FRect{static_cast<float>(width) - 90.0f, 10.0f, 54.0f, 38.0f}, "8X"};

            drawButton(renderer, gameBackButton, mouseX, mouseY);
            drawButton(renderer, spawnButton, mouseX, mouseY, spawnPanelOpen || placementMode);
            drawButton(renderer, pauseButton, mouseX, mouseY, clock.paused());
            drawButton(renderer, speed1Button, mouseX, mouseY, !clock.paused() && std::abs(clock.speed() - 1.0f) < 0.01f);
            drawButton(renderer, speed2Button, mouseX, mouseY, !clock.paused() && std::abs(clock.speed() - 2.0f) < 0.01f);
            drawButton(renderer, speed4Button, mouseX, mouseY, !clock.paused() && std::abs(clock.speed() - 4.0f) < 0.01f);
            drawButton(renderer, speed8Button, mouseX, mouseY, !clock.paused() && std::abs(clock.speed() - 8.0f) < 0.01f);

            drawText(renderer, clockText(clock), 244.0f, 16.0f, 18.0f, SDL_Color{187, 215, 213, 255});
            drawText(renderer, "CELLS " + std::to_string(cells.cells().size()), 382.0f, 17.0f, 15.0f, SDL_Color{103, 156, 158, 255});

            if (spawnPanelOpen) renderSpawnPanel(renderer, mouseX, mouseY, speciesDropdownOpen, dropdownButton, cladiaOptionButton);
            if (const Cell* selected = cells.findById(selectedCellId))
                renderCellInfoPanel(renderer, *selected, speciesRegistry, world, clock, width, height);
        }

        SDL_RenderPresent(renderer);
    }

    TTF_CloseFont(uiFont);
    uiFont = nullptr;
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
    return 0;
}
