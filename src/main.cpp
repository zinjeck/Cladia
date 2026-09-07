#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "AbiogenesisSystem.h"
#include "AquaticWorld.h"
#include "ProtocellLifecycle.h"
#include "SimulationClock.h"

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

    void drawText(SDL_Renderer* renderer, std::string_view text, float x, float y, float size, SDL_Color color)
    {
        if (!uiFont || text.empty()) return;
        if (!TTF_SetFontSize(uiFont, size)) return;
        SDL_Surface* surface = TTF_RenderText_Blended(uiFont, text.data(), text.size(), color);
        if (!surface) return;
        SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
        if (texture)
        {
            SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
            SDL_FRect dst{x, y, static_cast<float>(surface->w), static_cast<float>(surface->h)};
            SDL_RenderTexture(renderer, texture, nullptr, &dst);
            SDL_DestroyTexture(texture);
        }
        SDL_DestroySurface(surface);
    }

    float textWidth(std::string_view text, float size)
    {
        if (!uiFont || text.empty()) return 0.0f;
        if (!TTF_SetFontSize(uiFont, size)) return 0.0f;
        int w = 0;
        int h = 0;
        return TTF_GetStringSize(uiFont, text.data(), text.size(), &w, &h) ? static_cast<float>(w) : 0.0f;
    }

    bool inside(const SDL_FRect& rect, float x, float y)
    {
        return x >= rect.x && x <= rect.x + rect.w && y >= rect.y && y <= rect.y + rect.h;
    }

    void drawButton(SDL_Renderer* renderer, const Button& button, float mx, float my, bool active = false)
    {
        const bool hovered = inside(button.rect, mx, my);
        const SDL_Color fill = active ? SDL_Color{52,132,154,255}
            : hovered ? SDL_Color{38,79,94,255} : SDL_Color{25,52,64,255};
        SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
        SDL_RenderFillRect(renderer, &button.rect);
        SDL_SetRenderDrawColor(renderer, 72,126,143,255);
        SDL_RenderRect(renderer, &button.rect);
        constexpr float size = 16.0f;
        drawText(renderer, button.label,
            button.rect.x + (button.rect.w - textWidth(button.label, size)) * 0.5f,
            button.rect.y + 10.0f, size, SDL_Color{226,239,241,255});
    }

    float viewW(const Camera& camera) { return 1.0f / camera.zoom; }
    float viewH(const Camera& camera) { return WorldHeight / camera.zoom; }

    float orderedClamp(float value, float a, float b)
    {
        return std::clamp(value, std::min(a,b), std::max(a,b));
    }

    void clampCamera(Camera& camera)
    {
        camera.zoom = std::clamp(camera.zoom, 1.0f, 4.0f);
        const float w = viewW(camera);
        const float h = viewH(camera);
        camera.centerX = w >= 0.999999f ? 0.5f : orderedClamp(camera.centerX, w * 0.5f, 1.0f - w * 0.5f);
        camera.centerY = h >= WorldHeight - 0.000001f
            ? (SkyTop + OceanBottom) * 0.5f
            : orderedClamp(camera.centerY, SkyTop + h * 0.5f, OceanBottom - h * 0.5f);
    }

    SDL_FPoint worldToScreen(float x, float y, const Camera& camera, int width, int height)
    {
        const float w = viewW(camera);
        const float h = viewH(camera);
        const float left = camera.centerX - w * 0.5f;
        const float top = camera.centerY - h * 0.5f;
        return {((x-left)/w)*static_cast<float>(width),
            58.0f + ((y-top)/h)*static_cast<float>(std::max(height-58,1))};
    }

    void updateCamera(Camera& camera, float dt, int width, int height, float mx, float my)
    {
        const bool* keys = SDL_GetKeyboardState(nullptr);
        float dx = 0.0f;
        float dy = 0.0f;
        if (keys[SDL_SCANCODE_A]) dx -= 1.0f;
        if (keys[SDL_SCANCODE_D]) dx += 1.0f;
        if (keys[SDL_SCANCODE_W]) dy -= 1.0f;
        if (keys[SDL_SCANCODE_S]) dy += 1.0f;
        constexpr float edge = 14.0f;
        if (mx <= edge) dx -= 1.0f;
        if (mx >= static_cast<float>(width)-edge) dx += 1.0f;
        if (my >= 58.0f && my <= 58.0f+edge) dy -= 1.0f;
        if (my >= static_cast<float>(height)-edge) dy += 1.0f;
        if (dx == 0.0f && dy == 0.0f) return;
        const float len = std::sqrt(dx*dx + dy*dy);
        camera.centerX += dx/len * (0.46f/camera.zoom) * dt;
        camera.centerY += dy/len * (0.56f/camera.zoom) * dt;
        clampCamera(camera);
    }

    void changeZoom(Camera& camera, float factor)
    {
        camera.zoom = std::clamp(camera.zoom * factor, 1.0f, 4.0f);
        clampCamera(camera);
    }

    float dayFraction(const SimulationClock& clock)
    {
        double seconds = std::fmod(clock.elapsedSimulationSeconds(), 86400.0);
        if (seconds < 0.0) seconds += 86400.0;
        return static_cast<float>(seconds / 86400.0);
    }

    float solarEnergy(const SimulationClock& clock)
    {
        const float t = dayFraction(clock);
        if (t < 0.25f || t >= 0.75f) return 0.0f;
        const float p = std::clamp((t-0.25f)/0.50f, 0.0f, 1.0f);
        return std::max(0.0f, std::sin(p*std::numbers::pi_v<float>) * 1000.0f);
    }

    bool day(const SimulationClock& clock)
    {
        const float t = dayFraction(clock);
        return t >= 0.25f && t < 0.75f;
    }

    SDL_FPoint celestialWorld(const SimulationClock& clock, bool sun)
    {
        const float t = dayFraction(clock);
        float p = 0.0f;
        if (sun) p = std::clamp((t-0.25f)/0.50f, 0.0f, 1.0f);
        else
        {
            p = t >= 0.75f ? (t-0.75f)/0.25f : t/0.25f;
            p = std::clamp(p,0.0f,1.0f);
        }
        return {0.06f+p*0.88f, -0.075f-std::sin(p*std::numbers::pi_v<float>)*0.095f};
    }

    void filledCircle(SDL_Renderer* r, float cx, float cy, float radius, SDL_Color c)
    {
        SDL_SetRenderDrawColor(r,c.r,c.g,c.b,c.a);
        const int rr = static_cast<int>(std::ceil(radius));
        for (int y=-rr; y<=rr; ++y)
        {
            const float half = std::sqrt(std::max(0.0f, radius*radius-static_cast<float>(y*y)));
            SDL_RenderLine(r,cx-half,cy+static_cast<float>(y),cx+half,cy+static_cast<float>(y));
        }
    }

    void hollowCircle(SDL_Renderer* r, float cx, float cy, float radius, SDL_Color c)
    {
        SDL_SetRenderDrawColor(r,c.r,c.g,c.b,c.a);
        constexpr int n=24;
        float lx=cx+radius, ly=cy;
        for(int i=1;i<=n;++i)
        {
            const float a=static_cast<float>(i)/static_cast<float>(n)*2.0f*std::numbers::pi_v<float>;
            const float x=cx+std::cos(a)*radius, y=cy+std::sin(a)*radius;
            SDL_RenderLine(r,lx,ly,x,y); lx=x; ly=y;
        }
    }

    std::string clockText(const SimulationClock& clock)
    {
        std::ostringstream s;
        s << "DAY " << clock.day() << "  " << std::setfill('0') << std::setw(2) << clock.hour()
          << ':' << std::setw(2) << clock.minute();
        return s.str();
    }

    void renderSunlightWedge(SDL_Renderer* renderer, const SimulationClock& clock, const Camera& camera, int width, int height)
    {
        if (!day(clock)) return;
        const SDL_FPoint sunW = celestialWorld(clock,true);
        const SDL_FPoint sunS = worldToScreen(sunW.x,sunW.y,camera,width,height);
        const SDL_FPoint surface = worldToScreen(sunW.x,0.0f,camera,width,height);
        if (surface.y < 58.0f || surface.y > static_cast<float>(height)) return;

        const float maxDepthY = std::min(static_cast<float>(height), surface.y + 300.0f);
        const float span = std::max(1.0f, maxDepthY-surface.y);
        for (float y=surface.y; y<maxDepthY; y+=5.0f)
        {
            const float p=(y-surface.y)/span;
            const float halfWidth=90.0f + p*260.0f;
            const Uint8 alpha=static_cast<Uint8>(std::max(2.0f,24.0f*(1.0f-p)));
            SDL_SetRenderDrawColor(renderer,245,229,153,alpha);
            SDL_RenderLine(renderer,sunS.x-halfWidth,y,sunS.x+halfWidth,y);
        }
        SDL_SetRenderDrawColor(renderer,245,229,153,32);
        SDL_RenderLine(renderer,sunS.x,sunS.y,sunS.x-90.0f,surface.y);
        SDL_RenderLine(renderer,sunS.x,sunS.y,sunS.x+90.0f,surface.y);
    }

    void renderOcean(SDL_Renderer* renderer, const AquaticWorld& world, const SimulationClock& clock, const Camera& camera, int width, int height)
    {
        SDL_SetRenderDrawColor(renderer,5,16,27,255);
        SDL_RenderClear(renderer);
        const float solar=solarEnergy(clock);
        const float daylight=std::clamp(solar/1000.0f,0.0f,1.0f);
        const float h=viewH(camera);
        const float top=camera.centerY-h*0.5f;
        for(int sy=58; sy<height; sy+=4)
        {
            const float v=static_cast<float>(sy-58)/static_cast<float>(std::max(height-58,1));
            const float wy=top+v*h;
            if(wy<0.0f)
            {
                const float k=std::clamp((wy-SkyTop)/(0.0f-SkyTop),0.0f,1.0f);
                SDL_SetRenderDrawColor(renderer,
                    static_cast<Uint8>(6+daylight*(42+k*24)),
                    static_cast<Uint8>(14+daylight*(78+k*30)),
                    static_cast<Uint8>(27+daylight*(126+k*38)),255);
            }
            else
            {
                const float depth=std::clamp(wy,0.0f,1.0f);
                const EnvironmentSample env=world.environmentAt(0.5f,depth,solar);
                const float light=std::clamp(env.sunlight/1000.0f,0.0f,1.0f);
                const float deep=std::pow(depth,0.72f);
                SDL_SetRenderDrawColor(renderer,
                    static_cast<Uint8>(std::clamp(7.0f+light*14.0f-deep*4.0f,2.0f,255.0f)),
                    static_cast<Uint8>(std::clamp(37.0f+light*42.0f-deep*25.0f,8.0f,255.0f)),
                    static_cast<Uint8>(std::clamp(56.0f+light*58.0f-deep*30.0f,15.0f,255.0f)),255);
            }
            SDL_FRect band{0.0f,static_cast<float>(sy),static_cast<float>(width),5.0f};
            SDL_RenderFillRect(renderer,&band);
        }

        const SDL_FPoint surfaceL=worldToScreen(0.0f,0.0f,camera,width,height);
        const SDL_FPoint surfaceR=worldToScreen(1.0f,0.0f,camera,width,height);
        if(surfaceL.y>=58.0f && surfaceL.y<=height)
        {
            SDL_SetRenderDrawColor(renderer,111,178,191,220);
            SDL_RenderLine(renderer,surfaceL.x,surfaceL.y,surfaceR.x,surfaceR.y);
        }

        renderSunlightWedge(renderer,clock,camera,width,height);

        const bool sun=day(clock);
        const SDL_FPoint bodyW=celestialWorld(clock,sun);
        const SDL_FPoint bodyS=worldToScreen(bodyW.x,bodyW.y,camera,width,height);
        if(bodyS.y>=58.0f && bodyS.y<=height)
        {
            if(sun) filledCircle(renderer,bodyS.x,bodyS.y,18.0f,{245,222,126,255});
            else
            {
                filledCircle(renderer,bodyS.x,bodyS.y,15.0f,{199,211,218,255});
                filledCircle(renderer,bodyS.x+6.0f,bodyS.y-3.0f,13.0f,{8,18,30,255});
            }
        }
    }

    const PrimitiveParticle* findParticle(const AbiogenesisSystem& system, std::uint32_t id)
    {
        for(const PrimitiveParticle& p:system.particles()) if(p.id==id) return &p;
        return nullptr;
    }

    void renderChemistry(SDL_Renderer* renderer, const AbiogenesisSystem& system, const Camera& camera, int width, int height)
    {
        for(const PrimitiveParticle& p:system.particles())
        {
            if(p.kind==PrimitiveKind::RnaTriplet && p.backLink!=0)
            {
                if(const PrimitiveParticle* q=findParticle(system,p.backLink))
                {
                    const SDL_FPoint a=worldToScreen(p.body.x,p.body.y,camera,width,height);
                    const SDL_FPoint b=worldToScreen(q->body.x,q->body.y,camera,width,height);
                    SDL_SetRenderDrawColor(renderer,176,64,73,230);
                    SDL_RenderLine(renderer,a.x,a.y,b.x,b.y);
                    SDL_RenderLine(renderer,a.x,a.y+1.0f,b.x,b.y+1.0f);
                    filledCircle(renderer,(a.x+b.x)*0.5f,(a.y+b.y)*0.5f,1.4f,{210,87,96,230});
                }
            }
            if(p.kind==PrimitiveKind::Lipid)
            {
                for(std::uint32_t id:p.lipidLinks)
                {
                    if(id<p.id) continue;
                    if(const PrimitiveParticle* q=findParticle(system,id))
                    {
                        const SDL_FPoint a=worldToScreen(p.body.x,p.body.y,camera,width,height);
                        const SDL_FPoint b=worldToScreen(q->body.x,q->body.y,camera,width,height);
                        SDL_SetRenderDrawColor(renderer,93,185,218,p.inProtoCell?220:150);
                        SDL_RenderLine(renderer,a.x,a.y,b.x,b.y);
                    }
                }
            }
        }

        for(const HydrothermalVent& vent:system.vents())
        {
            const SDL_FPoint s=worldToScreen(vent.x,vent.y,camera,width,height);
            SDL_SetRenderDrawColor(renderer,45,42,38,255);
            SDL_RenderLine(renderer,s.x-18.0f,s.y+14.0f,s.x,s.y-16.0f);
            SDL_RenderLine(renderer,s.x,s.y-16.0f,s.x+18.0f,s.y+14.0f);
            SDL_RenderLine(renderer,s.x-18.0f,s.y+14.0f,s.x+18.0f,s.y+14.0f);
            filledCircle(renderer,s.x,s.y-13.0f,4.0f,{235,113,62,255});
        }

        for(const EnergyRay& ray:system.energyRays())
        {
            const SDL_FPoint a=worldToScreen(ray.x,ray.y,camera,width,height);
            const SDL_FPoint b=worldToScreen(ray.x-ray.vx*0.12f,ray.y-ray.vy*0.12f,camera,width,height);
            SDL_SetRenderDrawColor(renderer,ray.solar?247:255,ray.solar?224:137,ray.solar?136:74,150);
            SDL_RenderLine(renderer,a.x,a.y,b.x,b.y);
        }

        for(const PrimitiveParticle& p:system.particles())
        {
            const SDL_FPoint s=worldToScreen(p.body.x,p.body.y,camera,width,height);
            if(s.y<58.0f || s.y>height) continue;
            switch(p.kind)
            {
            case PrimitiveKind::Lipid:
            {
                const SDL_Color lipidColor = p.inProtoCell
                    ? SDL_Color{151,235,248,255}
                    : p.inClosedLipidLoop ? SDL_Color{126,224,244,255} : SDL_Color{105,201,232,255};
                filledCircle(renderer,s.x,s.y,2.5f,lipidColor);
                break;
            }
            case PrimitiveKind::RnaTriplet:
            {
                const float rnaRadius = 5.0f;
                hollowCircle(renderer,s.x,s.y,rnaRadius,p.stopTriplet?SDL_Color{245,99,111,255}:SDL_Color{218,73,84,255});
                if(!p.stopTriplet)
                {
                    SDL_SetRenderDrawColor(renderer,218,73,84,255);
                    SDL_FRect peg{s.x+4.2f,s.y-1.2f,3.0f,2.4f};
                    SDL_RenderFillRect(renderer,&peg);
                }
                SDL_SetRenderDrawColor(renderer,218,73,84,255);
                SDL_FRect socket{s.x-6.8f,s.y-1.4f,2.2f,2.8f};
                SDL_RenderRect(renderer,&socket);
                const float labelSize = 6.2f;
                drawText(renderer,p.triplet,s.x-textWidth(p.triplet,labelSize)*0.5f,s.y-4.0f,labelSize,{255,202,204,255});
                break;
            }
            case PrimitiveKind::Peptide:
            {
                if(p.excited) filledCircle(renderer,s.x,s.y,6.0f,{255,197,78,55});
                filledCircle(renderer,s.x,s.y,3.0f,{239,143,48,255});
                break;
            }
            case PrimitiveKind::Atp:
            {
                SDL_SetRenderDrawColor(renderer,248,224,79,255);
                SDL_RenderLine(renderer,s.x-4.0f,s.y-4.0f,s.x+1.0f,s.y-1.0f);
                SDL_RenderLine(renderer,s.x+1.0f,s.y-1.0f,s.x-2.0f,s.y+2.0f);
                SDL_RenderLine(renderer,s.x-2.0f,s.y+2.0f,s.x+4.0f,s.y+5.0f);
                break;
            }
            }
        }
    }

    void renderMenu(SDL_Renderer* renderer,int width,int height,float mx,float my,Button& play,Button& library)
    {
        SDL_SetRenderDrawColor(renderer,8,25,35,255); SDL_RenderClear(renderer);
        const float title=width<900?60.0f:78.0f;
        drawText(renderer,"CLADIA",(width-textWidth("CLADIA",title))*0.5f,height*0.20f,title,{221,241,237,255});
        drawText(renderer,"ABIOGENESIS EVOLUTION SIMULATION",(width-textWidth("ABIOGENESIS EVOLUTION SIMULATION",18.0f))*0.5f,height*0.34f,18.0f,{126,171,176,255});
        const float x=(width-260.0f)*0.5f;
        play={{x,height*0.52f,260.0f,54.0f},"PLAY"};
        library={{x,height*0.52f+68.0f,260.0f,48.0f},"SPECIES LIBRARY"};
        drawButton(renderer,play,mx,my); drawButton(renderer,library,mx,my);
    }
}

