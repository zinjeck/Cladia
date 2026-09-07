#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "CellSystem.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string_view>

namespace
{
    enum class Screen
    {
        MainMenu,
        CellWorld,
        CellEditor
    };

    struct Button
    {
        SDL_FRect rect{};
        bool hovered = false;
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

    void drawFilledCircle(SDL_Renderer* renderer, float cx, float cy, float radius, SDL_Color color)
    {
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
        const int r = std::max(1, static_cast<int>(std::ceil(radius)));

        for (int y = -r; y <= r; ++y)
        {
            const float fy = static_cast<float>(y);
            const float inside = radius * radius - fy * fy;
            if (inside < 0.0f)
            {
                continue;
            }

            const float halfWidth = std::sqrt(inside);
            SDL_RenderLine(renderer, cx - halfWidth, cy + fy, cx + halfWidth, cy + fy);
        }
    }

    void renderButton(SDL_Renderer* renderer, Button& button, std::string_view label, float x, float y, float width)
    {
        button.rect = SDL_FRect{x, y, width, 58.0f};
        SDL_SetRenderDrawColor(
            renderer,
            button.hovered ? 92 : 61,
            button.hovered ? 143 : 104,
            button.hovered ? 153 : 119,
            255);
        SDL_RenderFillRect(renderer, &button.rect);

        const float size = 24.0f;
        drawText(
            renderer,
            label,
            button.rect.x + (button.rect.w - textWidth(label, size)) * 0.5f,
            button.rect.y + 12.0f,
            size,
            SDL_Color{244, 249, 249, 255});
    }

    void renderMainMenu(SDL_Renderer* renderer, int width, int height, Button& playButton, Button& editorButton)
    {
        SDL_SetRenderDrawColor(renderer, 7, 28, 39, 255);
        SDL_RenderClear(renderer);

        SDL_SetRenderDrawColor(renderer, 9, 42, 57, 255);
        SDL_FRect lower{0.0f, static_cast<float>(height) * 0.58f, static_cast<float>(width), static_cast<float>(height) * 0.42f};
        SDL_RenderFillRect(renderer, &lower);

        const float titleSize = width < 1000 ? 58.0f : 76.0f;
        const float titleX = (static_cast<float>(width) - textWidth("CLADIA", titleSize)) * 0.5f;
        drawText(renderer, "CLADIA", titleX, static_cast<float>(height) * 0.18f, titleSize, SDL_Color{225, 242, 240, 255});

        const float subtitleSize = 21.0f;
        const float subtitleX = (static_cast<float>(width) - textWidth("CELL EVOLUTION SIMULATION", subtitleSize)) * 0.5f;
        drawText(renderer, "CELL EVOLUTION SIMULATION", subtitleX, static_cast<float>(height) * 0.34f, subtitleSize, SDL_Color{154, 198, 202, 255});

        const float buttonWidth = 270.0f;
        const float x = (static_cast<float>(width) - buttonWidth) * 0.5f;
        renderButton(renderer, playButton, "PLAY", x, static_cast<float>(height) * 0.52f, buttonWidth);
        renderButton(renderer, editorButton, "CELL EDITOR", x, static_cast<float>(height) * 0.52f + 76.0f, buttonWidth);
    }

    void clampCamera(Camera& camera)
    {
        camera.zoom = std::clamp(camera.zoom, 1.0f, 4.0f);
        const float half = 0.5f / camera.zoom;
        camera.centerX = std::clamp(camera.centerX, half, 1.0f - half);
        camera.centerY = std::clamp(camera.centerY, half, 1.0f - half);
    }

    void changeZoom(Camera& camera, float factor)
    {
        camera.zoom *= factor;
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
            const float speed = 0.55f / camera.zoom;
            camera.centerX += dx * speed * dt;
            camera.centerY += dy * speed * dt;
            clampCamera(camera);
        }
    }

    void worldToScreen(const Camera& camera, float worldX, float worldY, int width, int height, float& screenX, float& screenY)
    {
        const float viewSize = 1.0f / camera.zoom;
        const float left = camera.centerX - viewSize * 0.5f;
        const float top = camera.centerY - viewSize * 0.5f;
        screenX = ((worldX - left) / viewSize) * static_cast<float>(width);
        screenY = ((worldY - top) / viewSize) * static_cast<float>(height);
    }

    bool screenToWorld(const Camera& camera, float screenX, float screenY, int width, int height, float& worldX, float& worldY)
    {
        if (width <= 0 || height <= 0)
        {
            return false;
        }

        const float viewSize = 1.0f / camera.zoom;
        const float left = camera.centerX - viewSize * 0.5f;
        const float top = camera.centerY - viewSize * 0.5f;
        worldX = left + (screenX / static_cast<float>(width)) * viewSize;
        worldY = top + (screenY / static_cast<float>(height)) * viewSize;
        return true;
    }

