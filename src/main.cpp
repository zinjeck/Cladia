#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "CellSystem.h"
#include "WorldGenerator.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace
{
    enum class Screen
    {
        MainMenu,
        WorldSetup,
        World,
        CellEditor
    };

    enum class SliderId
    {
        None,
        Elevation,
        Temperature,
        Water,
        Moisture,
        ContinentScale
    };

    struct Button
    {
        SDL_FRect rect{};
        bool hovered = false;
    };

    struct Slider
    {
        SDL_FRect rect{};
        float* value = nullptr;
    };

    struct Camera
    {
        float centerX = 0.5f;
        float centerY = 0.5f;
        float zoom = 1.0f;
    };

    constexpr float cellVisibilityZoom = 28.0f;
    constexpr float maxCameraZoom = 900.0f;
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
        return TTF_GetStringSize(uiFont, text.data(), text.size(), &width, &height) ? static_cast<float>(width) : 0.0f;
    }

    bool pointInside(const SDL_FRect& rect, float x, float y)
    {
        return x >= rect.x && x <= rect.x + rect.w && y >= rect.y && y <= rect.y + rect.h;
    }

    std::uint32_t makeSeed()
    {
        const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        return static_cast<std::uint32_t>(now ^ (now >> 32));
    }

    void drawButton(SDL_Renderer* renderer, Button& button, std::string_view label, float x, float y, float w, float h)
    {
        button.rect = SDL_FRect{x, y, w, h};
        SDL_SetRenderDrawColor(renderer, button.hovered ? 115 : 79, button.hovered ? 158 : 119, button.hovered ? 104 : 76, 255);
        SDL_RenderFillRect(renderer, &button.rect);
        const float size = 24.0f;
        drawText(renderer, label, x + (w - textWidth(label, size)) * 0.5f, y + 12.0f, size, SDL_Color{244, 248, 238, 255});
    }

    void setSliderFromMouse(Slider& slider, float mouseX)
    {
        if (slider.value == nullptr || slider.rect.w <= 0.0f) return;
        *slider.value = std::clamp((mouseX - slider.rect.x) / slider.rect.w, 0.0f, 1.0f);
    }

    void drawSlider(SDL_Renderer* renderer, Slider& slider, float x, float y, float width, float& value)
    {
        slider.rect = SDL_FRect{x, y, width, 22.0f};
        slider.value = &value;
        SDL_SetRenderDrawColor(renderer, 42, 57, 61, 255);
        SDL_FRect track{x, y + 8.0f, width, 6.0f};
        SDL_RenderFillRect(renderer, &track);
        SDL_SetRenderDrawColor(renderer, 103, 151, 105, 255);
        SDL_FRect filled{x, y + 8.0f, width * value, 6.0f};
        SDL_RenderFillRect(renderer, &filled);
        SDL_SetRenderDrawColor(renderer, 221, 234, 216, 255);
        SDL_FRect knob{x + width * value - 6.0f, y + 3.0f, 12.0f, 16.0f};
        SDL_RenderFillRect(renderer, &knob);
    }

    void renderMainMenu(SDL_Renderer* renderer, int width, int height, Button& playButton, Button& editorButton)
    {
        SDL_SetRenderDrawColor(renderer, 12, 20, 25, 255);
        SDL_RenderClear(renderer);
        SDL_SetRenderDrawColor(renderer, 20, 34, 39, 255);
        SDL_FRect lower{0.0f, static_cast<float>(height) * 0.62f, static_cast<float>(width), static_cast<float>(height) * 0.38f};
        SDL_RenderFillRect(renderer, &lower);

        const float titleSize = width < 1000 ? 58.0f : 76.0f;
        drawText(renderer, "CLADIA", (static_cast<float>(width) - textWidth("CLADIA", titleSize)) * 0.5f, static_cast<float>(height) * 0.20f, titleSize, SDL_Color{226, 237, 219, 255});
        const float subtitleSize = 20.0f;
        drawText(renderer, "EVOLUTION SIMULATION", (static_cast<float>(width) - textWidth("EVOLUTION SIMULATION", subtitleSize)) * 0.5f, static_cast<float>(height) * 0.35f, subtitleSize, SDL_Color{153, 174, 160, 255});

        const float x = (static_cast<float>(width) - 270.0f) * 0.5f;
        drawButton(renderer, playButton, "PLAY", x, static_cast<float>(height) * 0.51f, 270.0f, 60.0f);
        drawButton(renderer, editorButton, "CELL EDITOR", x, static_cast<float>(height) * 0.62f, 270.0f, 60.0f);
    }

    void renderCellEditor(SDL_Renderer* renderer, int width, int height)
    {
        SDL_SetRenderDrawColor(renderer, 10, 18, 23, 255);
        SDL_RenderClear(renderer);
        drawText(renderer, "CELL EDITOR", 34.0f, 26.0f, 36.0f, SDL_Color{229, 238, 224, 255});
        drawText(renderer, "EDITOR SHELL ONLY - BIOLOGY WILL BE ADDED LATER", 36.0f, 76.0f, 17.0f, SDL_Color{153, 174, 160, 255});

        SDL_FRect parts{32.0f, 122.0f, 265.0f, static_cast<float>(height) - 154.0f};
        SDL_SetRenderDrawColor(renderer, 22, 32, 37, 255);
        SDL_RenderFillRect(renderer, &parts);
        SDL_SetRenderDrawColor(renderer, 67, 87, 88, 255);
        SDL_RenderRect(renderer, &parts);
        drawText(renderer, "COMPONENTS", 52.0f, 146.0f, 22.0f, SDL_Color{211, 222, 207, 255});
        drawText(renderer, "NONE YET", 52.0f, 190.0f, 18.0f, SDL_Color{136, 156, 145, 255});

        SDL_FRect canvas{329.0f, 122.0f, static_cast<float>(width) - 361.0f, static_cast<float>(height) - 154.0f};
        SDL_SetRenderDrawColor(renderer, 15, 26, 31, 255);
        SDL_RenderFillRect(renderer, &canvas);
        SDL_SetRenderDrawColor(renderer, 67, 87, 88, 255);
        SDL_RenderRect(renderer, &canvas);
        const float messageSize = 24.0f;
        const std::string_view message = "EMPTY CELL CANVAS";
        drawText(renderer, message, canvas.x + (canvas.w - textWidth(message, messageSize)) * 0.5f, canvas.y + canvas.h * 0.45f, messageSize, SDL_Color{125, 146, 136, 255});
        drawText(renderer, "ESC TO RETURN", 36.0f, static_cast<float>(height) - 28.0f, 15.0f, SDL_Color{136, 156, 145, 255});
    }

    void renderSetup(SDL_Renderer* renderer, int width, int height, const GeneratedWorld& preview, WorldSettings& settings, Button& playButton, Button& rerollButton, Slider& elevationSlider, Slider& temperatureSlider, Slider& waterSlider, Slider& moistureSlider, Slider& continentSlider)
    {
        SDL_SetRenderDrawColor(renderer, 12, 20, 25, 255);
        SDL_RenderClear(renderer);
        const float titleSize = width < 1000 ? 48.0f : 62.0f;
        drawText(renderer, "CLADIA", (static_cast<float>(width) - textWidth("CLADIA", titleSize)) * 0.5f, 28.0f, titleSize, SDL_Color{226, 237, 219, 255});

        const float panelMargin = 46.0f;
        const float panelTop = 120.0f;
        const float panelHeight = std::max(480.0f, static_cast<float>(height) - panelTop - 38.0f);
        SDL_FRect panel{panelMargin, panelTop, static_cast<float>(width) - panelMargin * 2.0f, panelHeight};
        SDL_SetRenderDrawColor(renderer, 22, 32, 37, 255);
        SDL_RenderFillRect(renderer, &panel);
        SDL_SetRenderDrawColor(renderer, 67, 87, 88, 255);
        SDL_RenderRect(renderer, &panel);

        const float gap = 34.0f;
        const float leftWidth = panel.w * 0.54f;
        SDL_FRect previewFrame{panel.x + 26.0f, panel.y + 56.0f, leftWidth - 38.0f, panel.h - 112.0f};
        drawText(renderer, "WORLD PREVIEW", previewFrame.x, panel.y + 20.0f, 22.0f, SDL_Color{190, 208, 186, 255});
        SDL_SetRenderDrawColor(renderer, 6, 12, 16, 255);
        SDL_RenderFillRect(renderer, &previewFrame);
        if (preview.texture != nullptr)
        {
            const float aspect = static_cast<float>(preview.width) / static_cast<float>(preview.height);
            SDL_FRect destination = previewFrame;
            const float frameAspect = previewFrame.w / previewFrame.h;
            if (frameAspect > aspect)
            {
                destination.w = previewFrame.h * aspect;
                destination.x = previewFrame.x + (previewFrame.w - destination.w) * 0.5f;
            }
            else
            {
                destination.h = previewFrame.w / aspect;
                destination.y = previewFrame.y + (previewFrame.h - destination.h) * 0.5f;
            }
            SDL_RenderTexture(renderer, preview.texture, nullptr, &destination);
        }
        SDL_SetRenderDrawColor(renderer, 90, 111, 108, 255);
        SDL_RenderRect(renderer, &previewFrame);

        const float rightX = panel.x + leftWidth + gap;
        const float rightWidth = panel.x + panel.w - rightX - 28.0f;
        const float sliderWidth = std::max(180.0f, rightWidth);
        float y = panel.y + 76.0f;
        drawText(renderer, "WORLD SETTINGS", rightX, panel.y + 20.0f, 22.0f, SDL_Color{190, 208, 186, 255});

        const std::array<std::pair<std::string_view, float*>, 5> items = {{
            {"AVERAGE ELEVATION", &settings.averageElevation},
            {"TEMPERATURE", &settings.temperature},
            {"WATER LEVEL", &settings.waterLevel},
            {"MOISTURE", &settings.moisture},
            {"CONTINENT SCALE", &settings.continentScale}}};
        std::array<Slider*, 5> sliders = {&elevationSlider, &temperatureSlider, &waterSlider, &moistureSlider, &continentSlider};
        for (std::size_t i = 0; i < items.size(); ++i)
        {
            drawText(renderer, items[i].first, rightX, y, 17.0f, SDL_Color{218, 226, 211, 255});
            y += 28.0f;
            drawSlider(renderer, *sliders[i], rightX, y, sliderWidth, *items[i].second);
            y += 58.0f;
        }

        rerollButton.rect = SDL_FRect{rightX, panel.y + panel.h - 96.0f, std::min(170.0f, rightWidth * 0.43f), 52.0f};
        playButton.rect = SDL_FRect{rerollButton.rect.x + rerollButton.rect.w + 18.0f, rerollButton.rect.y, rightWidth - rerollButton.rect.w - 18.0f, 52.0f};
        SDL_SetRenderDrawColor(renderer, rerollButton.hovered ? 72 : 49, rerollButton.hovered ? 98 : 72, rerollButton.hovered ? 92 : 77, 255);
        SDL_RenderFillRect(renderer, &rerollButton.rect);
        drawText(renderer, "NEW WORLD", rerollButton.rect.x + 14.0f, rerollButton.rect.y + 14.0f, 18.0f, SDL_Color{231, 238, 226, 255});
        SDL_SetRenderDrawColor(renderer, playButton.hovered ? 115 : 79, playButton.hovered ? 158 : 119, playButton.hovered ? 104 : 76, 255);
        SDL_RenderFillRect(renderer, &playButton.rect);
        drawText(renderer, "PLAY", playButton.rect.x + (playButton.rect.w - textWidth("PLAY", 24.0f)) * 0.5f, playButton.rect.y + 11.0f, 24.0f, SDL_Color{244, 248, 238, 255});
    }

    void clampCamera(Camera& camera)
    {
        camera.zoom = std::clamp(camera.zoom, 1.0f, maxCameraZoom);
        const float half = 0.5f / camera.zoom;
        camera.centerX = std::clamp(camera.centerX, half, 1.0f - half);
        camera.centerY = std::clamp(camera.centerY, half, 1.0f - half);
    }

    void changeZoom(Camera& camera, float factor, float mouseX, float mouseY, int width, int height)
    {
        if (width <= 0 || height <= 0) return;
        const float oldView = 1.0f / camera.zoom;
        const float mouseU = std::clamp(mouseX / static_cast<float>(width), 0.0f, 1.0f);
        const float mouseV = std::clamp(mouseY / static_cast<float>(height), 0.0f, 1.0f);
        const float worldX = camera.centerX + (mouseU - 0.5f) * oldView;
        const float worldY = camera.centerY + (mouseV - 0.5f) * oldView;
        camera.zoom = std::clamp(camera.zoom * factor, 1.0f, maxCameraZoom);
        const float newView = 1.0f / camera.zoom;
        camera.centerX = worldX - (mouseU - 0.5f) * newView;
        camera.centerY = worldY - (mouseV - 0.5f) * newView;
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
        constexpr float edge = 18.0f;
        if (mouseX <= edge) dx -= 1.0f;
        if (mouseX >= static_cast<float>(width) - edge) dx += 1.0f;
        if (mouseY <= edge) dy -= 1.0f;
        if (mouseY >= static_cast<float>(height) - edge) dy += 1.0f;
        if (dx != 0.0f || dy != 0.0f)
        {
            const float length = std::sqrt(dx * dx + dy * dy);
            dx /= length;
            dy /= length;
            const float speed = 0.65f / std::sqrt(camera.zoom);
            camera.centerX += dx * speed * dt;
            camera.centerY += dy * speed * dt;
            clampCamera(camera);
        }
    }

    bool cellToScreen(const Cell& cell, const Camera& camera, int width, int height, float& sx, float& sy, float& radius)
    {
        const float view = 1.0f / camera.zoom;
        const float left = camera.centerX - view * 0.5f;
        const float top = camera.centerY - view * 0.5f;
        const float u = (cell.x - left) / view;
        const float v = (cell.y - top) / view;
        if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) return false;
        sx = u * static_cast<float>(width);
        sy = v * static_cast<float>(height);
        radius = std::clamp(cell.radius / view * static_cast<float>(width), 3.5f, 36.0f);
        return true;
    }

    void drawFilledCircle(SDL_Renderer* renderer, float cx, float cy, float radius)
    {
        const int r = static_cast<int>(std::ceil(radius));
        for (int y = -r; y <= r; ++y)
        {
            const float half = std::sqrt(std::max(0.0f, radius * radius - static_cast<float>(y * y)));
            SDL_RenderLine(renderer, cx - half, cy + static_cast<float>(y), cx + half, cy + static_cast<float>(y));
        }
    }

    std::uint32_t pickCell(const CellSystem& cells, const Camera& camera, int width, int height, float mouseX, float mouseY)
    {
        if (camera.zoom < cellVisibilityZoom) return 0;
        float best = std::numeric_limits<float>::max();
        std::uint32_t bestId = 0;
        for (const Cell& cell : cells.cells())
        {
            float sx = 0.0f, sy = 0.0f, radius = 0.0f;
            if (!cellToScreen(cell, camera, width, height, sx, sy, radius)) continue;
            const float dx = mouseX - sx;
            const float dy = mouseY - sy;
            const float distance = std::sqrt(dx * dx + dy * dy);
            if (distance <= std::max(10.0f, radius + 4.0f) && distance < best)
            {
                best = distance;
                bestId = cell.id;
            }
        }
        return bestId;
    }

    void renderWorld(SDL_Renderer* renderer, const GeneratedWorld& world, const Camera& camera, const CellSystem& cells, std::uint32_t selectedCellId, int width, int height)
    {
        SDL_SetRenderDrawColor(renderer, 7, 12, 15, 255);
        SDL_RenderClear(renderer);
        if (world.texture != nullptr)
        {
            const float sourceW = static_cast<float>(world.width) / camera.zoom;
            const float sourceH = static_cast<float>(world.height) / camera.zoom;
            SDL_FRect source{camera.centerX * world.width - sourceW * 0.5f, camera.centerY * world.height - sourceH * 0.5f, sourceW, sourceH};
            SDL_FRect destination{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)};
            SDL_RenderTexture(renderer, world.texture, &source, &destination);
        }

        if (camera.zoom >= cellVisibilityZoom)
        {
            for (const Cell& cell : cells.cells())
            {
                float sx = 0.0f, sy = 0.0f, radius = 0.0f;
                if (!cellToScreen(cell, camera, width, height, sx, sy, radius)) continue;
                const bool selected = cell.id == selectedCellId;
                SDL_SetRenderDrawColor(renderer, selected ? 241 : 204, selected ? 238 : 224, selected ? 150 : 192, 235);
                drawFilledCircle(renderer, sx, sy, radius);
                if (selected)
                {
                    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
                    SDL_FRect outline{sx - radius - 4.0f, sy - radius - 4.0f, radius * 2.0f + 8.0f, radius * 2.0f + 8.0f};
                    SDL_RenderRect(renderer, &outline);
                }
            }
        }

        SDL_SetRenderDrawColor(renderer, 7, 11, 14, 205);
        SDL_FRect header{0.0f, 0.0f, static_cast<float>(width), 56.0f};
        SDL_RenderFillRect(renderer, &header);
        drawText(renderer, camera.zoom >= cellVisibilityZoom ? "CELL SCALE" : "WORLD", 20.0f, 11.0f, 26.0f, SDL_Color{228, 236, 223, 255});
        drawText(renderer, "WASD / EDGE SCROLL   WHEEL OR +/- TO ZOOM", 170.0f, 17.0f, 15.0f, SDL_Color{162, 179, 166, 255});

        if (camera.zoom < cellVisibilityZoom)
        {
            drawText(renderer, "ZOOM FURTHER INTO OCEAN WATER TO REACH CELL SCALE", 20.0f, 72.0f, 16.0f, SDL_Color{228, 236, 223, 220});
        }

        if (selectedCellId != 0)
        {
            const Cell* cell = cells.findById(selectedCellId);
            if (cell != nullptr)
            {
                constexpr float panelWidth = 300.0f;
                SDL_FRect panel{static_cast<float>(width) - panelWidth - 18.0f, 74.0f, panelWidth, 220.0f};
                SDL_SetRenderDrawColor(renderer, 16, 27, 32, 240);
                SDL_RenderFillRect(renderer, &panel);
                SDL_SetRenderDrawColor(renderer, 85, 104, 101, 255);
                SDL_RenderRect(renderer, &panel);
                drawText(renderer, "CELL", panel.x + 18.0f, panel.y + 16.0f, 25.0f, SDL_Color{231, 239, 226, 255});
                drawText(renderer, "ID: " + std::to_string(cell->id), panel.x + 18.0f, panel.y + 56.0f, 17.0f, SDL_Color{184, 201, 187, 255});
                drawText(renderer, "COMPONENTS", panel.x + 18.0f, panel.y + 100.0f, 19.0f, SDL_Color{211, 222, 207, 255});
                drawText(renderer, "NONE YET", panel.x + 18.0f, panel.y + 136.0f, 17.0f, SDL_Color{140, 160, 149, 255});
                drawText(renderer, "BIOLOGY NOT IMPLEMENTED", panel.x + 18.0f, panel.y + 176.0f, 14.0f, SDL_Color{122, 143, 133, 255});
            }
        }
    }
}