int main()
{
    if(!SDL_Init(SDL_INIT_VIDEO)) return 1;
    if(!TTF_Init()){SDL_Quit();return 1;}
    SDL_Window* window=SDL_CreateWindow("Cladia",1280,720,SDL_WINDOW_RESIZABLE);
    if(!window){TTF_Quit();SDL_Quit();return 1;}
    SDL_Renderer* renderer=SDL_CreateRenderer(window,nullptr);
    if(!renderer){SDL_DestroyWindow(window);TTF_Quit();SDL_Quit();return 1;}
    SDL_SetRenderVSync(renderer,1);
    SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);
    uiFont=TTF_OpenFont("Arimo-Regular.ttf",24.0f);
    if(!uiFont){SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);TTF_Quit();SDL_Quit();return 1;}

    Screen screen=Screen::MainMenu;
    bool running=true;
    AquaticWorld world=AquaticWorld::createDefault();
    SimulationClock clock;
    AbiogenesisSystem abiogenesis;
    ProtocellLifecycleSystem lifecycle;
    abiogenesis.reset();
    lifecycle.reset(abiogenesis);
    Camera camera;
    Button playButton,libraryButton,backButton,pauseButton,s1,s2,s4,s8;
    auto previous=std::chrono::steady_clock::now();

    while(running)
    {
        const auto now=std::chrono::steady_clock::now();
        const float dt=std::clamp(std::chrono::duration<float>(now-previous).count(),0.0f,0.05f);
        previous=now;
        int width=0,height=0; SDL_GetWindowSizeInPixels(window,&width,&height);
        float mx=0.0f,my=0.0f; SDL_GetMouseState(&mx,&my);

        if(screen==Screen::Game)
        {
            updateCamera(camera,dt,width,height,mx,my);
            clock.update(dt);
            const bool sun=day(clock);
            const SDL_FPoint sunW=celestialWorld(clock,true);
            const float chemistryDt = clock.paused()?0.0f:dt*clock.speed();
            abiogenesis.update(chemistryDt,clock.elapsedSimulationSeconds(),solarEnergy(clock),sun?sunW.x:0.5f);
            lifecycle.update(abiogenesis,chemistryDt);
        }

        SDL_Event event;
        while(SDL_PollEvent(&event))
        {
            if(event.type==SDL_EVENT_QUIT){running=false;continue;}
            if(event.type==SDL_EVENT_KEY_DOWN && event.key.key==SDLK_ESCAPE)
            {
                if(screen==Screen::MainMenu) running=false; else screen=Screen::MainMenu;
                continue;
            }
            if(screen==Screen::Game && event.type==SDL_EVENT_MOUSE_WHEEL)
            {
                changeZoom(camera,event.wheel.y>0?1.2f:1.0f/1.2f); continue;
            }
            if(screen==Screen::Game && event.type==SDL_EVENT_KEY_DOWN)
            {
                if(event.key.key==SDLK_PLUS||event.key.key==SDLK_EQUALS||event.key.key==SDLK_KP_PLUS) changeZoom(camera,1.2f);
                else if(event.key.key==SDLK_MINUS||event.key.key==SDLK_KP_MINUS) changeZoom(camera,1.0f/1.2f);
                else if(event.key.key==SDLK_SPACE) clock.togglePaused();
            }
            if(event.type!=SDL_EVENT_MOUSE_BUTTON_DOWN || event.button.button!=SDL_BUTTON_LEFT) continue;
            if(screen==Screen::MainMenu)
            {
                if(inside(playButton.rect,event.button.x,event.button.y))
                {
                    clock=SimulationClock{};
                    camera=Camera{};
                    abiogenesis.reset();
                    lifecycle.reset(abiogenesis);
                    screen=Screen::Game;
                }
                else if(inside(libraryButton.rect,event.button.x,event.button.y)) screen=Screen::SpeciesLibrary;
                continue;
            }
            if(screen==Screen::SpeciesLibrary)
            {
                if(inside(backButton.rect,event.button.x,event.button.y)) screen=Screen::MainMenu;
                continue;
            }
            if(screen==Screen::Game)
            {
                if(inside(backButton.rect,event.button.x,event.button.y)){screen=Screen::MainMenu;continue;}
                if(inside(pauseButton.rect,event.button.x,event.button.y)){clock.togglePaused();continue;}
                if(inside(s1.rect,event.button.x,event.button.y)){clock.setSpeed(1.0f);clock.setPaused(false);continue;}
                if(inside(s2.rect,event.button.x,event.button.y)){clock.setSpeed(2.0f);clock.setPaused(false);continue;}
                if(inside(s4.rect,event.button.x,event.button.y)){clock.setSpeed(4.0f);clock.setPaused(false);continue;}
                if(inside(s8.rect,event.button.x,event.button.y)){clock.setSpeed(8.0f);clock.setPaused(false);continue;}
            }
        }

        if(screen==Screen::MainMenu) renderMenu(renderer,width,height,mx,my,playButton,libraryButton);
        else if(screen==Screen::SpeciesLibrary)
        {
            SDL_SetRenderDrawColor(renderer,8,23,32,255); SDL_RenderClear(renderer);
            backButton={{18.0f,14.0f,86.0f,36.0f},"BACK"}; drawButton(renderer,backButton,mx,my);
            drawText(renderer,"SPECIES LIBRARY",124.0f,20.0f,30.0f,{224,239,237,255});
            drawText(renderer,"NO SPECIES HAVE EMERGED YET",30.0f,100.0f,20.0f,{160,190,190,255});
        }
        else
        {
            renderOcean(renderer,world,clock,camera,width,height);
            renderChemistry(renderer,abiogenesis,camera,width,height);
            SDL_FRect bar{0,0,static_cast<float>(width),58.0f}; SDL_SetRenderDrawColor(renderer,7,23,32,245); SDL_RenderFillRect(renderer,&bar);
            backButton={{10,10,80,38},"BACK"};
            pauseButton={{static_cast<float>(width)-330,10,52,38},clock.paused()?"PLAY":"PAUSE"};
            s1={{static_cast<float>(width)-270,10,54,38},"1X"}; s2={{static_cast<float>(width)-210,10,54,38},"2X"};
            s4={{static_cast<float>(width)-150,10,54,38},"4X"}; s8={{static_cast<float>(width)-90,10,54,38},"8X"};
            drawButton(renderer,backButton,mx,my); drawButton(renderer,pauseButton,mx,my,clock.paused());
            drawButton(renderer,s1,mx,my,!clock.paused()&&std::abs(clock.speed()-1.0f)<0.01f);
            drawButton(renderer,s2,mx,my,!clock.paused()&&std::abs(clock.speed()-2.0f)<0.01f);
            drawButton(renderer,s4,mx,my,!clock.paused()&&std::abs(clock.speed()-4.0f)<0.01f);
            drawButton(renderer,s8,mx,my,!clock.paused()&&std::abs(clock.speed()-8.0f)<0.01f);
            drawText(renderer,clockText(clock),110.0f,16.0f,18.0f,{187,215,213,255});
            drawText(renderer,"PRIMITIVE CHEMISTRY",280.0f,17.0f,15.0f,{103,156,158,255});
        }
        SDL_RenderPresent(renderer);
    }

    TTF_CloseFont(uiFont); uiFont=nullptr;
    SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); TTF_Quit(); SDL_Quit();
    return 0;
}
