#include <SDL3/SDL.h>

#include "WorldGenerator.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <unordered_map>

namespace
{
    enum class Screen
    {
        MainMenu,
        World
    };

    struct Button
    {
        SDL_FRect rect{};
        bool hovered = false;
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
        {'N', {0b10001, 0b11001, 0b10101, 0b10011, 0b10001, 0b10001, 0b10001}},
        {'O', {0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110}},
        {'P', {0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000, 0b10000}},
        {'R', {0b11110, 0b10001, 0b10001, 0b11110, 0b10100, 0b10010, 0b10001}},
        {'S', {0b01111, 0b10000, 0b10000, 0b01110, 0b00001, 0b00001, 0b11110}},
        {'T', {0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100}},
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
        if (text.empty())
        {
            return 0.0f;
        }
        return static_cast<float>(text.size() * 6 - 1) * scale;
    }

    bool pointInside(const SDL_FRect& rect, float x, float y)
    {
        return x >= rect.x && x <= rect.x + rect.w && y >= rect.y && y <= rect.y + rect.h;
    }

    void renderMainMenu(SDL_Renderer* renderer, int width, int height, Button& playButton)
    {
        SDL_SetRenderDrawColor(renderer, 13, 21, 27, 255);
        SDL_RenderClear(renderer);

        const float horizon = static_cast<float>(height) * 0.70f;
        SDL_SetRenderDrawColor(renderer, 24, 39, 45, 255);
        SDL_FRect lower{0.0f, horizon, static_cast<float>(width), static_cast<float>(height) - horizon};
        SDL_RenderFillRect(renderer, &lower);

        SDL_SetRenderDrawColor(renderer, 31, 52, 54, 255);
        for (int i = 0; i < 9; ++i)
        {
            const float moundWidth = 210.0f + static_cast<float>((i * 37) % 120);
            const float moundHeight = 55.0f + static_cast<float>((i * 29) % 80);
            const float x = static_cast<float>(i) * (static_cast<float>(width) / 8.0f) - 90.0f;
            SDL_FRect mound{x, horizon - moundHeight, moundWidth, moundHeight + 4.0f};
            SDL_RenderFillRect(renderer, &mound);
        }

        const float titleScale = width < 900 ? 7.0f : 9.0f;
        const float titleX = (static_cast<float>(width) - textWidth("CLADIA", titleScale)) * 0.5f;
        const float titleY = static_cast<float>(height) * 0.20f;
        drawText(renderer, "CLADIA", titleX + 3.0f, titleY + 4.0f, titleScale, SDL_Color{0, 0, 0, 90});
        drawText(renderer, "CLADIA", titleX, titleY, titleScale, SDL_Color{224, 235, 216, 255});

        playButton.rect.w = 240.0f;
        playButton.rect.h = 72.0f;
        playButton.rect.x = (static_cast<float>(width) - playButton.rect.w) * 0.5f;
        playButton.rect.y = static_cast<float>(height) * 0.53f;

        const SDL_Color fill = playButton.hovered ? SDL_Color{116, 158, 105, 255} : SDL_Color{79, 119, 76, 255};
        SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
        SDL_RenderFillRect(renderer, &playButton.rect);

        SDL_SetRenderDrawColor(renderer, 189, 215, 176, 255);
        SDL_RenderRect(renderer, &playButton.rect);

        const float playScale = 4.0f;
        drawText(
            renderer,
            "PLAY",
            playButton.rect.x + (playButton.rect.w - textWidth("PLAY", playScale)) * 0.5f,
            playButton.rect.y + (playButton.rect.h - 7.0f * playScale) * 0.5f,
            playScale,
            SDL_Color{242, 247, 236, 255});
    }

    void renderWorld(SDL_Renderer* renderer, const GeneratedWorld& world, int width, int height)
    {
        SDL_SetRenderDrawColor(renderer, 9, 16, 20, 255);
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

        SDL_SetRenderDrawColor(renderer, 8, 12, 15, 175);
        SDL_FRect header{0.0f, 0.0f, static_cast<float>(width), 52.0f};
        SDL_RenderFillRect(renderer, &header);
        drawText(renderer, "WORLD", 20.0f, 16.0f, 3.0f, SDL_Color{228, 236, 223, 255});
    }

    std::uint32_t makeSeed()
    {
        const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        return static_cast<std::uint32_t>(now ^ (now >> 32));
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
    Screen screen = Screen::MainMenu;
    Button playButton;
    GeneratedWorld world;

    while (running)
    {
        int width = 0;
        int height = 0;
        SDL_GetWindowSizeInPixels(window, &width, &height);

        float mouseX = 0.0f;
        float mouseY = 0.0f;
        SDL_GetMouseState(&mouseX, &mouseY);
        playButton.hovered = screen == Screen::MainMenu && pointInside(playButton.rect, mouseX, mouseY);

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
                    screen = Screen::MainMenu;
                }
                else
                {
                    running = false;
                }
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT)
            {
                if (screen == Screen::MainMenu && pointInside(playButton.rect, event.button.x, event.button.y))
                {
                    WorldGenerator::destroy(world);
                    world = WorldGenerator::generate(renderer, 960, 540, makeSeed());
                    screen = Screen::World;
                }
            }
        }

        if (screen == Screen::MainMenu)
        {
            renderMainMenu(renderer, width, height, playButton);
        }
        else
        {
            renderWorld(renderer, world, width, height);
        }

        SDL_RenderPresent(renderer);
    }

    WorldGenerator::destroy(world);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