int main()
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
        return 1;
    }
    if (!TTF_Init())
    {
        std::cerr << "TTF_Init failed: " << SDL_GetError() << '\n';
        SDL_Quit();
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("Cladia", 1280, 720, SDL_WINDOW_RESIZABLE);
    if (window == nullptr)
    {
        TTF_Quit(); SDL_Quit(); return 1;
    }
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (renderer == nullptr)
    {
        SDL_DestroyWindow(window); TTF_Quit(); SDL_Quit(); return 1;
    }
    uiFont = TTF_OpenFont("Arimo-Regular.ttf", 24.0f);
    if (uiFont == nullptr)
    {
        SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); TTF_Quit(); SDL_Quit(); return 1;
    }
    SDL_SetRenderVSync(renderer, 1);

    bool running = true;
    Screen screen = Screen::MainMenu;
    SliderId activeSlider = SliderId::None;
    WorldSettings settings;
    std::uint32_t seed = makeSeed();
    Camera camera;
    CellSystem cellSystem;
    std::uint32_t selectedCellId = 0;

    Button mainPlayButton, editorButton, worldPlayButton, rerollButton;
    Slider elevationSlider, temperatureSlider, waterSlider, moistureSlider, continentSlider;
    constexpr int previewWidth = 192;
    constexpr int previewHeight = 108;
    GeneratedWorld preview = WorldGenerator::generate(renderer, previewWidth, previewHeight, seed, settings);
    GeneratedWorld world;

    auto refreshPreview = [&]()
    {
        WorldGenerator::destroy(preview);
        preview = WorldGenerator::generate(renderer, previewWidth, previewHeight, seed, settings);
    };

    auto sliderFor = [&](SliderId id) -> Slider*
    {
        switch (id)
        {
        case SliderId::Elevation: return &elevationSlider;
        case SliderId::Temperature: return &temperatureSlider;
        case SliderId::Water: return &waterSlider;
        case SliderId::Moisture: return &moistureSlider;
        case SliderId::ContinentScale: return &continentSlider;
        default: return nullptr;
        }
    };

    auto previousFrame = std::chrono::steady_clock::now();
    while (running)
    {
        const auto now = std::chrono::steady_clock::now();
        const float dt = std::clamp(std::chrono::duration<float>(now - previousFrame).count(), 0.0f, 0.05f);
        previousFrame = now;

        int width = 0, height = 0;
        SDL_GetWindowSizeInPixels(window, &width, &height);
        float mouseX = 0.0f, mouseY = 0.0f;
        SDL_GetMouseState(&mouseX, &mouseY);

        mainPlayButton.hovered = screen == Screen::MainMenu && pointInside(mainPlayButton.rect, mouseX, mouseY);
        editorButton.hovered = screen == Screen::MainMenu && pointInside(editorButton.rect, mouseX, mouseY);
        worldPlayButton.hovered = screen == Screen::WorldSetup && pointInside(worldPlayButton.rect, mouseX, mouseY);
        rerollButton.hovered = screen == Screen::WorldSetup && pointInside(rerollButton.rect, mouseX, mouseY);

        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT)
            {
                running = false;
            }
            else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)
            {
                if (screen == Screen::World) screen = Screen::WorldSetup;
                else if (screen == Screen::WorldSetup || screen == Screen::CellEditor) screen = Screen::MainMenu;
                else running = false;
            }
            else if (screen == Screen::World && event.type == SDL_EVENT_MOUSE_WHEEL)
            {
                const float factor = event.wheel.y > 0.0f ? 1.28f : (event.wheel.y < 0.0f ? 1.0f / 1.28f : 1.0f);
                changeZoom(camera, factor, mouseX, mouseY, width, height);
            }
            else if (screen == Screen::World && event.type == SDL_EVENT_KEY_DOWN)
            {
                if (event.key.key == SDLK_PLUS || event.key.key == SDLK_EQUALS || event.key.key == SDLK_KP_PLUS)
                    changeZoom(camera, 1.28f, width * 0.5f, height * 0.5f, width, height);
                else if (event.key.key == SDLK_MINUS || event.key.key == SDLK_KP_MINUS)
                    changeZoom(camera, 1.0f / 1.28f, width * 0.5f, height * 0.5f, width, height);
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT)
            {
                if (screen == Screen::MainMenu)
                {
                    if (pointInside(mainPlayButton.rect, event.button.x, event.button.y)) screen = Screen::WorldSetup;
                    else if (pointInside(editorButton.rect, event.button.x, event.button.y)) screen = Screen::CellEditor;
                }
                else if (screen == Screen::WorldSetup)
                {
                    if (pointInside(worldPlayButton.rect, event.button.x, event.button.y))
                    {
                        WorldGenerator::destroy(world);
                        world = WorldGenerator::generate(renderer, 2048, 1152, seed, settings);
                        cellSystem.seedPlaceholderOceanCells(seed);
                        selectedCellId = 0;
                        camera = Camera{};
                        screen = Screen::World;
                    }
                    else if (pointInside(rerollButton.rect, event.button.x, event.button.y))
                    {
                        seed = makeSeed();
                        refreshPreview();
                    }
                    else
                    {
                        const std::array<std::pair<SliderId, Slider*>, 5> sliders = {{
                            {SliderId::Elevation, &elevationSlider}, {SliderId::Temperature, &temperatureSlider},
                            {SliderId::Water, &waterSlider}, {SliderId::Moisture, &moistureSlider},
                            {SliderId::ContinentScale, &continentSlider}}};
                        for (const auto& [id, slider] : sliders)
                        {
                            if (pointInside(slider->rect, event.button.x, event.button.y))
                            {
                                activeSlider = id;
                                setSliderFromMouse(*slider, event.button.x);
                                refreshPreview();
                                break;
                            }
                        }
                    }
                }
                else if (screen == Screen::World && event.button.y > 56.0f)
                {
                    selectedCellId = pickCell(cellSystem, camera, width, height, event.button.x, event.button.y);
                }
            }
            else if (event.type == SDL_EVENT_MOUSE_MOTION && screen == Screen::WorldSetup && activeSlider != SliderId::None)
            {
                Slider* slider = sliderFor(activeSlider);
                if (slider != nullptr)
                {
                    setSliderFromMouse(*slider, event.motion.x);
                    refreshPreview();
                }
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT)
            {
                activeSlider = SliderId::None;
            }
        }

        if (screen == Screen::World) updateCamera(camera, dt, width, height, mouseX, mouseY);

        if (screen == Screen::MainMenu) renderMainMenu(renderer, width, height, mainPlayButton, editorButton);
        else if (screen == Screen::WorldSetup) renderSetup(renderer, width, height, preview, settings, worldPlayButton, rerollButton, elevationSlider, temperatureSlider, waterSlider, moistureSlider, continentSlider);
        else if (screen == Screen::CellEditor) renderCellEditor(renderer, width, height);
        else renderWorld(renderer, world, camera, cellSystem, selectedCellId, width, height);

        SDL_RenderPresent(renderer);
    }

    WorldGenerator::destroy(preview);
    WorldGenerator::destroy(world);
    TTF_CloseFont(uiFont);
    uiFont = nullptr;
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
    return 0;
}
