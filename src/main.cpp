#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "AquaticWorld.h"
#include "CellSystem.h"
#include "SimulationClock.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace
{
    enum class Screen
    {
        MainMenu,
        Game,
        CellEditor
    };

    struct Button
    {
        SDL_FRect rect{};
        std::string_view label{};
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
            SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
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
        drawText(
            renderer,
            button.label,
            button.rect.x + (button.rect.w - textWidth(button.label, textSize)) * 0.5f,
            button.rect.y + 10.0f,
            textSize,
            SDL_Color{226, 239, 241, 255});
    }

    void clampCamera(Camera& camera)
    {
        camera.zoom = std::clamp(camera.zoom, 1.0f, 4.0f);
        const float half = 0.5f / camera.zoom;
        camera.centerX = std::clamp(camera.centerX, half, 1.0f - half);
        camera.centerY = std::clamp(camera.centerY, half, 1.0f - half);
    }

    void changeZoom(Camera& camera, float factor, float mouseX, float mouseY, int width, int height)
    {
        if (width <= 0 || height <= 0)
        {
            return;
        }

        const float oldZoom = camera.zoom;
        const float oldView = 1.0f / oldZoom;
        const float u = std::clamp(mouseX / static_cast<float>(width), 0.0f, 1.0f);
        const float v = std::clamp(mouseY / static_cast<float>(height), 0.0f, 1.0f);
        const float worldX = camera.centerX + (u - 0.5f) * oldView;
        const float worldY = camera.centerY + (v - 0.5f) * oldView;

        camera.zoom = std::clamp(camera.zoom * factor, 1.0f, 4.0f);
        const float newView = 1.0f / camera.zoom;
        camera.centerX = worldX - (u - 0.5f) * newView;
        camera.centerY = worldY - (v - 0.5f) * newView;
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
        if (mouseY <= 58.0f + edge && mouseY >= 58.0f) dy -= 1.0f;
        if (mouseY >= static_cast<float>(height) - edge) dy += 1.0f;

        if (dx == 0.0f && dy == 0.0f)
        {
            return;
        }

        const float length = std::sqrt(dx * dx + dy * dy);
        const float speed = 0.48f / camera.zoom;
        camera.centerX += (dx / length) * speed * dt;
        camera.centerY += (dy / length) * speed * dt;
        clampCamera(camera);
    }

    void renderPixelWater(SDL_Renderer* renderer, const AquaticWorld& world, const Camera& camera, int width, int height)
    {
        SDL_SetRenderDrawColor(renderer, 8, 29, 43, 255);
        SDL_RenderClear(renderer);

        constexpr int pixelSize = 10;
        const auto& definition = world.definition();
        const float view = 1.0f / camera.zoom;
        const float left = camera.centerX - view * 0.5f;
        const float top = camera.centerY - view * 0.5f;

        for (int sy = 58; sy < height; sy += pixelSize)
        {
            for (int sx = 0; sx < width; sx += pixelSize)
            {
                const float u = static_cast<float>(sx) / static_cast<float>(std::max(width, 1));
                const float v = static_cast<float>(sy - 58) / static_cast<float>(std::max(height - 58, 1));
                const float nx = left + u * view;
                const float ny = top + v * view;
                const int wx = static_cast<int>(nx * static_cast<float>(definition.logicalWidth));
                const int wy = static_cast<int>(ny * static_cast<float>(definition.logicalHeight));
                const float variation = world.waterVariationAt(wx, wy);

                const Uint8 r = static_cast<Uint8>(11 + variation * 10.0f);
                const Uint8 g = static_cast<Uint8>(45 + variation * 28.0f);
                const Uint8 b = static_cast<Uint8>(63 + variation * 35.0f);
                SDL_SetRenderDrawColor(renderer, r, g, b, 255);

                SDL_FRect pixel{
                    static_cast<float>(sx),
                    static_cast<float>(sy),
                    static_cast<float>(pixelSize + 1),
                    static_cast<float>(pixelSize + 1)};
                SDL_RenderFillRect(renderer, &pixel);
            }
        }
    }

    SDL_FPoint cellScreenPosition(const Cell& cell, const Camera& camera, int width, int height)
    {
        const float view = 1.0f / camera.zoom;
        const float left = camera.centerX - view * 0.5f;
        const float top = camera.centerY - view * 0.5f;
        return SDL_FPoint{
            ((cell.x - left) / view) * static_cast<float>(width),
            58.0f + ((cell.y - top) / view) * static_cast<float>(height - 58)};
    }

    void drawCell(SDL_Renderer* renderer, const Cell& cell, const Camera& camera, int width, int height, bool selected)
    {
        const SDL_FPoint position = cellScreenPosition(cell, camera, width, height);
        const float radius = std::max(6.0f, cell.radius * static_cast<float>(width) * camera.zoom);

        if (position.x < -radius || position.x > static_cast<float>(width) + radius ||
            position.y < 58.0f - radius || position.y > static_cast<float>(height) + radius)
        {
            return;
        }

        const float snappedX = std::floor(position.x / 2.0f) * 2.0f;
        const float snappedY = std::floor(position.y / 2.0f) * 2.0f;
        const float size = std::floor(radius * 2.0f / 2.0f) * 2.0f;

        SDL_FRect shadow{snappedX - size * 0.5f + 3.0f, snappedY - size * 0.5f + 3.0f, size, size};
        SDL_SetRenderDrawColor(renderer, 4, 20, 28, 180);
        SDL_RenderFillRect(renderer, &shadow);

        SDL_FRect body{snappedX - size * 0.5f, snappedY - size * 0.5f, size, size};
        SDL_SetRenderDrawColor(renderer, 115, 205, 181, 255);
        SDL_RenderFillRect(renderer, &body);

        SDL_FRect core{body.x + size * 0.28f, body.y + size * 0.28f, size * 0.44f, size * 0.44f};
        SDL_SetRenderDrawColor(renderer, 53, 130, 122, 255);
        SDL_RenderFillRect(renderer, &core);

        if (selected)
        {
            SDL_FRect outline{body.x - 3.0f, body.y - 3.0f, body.w + 6.0f, body.h + 6.0f};
            SDL_SetRenderDrawColor(renderer, 235, 244, 197, 255);
            SDL_RenderRect(renderer, &outline);
        }
    }

    std::string clockText(const SimulationClock& clock)
    {
        std::ostringstream stream;
        stream << "DAY " << clock.day() << "  "
               << std::setfill('0') << std::setw(2) << clock.hour() << ':'
               << std::setfill('0') << std::setw(2) << clock.minute();
        return stream.str();
    }

    void renderMainMenu(SDL_Renderer* renderer, int width, int height, float mouseX, float mouseY, Button& playButton, Button& editorButton)
    {
        SDL_SetRenderDrawColor(renderer, 7, 20, 30, 255);
        SDL_RenderClear(renderer);

        constexpr int block = 16;
        for (int y = 0; y < height; y += block)
        {
            for (int x = 0; x < width; x += block)
            {
                const int band = ((x / block) * 3 + (y / block) * 5) % 11;
                SDL_SetRenderDrawColor(renderer, 8, static_cast<Uint8>(28 + band), static_cast<Uint8>(42 + band * 2), 255);
                SDL_FRect rect{static_cast<float>(x), static_cast<float>(y), static_cast<float>(block), static_cast<float>(block)};
                SDL_RenderFillRect(renderer, &rect);
            }
        }

        const float titleSize = width < 900 ? 60.0f : 78.0f;
        const float titleX = (static_cast<float>(width) - textWidth("CLADIA", titleSize)) * 0.5f;
        drawText(renderer, "CLADIA", titleX, static_cast<float>(height) * 0.20f, titleSize, SDL_Color{221, 241, 237, 255});

        const float subtitleSize = 18.0f;
        const std::string_view subtitle = "CELLULAR EVOLUTION SIMULATION";
        drawText(
            renderer,
            subtitle,
            (static_cast<float>(width) - textWidth(subtitle, subtitleSize)) * 0.5f,
            static_cast<float>(height) * 0.34f,
            subtitleSize,
            SDL_Color{126, 171, 176, 255});

        playButton = Button{SDL_FRect{(static_cast<float>(width) - 250.0f) * 0.5f, static_cast<float>(height) * 0.52f, 250.0f, 54.0f}, "PLAY"};
        editorButton = Button{SDL_FRect{playButton.rect.x, playButton.rect.y + 68.0f, 250.0f, 48.0f}, "CELL EDITOR"};
        drawButton(renderer, playButton, mouseX, mouseY);
        drawButton(renderer, editorButton, mouseX, mouseY);
    }

    void renderCellEditor(SDL_Renderer* renderer, int width, int height)
    {
        SDL_SetRenderDrawColor(renderer, 8, 23, 32, 255);
        SDL_RenderClear(renderer);

        SDL_FRect top{0.0f, 0.0f, static_cast<float>(width), 58.0f};
        SDL_SetRenderDrawColor(renderer, 11, 31, 41, 255);
        SDL_RenderFillRect(renderer, &top);
        drawText(renderer, "CELL EDITOR", 18.0f, 14.0f, 24.0f, SDL_Color{224, 239, 237, 255});

        SDL_FRect canvas{32.0f, 90.0f, static_cast<float>(width) * 0.66f, static_cast<float>(height) - 126.0f};
        SDL_SetRenderDrawColor(renderer, 13, 38, 49, 255);
        SDL_RenderFillRect(renderer, &canvas);
        SDL_SetRenderDrawColor(renderer, 43, 88, 99, 255);
        SDL_RenderRect(renderer, &canvas);

        drawText(renderer, "EMPTY CELL", canvas.x + 20.0f, canvas.y + 18.0f, 18.0f, SDL_Color{151, 190, 190, 255});
        drawText(renderer, "COMPONENTS", canvas.x + canvas.w + 34.0f, canvas.y, 18.0f, SDL_Color{195, 218, 214, 255});
        drawText(renderer, "NONE YET", canvas.x + canvas.w + 34.0f, canvas.y + 34.0f, 16.0f, SDL_Color{111, 151, 153, 255});
        drawText(renderer, "ESC  BACK", 18.0f, static_cast<float>(height) - 34.0f, 14.0f, SDL_Color{105, 145, 148, 255});
    }

    void renderInspector(SDL_Renderer* renderer, const Cell& cell, int width, int height)
    {
        const float panelWidth = std::min(270.0f, static_cast<float>(width) * 0.28f);
        SDL_FRect panel{static_cast<float>(width) - panelWidth - 14.0f, 72.0f, panelWidth, 174.0f};
        SDL_SetRenderDrawColor(renderer, 10, 29, 38, 238);
        SDL_RenderFillRect(renderer, &panel);
        SDL_SetRenderDrawColor(renderer, 48, 102, 113, 255);
        SDL_RenderRect(renderer, &panel);

        drawText(renderer, "CELL", panel.x + 16.0f, panel.y + 14.0f, 22.0f, SDL_Color{222, 239, 235, 255});
        drawText(renderer, "ID " + std::to_string(cell.id), panel.x + 16.0f, panel.y + 48.0f, 16.0f, SDL_Color{147, 184, 182, 255});
        drawText(renderer, "COMPONENTS", panel.x + 16.0f, panel.y + 82.0f, 16.0f, SDL_Color{192, 213, 209, 255});
        drawText(renderer, "NONE YET", panel.x + 16.0f, panel.y + 108.0f, 15.0f, SDL_Color{108, 148, 149, 255});
        drawText(renderer, "AUTONOMOUS ONLY", panel.x + 16.0f, panel.y + 139.0f, 13.0f, SDL_Color{111, 165, 156, 255});
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

    SDL_SetRenderVSync(renderer, 1);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    uiFont = TTF_OpenFont("Arimo-Regular.ttf", 24.0f);
    if (uiFont == nullptr)
    {
        std::cerr << "TTF_OpenFont failed: " << SDL_GetError() << '\n';
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    bool running = true;
    Screen screen = Screen::MainMenu;
    AquaticWorld world = AquaticWorld::createDefault();
    CellSystem cells;
    SimulationClock clock;
    Camera camera;
    std::uint32_t selectedCellId = 0;

    Button playButton;
    Button editorButton;
    Button spawnButton;
    Button pauseButton;
    Button speed1Button;
    Button speed2Button;
    Button speed4Button;
    Button speed8Button;

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
                if (screen == Screen::MainMenu)
                {
                    running = false;
                }
                else
                {
                    screen = Screen::MainMenu;
                    selectedCellId = 0;
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
                {
                    changeZoom(camera, 1.2f, static_cast<float>(width) * 0.5f, static_cast<float>(height) * 0.5f, width, height);
                }
                else if (event.key.key == SDLK_MINUS || event.key.key == SDLK_KP_MINUS)
                {
                    changeZoom(camera, 1.0f / 1.2f, static_cast<float>(width) * 0.5f, static_cast<float>(height) * 0.5f, width, height);
                }
                else if (event.key.key == SDLK_SPACE)
                {
                    clock.togglePaused();
                }
            }

            if (event.type != SDL_EVENT_MOUSE_BUTTON_DOWN || event.button.button != SDL_BUTTON_LEFT)
            {
                continue;
            }

            if (screen == Screen::MainMenu)
            {
                if (pointInside(playButton.rect, event.button.x, event.button.y))
                {
                    cells.clear();
                    clock = SimulationClock{};
                    camera = Camera{};
                    selectedCellId = 0;
                    screen = Screen::Game;
                }
                else if (pointInside(editorButton.rect, event.button.x, event.button.y))
                {
                    screen = Screen::CellEditor;
                }
                continue;
            }

            if (screen != Screen::Game)
            {
                continue;
            }

            if (pointInside(spawnButton.rect, event.button.x, event.button.y))
            {
                selectedCellId = cells.spawnCell(camera.centerX, camera.centerY);
                continue;
            }
            if (pointInside(pauseButton.rect, event.button.x, event.button.y))
            {
                clock.togglePaused();
                continue;
            }
            if (pointInside(speed1Button.rect, event.button.x, event.button.y))
            {
                clock.setSpeed(1.0f);
                clock.setPaused(false);
                continue;
            }
            if (pointInside(speed2Button.rect, event.button.x, event.button.y))
            {
                clock.setSpeed(2.0f);
                clock.setPaused(false);
                continue;
            }
            if (pointInside(speed4Button.rect, event.button.x, event.button.y))
            {
                clock.setSpeed(4.0f);
                clock.setPaused(false);
                continue;
            }
            if (pointInside(speed8Button.rect, event.button.x, event.button.y))
            {
                clock.setSpeed(8.0f);
                clock.setPaused(false);
                continue;
            }

            if (event.button.y < 58.0f)
            {
                continue;
            }

            std::uint32_t closestId = 0;
            float closestDistance = 1.0e9f;
            for (const Cell& cell : cells.cells())
            {
                const SDL_FPoint position = cellScreenPosition(cell, camera, width, height);
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
            renderMainMenu(renderer, width, height, mouseX, mouseY, playButton, editorButton);
        }
        else if (screen == Screen::CellEditor)
        {
            renderCellEditor(renderer, width, height);
        }
        else
        {
            renderPixelWater(renderer, world, camera, width, height);

            for (const Cell& cell : cells.cells())
            {
                drawCell(renderer, cell, camera, width, height, cell.id == selectedCellId);
            }

            SDL_FRect topBar{0.0f, 0.0f, static_cast<float>(width), 58.0f};
            SDL_SetRenderDrawColor(renderer, 7, 23, 32, 245);
            SDL_RenderFillRect(renderer, &topBar);
            SDL_SetRenderDrawColor(renderer, 34, 82, 94, 255);
            SDL_FRect divider{0.0f, 57.0f, static_cast<float>(width), 1.0f};
            SDL_RenderFillRect(renderer, &divider);

            spawnButton = Button{SDL_FRect{14.0f, 10.0f, 130.0f, 38.0f}, "SPAWN CELL"};
            pauseButton = Button{SDL_FRect{static_cast<float>(width) - 330.0f, 10.0f, 52.0f, 38.0f}, clock.paused() ? "PLAY" : "PAUSE"};
            speed1Button = Button{SDL_FRect{static_cast<float>(width) - 270.0f, 10.0f, 54.0f, 38.0f}, "1X"};
            speed2Button = Button{SDL_FRect{static_cast<float>(width) - 210.0f, 10.0f, 54.0f, 38.0f}, "2X"};
            speed4Button = Button{SDL_FRect{static_cast<float>(width) - 150.0f, 10.0f, 54.0f, 38.0f}, "4X"};
            speed8Button = Button{SDL_FRect{static_cast<float>(width) - 90.0f, 10.0f, 54.0f, 38.0f}, "8X"};

            drawButton(renderer, spawnButton, mouseX, mouseY);
            drawButton(renderer, pauseButton, mouseX, mouseY, clock.paused());
            drawButton(renderer, speed1Button, mouseX, mouseY, !clock.paused() && std::abs(clock.speed() - 1.0f) < 0.01f);
            drawButton(renderer, speed2Button, mouseX, mouseY, !clock.paused() && std::abs(clock.speed() - 2.0f) < 0.01f);
            drawButton(renderer, speed4Button, mouseX, mouseY, !clock.paused() && std::abs(clock.speed() - 4.0f) < 0.01f);
            drawButton(renderer, speed8Button, mouseX, mouseY, !clock.paused() && std::abs(clock.speed() - 8.0f) < 0.01f);

            const std::string time = clockText(clock);
            drawText(renderer, time, 162.0f, 16.0f, 18.0f, SDL_Color{187, 215, 213, 255});

            const std::string countText = "CELLS " + std::to_string(cells.cells().size());
            drawText(renderer, countText, 300.0f, 17.0f, 15.0f, SDL_Color{103, 156, 158, 255});

            if (const Cell* selected = cells.findById(selectedCellId))
            {
                renderInspector(renderer, *selected, width, height);
            }
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