    void renderWater(SDL_Renderer* renderer, int width, int height)
    {
        SDL_SetRenderDrawColor(renderer, 6, 34, 49, 255);
        SDL_RenderClear(renderer);

        for (int y = 0; y < height; y += 36)
        {
            const Uint8 blue = static_cast<Uint8>(72 + (y / 36) % 3 * 6);
            SDL_SetRenderDrawColor(renderer, 13, 61, blue, 255);
            SDL_FRect band{0.0f, static_cast<float>(y), static_cast<float>(width), 18.0f};
            SDL_RenderFillRect(renderer, &band);
        }
    }

    void renderCellInspector(SDL_Renderer* renderer, const Cell* selected, int width, int height)
    {
        if (selected == nullptr)
        {
            return;
        }

        const float panelWidth = std::min(300.0f, static_cast<float>(width) * 0.30f);
        SDL_FRect panel{static_cast<float>(width) - panelWidth - 16.0f, 72.0f, panelWidth, std::min(260.0f, static_cast<float>(height) - 88.0f)};
        SDL_SetRenderDrawColor(renderer, 10, 30, 39, 235);
        SDL_RenderFillRect(renderer, &panel);
        SDL_SetRenderDrawColor(renderer, 95, 150, 155, 255);
        SDL_RenderRect(renderer, &panel);

        drawText(renderer, "CELL INSPECTOR", panel.x + 18.0f, panel.y + 16.0f, 22.0f, SDL_Color{227, 242, 240, 255});
        drawText(renderer, "CELL SELECTED", panel.x + 18.0f, panel.y + 58.0f, 17.0f, SDL_Color{172, 205, 204, 255});
        drawText(renderer, "COMPONENTS", panel.x + 18.0f, panel.y + 108.0f, 18.0f, SDL_Color{227, 242, 240, 255});
        drawText(renderer, "NONE YET", panel.x + 18.0f, panel.y + 140.0f, 17.0f, SDL_Color{155, 185, 186, 255});
        drawText(renderer, "BIOLOGY NOT IMPLEMENTED", panel.x + 18.0f, panel.y + 188.0f, 14.0f, SDL_Color{132, 164, 167, 255});
    }

    void renderCellWorld(SDL_Renderer* renderer, const CellSystem& cells, const Camera& camera, const Cell* selected, int width, int height)
    {
        renderWater(renderer, width, height);

        for (const Cell& cell : cells.cells())
        {
            float x = 0.0f;
            float y = 0.0f;
            worldToScreen(camera, cell.x, cell.y, width, height, x, y);

            if (x < -30.0f || y < -30.0f || x > static_cast<float>(width) + 30.0f || y > static_cast<float>(height) + 30.0f)
            {
                continue;
            }

            const float radius = std::max(4.0f, cell.radius * static_cast<float>(std::min(width, height)) * camera.zoom);
            const bool isSelected = selected != nullptr && selected->id == cell.id;

            drawFilledCircle(renderer, x, y, radius + (isSelected ? 3.0f : 0.0f), isSelected ? SDL_Color{230, 240, 177, 255} : SDL_Color{125, 201, 177, 255});
            drawFilledCircle(renderer, x, y, radius * 0.65f, SDL_Color{52, 124, 123, 255});
        }

        SDL_SetRenderDrawColor(renderer, 5, 22, 30, 220);
        SDL_FRect header{0.0f, 0.0f, static_cast<float>(width), 56.0f};
        SDL_RenderFillRect(renderer, &header);
        drawText(renderer, "AQUATIC CELL WORLD", 18.0f, 12.0f, 25.0f, SDL_Color{227, 242, 240, 255});
        drawText(renderer, "WASD / EDGE SCROLL   +/- OR WHEEL TO ADJUST VIEW", 270.0f, 17.0f, 15.0f, SDL_Color{151, 185, 187, 255});

        renderCellInspector(renderer, selected, width, height);
    }

