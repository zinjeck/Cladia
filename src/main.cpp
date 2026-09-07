#include <SDL3/SDL.h>

#include "WorldGenerator.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace
{
    enum class Screen
    {
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

    using Glyph = std::array<std::uint8_t, 7>;

    const std::unordered_map<char, Glyph> font = {
        {'A', {0b01110, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001}},
        {'C', {0b01111, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b01111}},
        {'D', {0b11110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b11110}},
        {'E', {0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b11111}},
        {'G', {0b01111, 0b10000, 0b10000, 0b10111, 0b10001, 0b10001, 0b01111}},
        {'I', {0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b11111}},
        {'L', {0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b11111}},
        {'M', {0b10001, 0b11011, 0b10101, 0b10101, 0b10001, 0b10001, 0b10001}},
        {'N', {0b10001, 0b11001, 0b10101, 0b10011, 0b10001, 0b10001, 0b10001}},
        {'O', {0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110}},
        {'P', {0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000, 0b10000}},
        {'R', {0b11110, 0b10001, 0b10001, 0b11110, 0b10100, 0b10010, 0b10001}},
        {'S', {0b01111, 0b10000, 0b10000, 0b01110, 0b00001, 0b00001, 0b11110}},
        {'T', {0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100}},
        {'U', {0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110}},
        {'V', {0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01010, 0b00100}},
        {'W', {0b10001, 0b10001, 0b10001, 0b10101, 0b10101, 0b10101, 0b01010}},
        {'Y', {0b10001, 0b10001, 0b01010, 0b00100, 0b00100, 0b00100, 0b00100}},
        {' ', {0, 0, 0, 0, 0, 0, 0}}
    };

    void drawText(SDL_Renderer* renderer, std::string_view text, float x, float y, float scale, SDL_Color color)
    {
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
        float cursorX = x;

        for (char c : text)
        {
            const auto it = font.find(c);
            if (it == font.end())
            {
                cursorX += 6.0f * scale;
                continue;
            }

            const Glyph& glyph = it->second;
            for (int row = 0; row < 7; ++row)
            {
                for (int column = 0; column < 5; ++column)
                {
                    const std::uint8_t mask = static_cast<std::uint8_t>(1u << (4 - column));
                    if ((glyph[static_cast<std::size_t>(row)] & mask) != 0)
                    {
                        SDL_FRect pixel{
                            cursorX + static_cast<float>(column) * scale,
                            y + static_cast<float>(row) * scale,
                            scale,
                            scale};
                        SDL_RenderFillRect(renderer, &pixel);
                    }
                }
            }
            cursorX += 6.0f * scale;
        }
    }

    float textWidth(std::string_view text, float scale)
    {
        return text.empty() ? 0.0f : static_cast<float>(text.size() * 6 - 1) * scale;
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

        const float titleScale = width < 1000 ? 5.0f : 7.0f;
        const float titleX = (static_cast<float>(width) - textWidth("CLADIA", titleScale)) * 0.5f;
        drawText(renderer, "CLADIA", titleX, 34.0f, titleScale, SDL_Color{226, 237, 219, 255});

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

        drawText(renderer, "WORLD PREVIEW", previewFrame.x, panel.y + 24.0f, 2.4f, SDL_Color{190, 208, 186, 255});
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

        drawText(renderer, "WORLD SETTINGS", rightX, panel.y + 24.0f, 2.4f, SDL_Color{190, 208, 186, 255});

        drawText(renderer, "AVERAGE ELEVATION", rightX, y, 1.7f, SDL_Color{218, 226, 211, 255});
        y += 28.0f;
        drawSlider(renderer, elevationSlider, rightX, y, sliderWidth, settings.averageElevation);
        y += 58.0f;

        drawText(renderer, "TEMPERATURE", rightX, y, 1.7f, SDL_Color{218, 226, 211, 255});
        y += 28.0f;
        drawSlider(renderer, temperatureSlider, rightX, y, sliderWidth, settings.temperature);
        y += 58.0f;

        drawText(renderer, "WATER LEVEL", rightX, y, 1.7f, SDL_Color{218, 226, 211, 255});
        y += 28.0f;
        drawSlider(renderer, waterSlider, rightX, y, sliderWidth, settings.waterLevel);
        y += 58.0f;

        drawText(renderer, "MOISTURE", rightX, y, 1.7f, SDL_Color{218, 226, 211, 255});
        y += 28.0f;
        drawSlider(renderer, moistureSlider, rightX, y, sliderWidth, settings.moisture);
        y += 58.0f;

        drawText(renderer, "CONTINENT SCALE", rightX, y, 1.7f, SDL_Color{218, 226, 211, 255});
        y += 28.0f;
        drawSlider(renderer, continentSlider, rightX, y, sliderWidth, settings.continentScale);

        rerollButton.rect = SDL_FRect{rightX, panel.y + panel.h - 96.0f, std::min(170.0f, rightWidth * 0.43f), 52.0f};
        playButton.rect = SDL_FRect{rerollButton.rect.x + rerollButton.rect.w + 18.0f, rerollButton.rect.y, rightWidth - rerollButton.rect.w - 18.0f, 52.0f};

        SDL_SetRenderDrawColor(renderer, rerollButton.hovered ? 72 : 49, rerollButton.hovered ? 98 : 72, rerollButton.hovered ? 92 : 77, 255);
        SDL_RenderFillRect(renderer, &rerollButton.rect);
        drawText(renderer, "NEW WORLD", rerollButton.rect.x + 14.0f, rerollButton.rect.y + 17.0f, 1.8f, SDL_Color{231, 238, 226, 255});

        SDL_SetRenderDrawColor(renderer, playButton.hovered ? 115 : 79, playButton.hovered ? 158 : 119, playButton.hovered ? 104 : 76, 255);
        SDL_RenderFillRect(renderer, &playButton.rect);
        const float playScale = 2.5f;
        drawText(renderer, "PLAY", playButton.rect.x + (playButton.rect.w - textWidth("PLAY", playScale)) * 0.5f, playButton.rect.y + 17.0f, playScale, SDL_Color{244, 248, 238, 255});
    }

    void renderWorld(SDL_Renderer* renderer, const GeneratedWorld& world, int width, int height)
    {
        SDL_SetRenderDrawColor(renderer, 7, 12, 15, 255);
        SDL_RenderClear(renderer);

        if (world.texture != nullptr)
        {
            const float sourceAspect = static_cast<float>(world.width) / static_cast<float>(world.height);
            const float targetAspect = static_cast<float>(width) / static_cast<float>(height);
            SDL_FRect destination{};

            if (targetAspect > sourceAspect)
            {
                destination.h = static_cast<float>(height);
                destination.w = destination.h * sourceAspect;
            }
            else
            {
                destination.w = static_cast<float>(width);
                destination.h = destination.w / sourceAspect;
            }

            destination.x = (static_cast<float>(width) - destination.w) * 0.5f;
            destination.y = (static_cast<float>(height) - destination.h) * 0.5f;
            SDL_RenderTexture(renderer, world.texture, nullptr, &destination);
        }

        SDL_SetRenderDrawColor(renderer, 7, 11, 14, 180);
        SDL_FRect header{0.0f, 0.0f, static_cast<float>(width), 52.0f};
        SDL_RenderFillRect(renderer, &header);
        drawText(renderer, "WORLD", 20.0f, 16.0f, 3.0f, SDL_Color{228, 236, 223, 255});
    }
}

int main()
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("Cladia", 1280, 720, SDL_WINDOW_RESIZABLE);
    if (window == nullptr)
    {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << '\n';
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (renderer == nullptr)
    {
        std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << '\n';
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_SetRenderVSync(renderer, 1);

    bool running = true;
    Screen screen = Screen::WorldSetup;
    SliderId activeSlider = SliderId::None;
    WorldSettings settings;
    std::uint32_t seed = makeSeed();

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

    while (running)
    {
        int width = 0;
        int height = 0;
        SDL_GetWindowSizeInPixels(window, &width, &height);

        float mouseX = 0.0f;
        float mouseY = 0.0f;
        SDL_GetMouseState(&mouseX, &mouseY);

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
                else
                {
                    running = false;
                }
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT && screen == Screen::WorldSetup)
            {
                if (pointInside(playButton.rect, event.button.x, event.button.y))
                {
                    WorldGenerator::destroy(world);
                    world = WorldGenerator::generate(renderer, 2048, 1152, seed, settings);
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

        if (screen == Screen::WorldSetup)
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
            renderWorld(renderer, world, width, height);
        }

        SDL_RenderPresent(renderer);
    }

    WorldGenerator::destroy(preview);
    WorldGenerator::destroy(world);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
