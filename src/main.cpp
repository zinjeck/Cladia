#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "WorldGenerator.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <utility>

namespace
{
    enum class Screen
    {
        MainMenu,
        WorldSetup,
        World
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

    TTF_Font* uiFont = nullptr;

    void drawText(SDL_Renderer* renderer, std::string_view text, float x, float y, float pointSize, SDL_Color color)
    {
        if (uiFont == nullptr || text.empty())
        {
            return;
        }

        if (!TTF_SetFontSize(uiFont, pointSize))
        {
            return;
        }

        SDL_Surface* surface = TTF_RenderText_Blended(uiFont, text.data(), text.size(), color);
        if (surface == nullptr)
        {
            return;
        }

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
        if (uiFont == nullptr || text.empty())
        {
            return 0.0f;
        }

        if (!TTF_SetFontSize(uiFont, pointSize))
        {
            return 0.0f;
        }

        int width = 0;
        int height = 0;
        if (!TTF_GetStringSize(uiFont, text.data(), text.size(), &width, &height))
        {
            return 0.0f;
        }

        return static_cast<float>(width);
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

    void setSliderFromMouse(Slider& slider, float mouseX)
    {
        if (slider.value == nullptr || slider.rect.w <= 0.0f)
        {
            return;
        }

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

    void renderMainMenu(SDL_Renderer* renderer, int width, int height, Button& startButton)
    {
        SDL_SetRenderDrawColor(renderer, 12, 20, 25, 255);
        SDL_RenderClear(renderer);

        SDL_SetRenderDrawColor(renderer, 20, 34, 39, 255);
        SDL_FRect lower{0.0f, static_cast<float>(height) * 0.62f, static_cast<float>(width), static_cast<float>(height) * 0.38f};
        SDL_RenderFillRect(renderer, &lower);

        const float titleSize = width < 1000 ? 58.0f : 76.0f;
        const float titleX = (static_cast<float>(width) - textWidth("CLADIA", titleSize)) * 0.5f;
        drawText(renderer, "CLADIA", titleX, static_cast<float>(height) * 0.22f, titleSize, SDL_Color{226, 237, 219, 255});

        const float subtitleSize = 20.0f;
        const float subtitleX = (static_cast<float>(width) - textWidth("EVOLUTION SIMULATION", subtitleSize)) * 0.5f;
        drawText(renderer, "EVOLUTION SIMULATION", subtitleX, static_cast<float>(height) * 0.37f, subtitleSize, SDL_Color{153, 174, 160, 255});

        startButton.rect = SDL_FRect{
            (static_cast<float>(width) - 250.0f) * 0.5f,
            static_cast<float>(height) * 0.56f,
            250.0f,
            62.0f};

        SDL_SetRenderDrawColor(
            renderer,
            startButton.hovered ? 115 : 79,
            startButton.hovered ? 158 : 119,
            startButton.hovered ? 104 : 76,
            255);
        SDL_RenderFillRect(renderer, &startButton.rect);

        const float playSize = 28.0f;
        drawText(
            renderer,
            "PLAY",
            startButton.rect.x + (startButton.rect.w - textWidth("PLAY", playSize)) * 0.5f,
            startButton.rect.y + 13.0f,
            playSize,
            SDL_Color{244, 248, 238, 255});
    }

    void renderSetup(
        SDL_Renderer* renderer,
        int width,
        int height,
        const GeneratedWorld& preview,
        WorldSettings& settings,
        Button& playButton,
        Button& rerollButton,
        Slider& elevationSlider,
        Slider& temperatureSlider,
        Slider& waterSlider,
        Slider& moistureSlider,
        Slider& continentSlider)
    {
        SDL_SetRenderDrawColor(renderer, 12, 20, 25, 255);
        SDL_RenderClear(renderer);

        const float titleSize = width < 1000 ? 48.0f : 62.0f;
        const float titleX = (static_cast<float>(width) - textWidth("CLADIA", titleSize)) * 0.5f;
        drawText(renderer, "CLADIA", titleX, 28.0f, titleSize, SDL_Color{226, 237, 219, 255});

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

        drawText(renderer, "AVERAGE ELEVATION", rightX, y, 17.0f, SDL_Color{218, 226, 211, 255});
        y += 28.0f;
        drawSlider(renderer, elevationSlider, rightX, y, sliderWidth, settings.averageElevation);
        y += 58.0f;

        drawText(renderer, "TEMPERATURE", rightX, y, 17.0f, SDL_Color{218, 226, 211, 255});
        y += 28.0f;
        drawSlider(renderer, temperatureSlider, rightX, y, sliderWidth, settings.temperature);
        y += 58.0f;

        drawText(renderer, "WATER LEVEL", rightX, y, 17.0f, SDL_Color{218, 226, 211, 255});
        y += 28.0f;
        drawSlider(renderer, waterSlider, rightX, y, sliderWidth, settings.waterLevel);
        y += 58.0f;

        drawText(renderer, "MOISTURE", rightX, y, 17.0f, SDL_Color{218, 226, 211, 255});
        y += 28.0f;
        drawSlider(renderer, moistureSlider, rightX, y, sliderWidth, settings.moisture);
        y += 58.0f;

        drawText(renderer, "CONTINENT SCALE", rightX, y, 17.0f, SDL_Color{218, 226, 211, 255});
        y += 28.0f;
        drawSlider(renderer, continentSlider, rightX, y, sliderWidth, settings.continentScale);

        rerollButton.rect = SDL_FRect{rightX, panel.y + panel.h - 96.0f, std::min(170.0f, rightWidth * 0.43f), 52.0f};
        playButton.rect = SDL_FRect{rerollButton.rect.x + rerollButton.rect.w + 18.0f, rerollButton.rect.y, rightWidth - rerollButton.rect.w - 18.0f, 52.0f};

        SDL_SetRenderDrawColor(renderer, rerollButton.hovered ? 72 : 49, rerollButton.hovered ? 98 : 72, rerollButton.hovered ? 92 : 77, 255);
        SDL_RenderFillRect(renderer, &rerollButton.rect);
        drawText(renderer, "NEW WORLD", rerollButton.rect.x + 14.0f, rerollButton.rect.y + 14.0f, 18.0f, SDL_Color{231, 238, 226, 255});

        SDL_SetRenderDrawColor(renderer, playButton.hovered ? 115 : 79, playButton.hovered ? 158 : 119, playButton.hovered ? 104 : 76, 255);
        SDL_RenderFillRect(renderer, &playButton.rect);
        const float playSize = 24.0f;
        drawText(renderer, "PLAY", playButton.rect.x + (playButton.rect.w - textWidth("PLAY", playSize)) * 0.5f, playButton.rect.y + 11.0f, playSize, SDL_Color{244, 248, 238, 255});
    }

    void clampCamera(Camera& camera)
    {
        camera.zoom = std::clamp(camera.zoom, 1.0f, 12.0f);
        const float halfWidth = 0.5f / camera.zoom;
        const float halfHeight = 0.5f / camera.zoom;
        camera.centerX = std::clamp(camera.centerX, halfWidth, 1.0f - halfWidth);
        camera.centerY = std::clamp(camera.centerY, halfHeight, 1.0f - halfHeight);
    }

    void changeZoom(Camera& camera, float factor, float mouseX, float mouseY, int width, int height)
    {
        if (width <= 0 || height <= 0)
        {
            return;
        }

        const float oldZoom = camera.zoom;
        const float oldViewW = 1.0f / oldZoom;
        const float oldViewH = 1.0f / oldZoom;
        const float mouseU = std::clamp(mouseX / static_cast<float>(width), 0.0f, 1.0f);
        const float mouseV = std::clamp(mouseY / static_cast<float>(height), 0.0f, 1.0f);

        const float worldXUnderCursor = camera.centerX + (mouseU - 0.5f) * oldViewW;
        const float worldYUnderCursor = camera.centerY + (mouseV - 0.5f) * oldViewH;

        camera.zoom = std::clamp(camera.zoom * factor, 1.0f, 12.0f);

        const float newViewW = 1.0f / camera.zoom;
        const float newViewH = 1.0f / camera.zoom;
        camera.centerX = worldXUnderCursor - (mouseU - 0.5f) * newViewW;
        camera.centerY = worldYUnderCursor - (mouseV - 0.5f) * newViewH;
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

        constexpr float edgeSize = 18.0f;
        if (mouseX <= edgeSize) dx -= 1.0f;
        if (mouseX >= static_cast<float>(width) - edgeSize) dx += 1.0f;
        if (mouseY <= edgeSize) dy -= 1.0f;
        if (mouseY >= static_cast<float>(height) - edgeSize) dy += 1.0f;

        if (dx != 0.0f || dy != 0.0f)
        {
            const float length = std::sqrt(dx * dx + dy * dy);
            dx /= length;
            dy /= length;

            const float speed = 0.65f / camera.zoom;
            camera.centerX += dx * speed * dt;
            camera.centerY += dy * speed * dt;
            clampCamera(camera);
        }
    }

    void renderWorld(SDL_Renderer* renderer, const GeneratedWorld& world, const Camera& camera, int width, int height)
    {
        SDL_SetRenderDrawColor(renderer, 7, 12, 15, 255);
        SDL_RenderClear(renderer);

        if (world.texture != nullptr)
        {
            const float sourceW = static_cast<float>(world.width) / camera.zoom;
            const float sourceH = static_cast<float>(world.height) / camera.zoom;

            SDL_FRect source{
                camera.centerX * static_cast<float>(world.width) - sourceW * 0.5f,
                camera.centerY * static_cast<float>(world.height) - sourceH * 0.5f,
                sourceW,
                sourceH};

            SDL_FRect destination{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)};
            SDL_RenderTexture(renderer, world.texture, &source, &destination);
        }

        SDL_SetRenderDrawColor(renderer, 7, 11, 14, 190);
        SDL_FRect header{0.0f, 0.0f, static_cast<float>(width), 56.0f};
        SDL_RenderFillRect(renderer, &header);

        drawText(renderer, "WORLD", 20.0f, 11.0f, 26.0f, SDL_Color{228, 236, 223, 255});
        drawText(renderer, "WASD / EDGE SCROLL   MOUSE WHEEL OR +/- TO ZOOM", 115.0f, 16.0f, 16.0f, SDL_Color{162, 179, 166, 255});
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
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << '\n';
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (renderer == nullptr)
    {
        std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << '\n';
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    uiFont = TTF_OpenFont("Arimo-Regular.ttf", 24.0f);
    if (uiFont == nullptr)
    {
        std::cerr << "TTF_OpenFont failed for Arimo-Regular.ttf: " << SDL_GetError() << '\n';
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    SDL_SetRenderVSync(renderer, 1);

    bool running = true;
    Screen screen = Screen::MainMenu;
    SliderId activeSlider = SliderId::None;
    WorldSettings settings;
    std::uint32_t seed = makeSeed();
    Camera camera;

    Button startButton;
    Button playButton;
    Button rerollButton;
    Slider elevationSlider;
    Slider temperatureSlider;
    Slider waterSlider;
    Slider moistureSlider;
    Slider continentSlider;

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

        int width = 0;
        int height = 0;
        SDL_GetWindowSizeInPixels(window, &width, &height);

        float mouseX = 0.0f;
        float mouseY = 0.0f;
        SDL_GetMouseState(&mouseX, &mouseY);

        startButton.hovered = screen == Screen::MainMenu && pointInside(startButton.rect, mouseX, mouseY);
        playButton.hovered = screen == Screen::WorldSetup && pointInside(playButton.rect, mouseX, mouseY);
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
                if (screen == Screen::World)
                {
                    screen = Screen::WorldSetup;
                }
                else if (screen == Screen::WorldSetup)
                {
                    screen = Screen::MainMenu;
                }
                else
                {
                    running = false;
                }
            }
            else if (screen == Screen::World && event.type == SDL_EVENT_MOUSE_WHEEL)
            {
                const float factor = event.wheel.y > 0.0f ? 1.22f : (event.wheel.y < 0.0f ? 1.0f / 1.22f : 1.0f);
                changeZoom(camera, factor, mouseX, mouseY, width, height);
            }
            else if (screen == Screen::World && event.type == SDL_EVENT_KEY_DOWN)
            {
                if (event.key.key == SDLK_PLUS || event.key.key == SDLK_EQUALS || event.key.key == SDLK_KP_PLUS)
                {
                    changeZoom(camera, 1.22f, static_cast<float>(width) * 0.5f, static_cast<float>(height) * 0.5f, width, height);
                }
                else if (event.key.key == SDLK_MINUS || event.key.key == SDLK_KP_MINUS)
                {
                    changeZoom(camera, 1.0f / 1.22f, static_cast<float>(width) * 0.5f, static_cast<float>(height) * 0.5f, width, height);
                }
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT)
            {
                if (screen == Screen::MainMenu && pointInside(startButton.rect, event.button.x, event.button.y))
                {
                    screen = Screen::WorldSetup;
                }
                else if (screen == Screen::WorldSetup)
                {
                    if (pointInside(playButton.rect, event.button.x, event.button.y))
                    {
                        WorldGenerator::destroy(world);
                        world = WorldGenerator::generate(renderer, 2048, 1152, seed, settings);
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
                            {SliderId::Elevation, &elevationSlider},
                            {SliderId::Temperature, &temperatureSlider},
                            {SliderId::Water, &waterSlider},
                            {SliderId::Moisture, &moistureSlider},
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

        if (screen == Screen::World)
        {
            updateCamera(camera, dt, width, height, mouseX, mouseY);
        }

        if (screen == Screen::MainMenu)
        {
            renderMainMenu(renderer, width, height, startButton);
        }
        else if (screen == Screen::WorldSetup)
        {
            renderSetup(
                renderer,
                width,
                height,
                preview,
                settings,
                playButton,
                rerollButton,
                elevationSlider,
                temperatureSlider,
                waterSlider,
                moistureSlider,
                continentSlider);
        }
        else
        {
            renderWorld(renderer, world, camera, width, height);
        }

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