    void renderCellEditor(SDL_Renderer* renderer, int width, int height)
    {
        renderWater(renderer, width, height);

        SDL_SetRenderDrawColor(renderer, 8, 25, 33, 230);
        SDL_FRect left{28.0f, 78.0f, 250.0f, static_cast<float>(height) - 106.0f};
        SDL_FRect canvas{302.0f, 78.0f, static_cast<float>(width) - 330.0f, static_cast<float>(height) - 106.0f};
        SDL_RenderFillRect(renderer, &left);
        SDL_RenderFillRect(renderer, &canvas);
        SDL_SetRenderDrawColor(renderer, 82, 130, 136, 255);
        SDL_RenderRect(renderer, &left);
        SDL_RenderRect(renderer, &canvas);

        drawText(renderer, "CELL EDITOR", 28.0f, 20.0f, 32.0f, SDL_Color{228, 243, 241, 255});
        drawText(renderer, "COMPONENTS", 48.0f, 100.0f, 20.0f, SDL_Color{223, 239, 237, 255});
        drawText(renderer, "NONE YET", 48.0f, 138.0f, 17.0f, SDL_Color{151, 184, 185, 255});
        drawText(renderer, "CELL CANVAS", canvas.x + 22.0f, canvas.y + 20.0f, 20.0f, SDL_Color{223, 239, 237, 255});
        drawText(renderer, "BIOLOGY WILL BE ADDED LATER", canvas.x + 22.0f, canvas.y + 58.0f, 16.0f, SDL_Color{145, 179, 181, 255});
    }

    const Cell* pickCell(const CellSystem& cells, const Camera& camera, float mouseX, float mouseY, int width, int height)
    {
        float wx = 0.0f;
        float wy = 0.0f;
        if (!screenToWorld(camera, mouseX, mouseY, width, height, wx, wy))
        {
            return nullptr;
        }

        const Cell* best = nullptr;
        float bestDistance = 1.0f;

        for (const Cell& cell : cells.cells())
        {
            const float dx = wx - cell.x;
            const float dy = wy - cell.y;
            const float distance = std::sqrt(dx * dx + dy * dy);
            const float hitRadius = std::max(cell.radius, 0.012f / camera.zoom);

            if (distance <= hitRadius && distance < bestDistance)
            {
                best = &cell;
                bestDistance = distance;
            }
        }

        return best;
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
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (renderer == nullptr)
    {
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    uiFont = TTF_OpenFont("Arimo-Regular.ttf", 24.0f);
    if (uiFont == nullptr)
    {
        std::cerr << "Failed to load Arimo-Regular.ttf: " << SDL_GetError() << '\n';
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    SDL_SetRenderVSync(renderer, 1);

    bool running = true;
    Screen screen = Screen::MainMenu;
    Button playButton;
    Button editorButton;
    Camera camera;
    CellSystem cells;
    const Cell* selectedCell = nullptr;

    cells.seedPlaceholderOceanCells(makeSeed(), 220);

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

        playButton.hovered = screen == Screen::MainMenu && pointInside(playButton.rect, mouseX, mouseY);
        editorButton.hovered = screen == Screen::MainMenu && pointInside(editorButton.rect, mouseX, mouseY);

        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT)
            {
                running = false;
            }
            else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)
            {
                if (screen == Screen::MainMenu)
                {
                    running = false;
                }
                else
                {
                    screen = Screen::MainMenu;
                    selectedCell = nullptr;
                }
            }
            else if (screen == Screen::CellWorld && event.type == SDL_EVENT_MOUSE_WHEEL)
            {
                if (event.wheel.y > 0.0f) changeZoom(camera, 1.18f);
                if (event.wheel.y < 0.0f) changeZoom(camera, 1.0f / 1.18f);
            }
            else if (screen == Screen::CellWorld && event.type == SDL_EVENT_KEY_DOWN)
            {
                if (event.key.key == SDLK_PLUS || event.key.key == SDLK_EQUALS || event.key.key == SDLK_KP_PLUS)
                {
                    changeZoom(camera, 1.18f);
                }
                else if (event.key.key == SDLK_MINUS || event.key.key == SDLK_KP_MINUS)
                {
                    changeZoom(camera, 1.0f / 1.18f);
                }
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT)
            {
                if (screen == Screen::MainMenu)
                {
                    if (pointInside(playButton.rect, event.button.x, event.button.y))
                    {
                        cells.seedPlaceholderOceanCells(makeSeed(), 220);
                        camera = Camera{};
                        selectedCell = nullptr;
                        screen = Screen::CellWorld;
                    }
                    else if (pointInside(editorButton.rect, event.button.x, event.button.y))
                    {
                        screen = Screen::CellEditor;
                    }
                }
                else if (screen == Screen::CellWorld)
                {
                    selectedCell = pickCell(cells, camera, event.button.x, event.button.y, width, height);
                }
            }
        }

        if (screen == Screen::CellWorld)
        {
            updateCamera(camera, dt, width, height, mouseX, mouseY);
        }

        if (screen == Screen::MainMenu)
        {
            renderMainMenu(renderer, width, height, playButton, editorButton);
        }
        else if (screen == Screen::CellWorld)
        {
            renderCellWorld(renderer, cells, camera, selectedCell, width, height);
        }
        else
        {
            renderCellEditor(renderer, width, height);
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
