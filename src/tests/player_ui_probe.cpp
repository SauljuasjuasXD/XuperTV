// Reproduce un video y pulsa teclas en un guion fijo para capturar la interfaz del reproductor.
// uso (binario con XUPERSWITCH_TEST): XS_PLAYER_SHOTS=prefijo player_ui_probe http://127.0.0.1:PUERTO/test.mp4
#include "player.hpp"
#include "view.hpp"
#include "trace.hpp"
#include <atomic>
#include <iostream>
#include <thread>
int main(int argc,char** argv){
    if(argc!=2)return 2;
    xs::trace_reset();xs::View view;std::string error;if(!view.open(error)){std::cerr<<error<<'\n';return 1;}
    std::atomic<bool> stop{false};
    std::thread keys([&]{
        struct Step{Uint32 at;SDL_Keycode key;};
        const Step script[]={{700,SDLK_UP},{800,SDLK_UP},{900,SDLK_UP},{1000,SDLK_UP},{1100,SDLK_UP},{1200,SDLK_UP},{1300,SDLK_UP},{1400,SDLK_UP},
            {1900,SDLK_RIGHT},{2700,SDLK_SPACE},{3700,SDLK_SPACE},{4300,SDLK_x},{4700,SDLK_DOWN},{4900,SDLK_DOWN},{5100,SDLK_DOWN},{5900,SDLK_ESCAPE},{6400,SDLK_ESCAPE}};
        const Uint32 start=SDL_GetTicks();
        for(const auto& step:script){while(!stop&&SDL_GetTicks()-start<step.at)SDL_Delay(10);if(stop)return;
            SDL_Event e{};e.type=SDL_KEYDOWN;e.key.keysym.sym=step.key;SDL_PushEvent(&e);}
    });
    xs::PlaybackOptions options;options.font=view.font;options.font_small=view.small;options.font_title=view.heading;options.font_chip=view.chip;options.texts=&view.texts;
    options.title="Horizonte: una nueva aventura";options.items={"Episodio 1","Episodio 2","Episodio 3"};options.item=1;options.volume=100;
    xs::PlaybackOutcome outcome;error=xs::play(argv[1],view.renderer,{},options,&outcome);
    stop=true;keys.join();view.close();
    if(!error.empty()){std::cerr<<error<<'\n';return 1;}
    std::cout<<"player ui: volumen final "<<outcome.volume<<"\n";return 0;
}
