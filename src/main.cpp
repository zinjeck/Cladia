#include <SDL3/SDL.h>
#include <iostream>

int main()
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Cladia",
        1280,
        720,
        SDL_WINDOW_RESIZABLE
    );

    if (window == nullptr)
    {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << '\n';
        SDL_Quit();
        return 1;
    }

    bool running = true;

    while (running)
    {
        SDL_Event event;

        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT)
            {
                running = false;
            }
        }

        SDL_Delay(1);
    }

    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}