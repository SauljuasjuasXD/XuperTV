#pragma once
#include "catalog.hpp"
#include "artwork.hpp"
#include <SDL.h>
#include <SDL_ttf.h>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
namespace xs {
// Texturas de texto reutilizadas entre cuadros: sin esto, animar a 60 fps obligaria
// a rasterizar cada texto en cada cuadro. La comparten el catalogo y el reproductor.
class TextCache {
public:
    struct Item{SDL_Texture* texture=nullptr;int w=0,h=0;Uint32 used=0;};
    // width>0 y wrap=false: una linea recortada con "…"; wrap=true: varias lineas.
    const Item& get(SDL_Renderer* renderer,TTF_Font* font,const std::string& text,SDL_Color color,int width,bool wrap);
    void tick();            // una vez por cuadro; descarta lo que lleva tiempo sin usarse
    void clear();
    ~TextCache(){clear();}
    // Fuente de respaldo (misma altura) para los simbolos que no trae la principal.
    std::unordered_map<TTF_Font*,TTF_Font*> fallback;
private:
    std::unordered_map<std::string,Item> items;Uint32 frame=0;Item empty;
};
// Interfaz estilo streaming con barra superior, cabecera "hero", filas de portadas y
// transiciones animadas (foco que se desliza, desplazamiento suave, fundidos).
class View {
public:
    SDL_Window* window=nullptr;SDL_Renderer* renderer=nullptr;
    TTF_Font *font=nullptr,*small=nullptr,*large=nullptr,*heading=nullptr,*title=nullptr,*chip=nullptr;
    TextCache texts;
    int nav_focus=-1,active_nav=0;bool favorite=false;
    // Segundos ya vistos de un episodio (0 = sin empezar); lo define la app.
    std::function<double(const Entry&)> resume;
    bool open(std::string& error);
    void draw(const Catalog& catalog,size_t selected,const std::string& status,bool full_memory);
    void splash(const std::string& status);
    void donation();
    bool poll_images(){return images&&images->poll();}
    bool animating()const{return busy;}                 // quedan transiciones por dibujar
    int columns(const Catalog& catalog)const;
    int page_size(const Catalog& catalog)const;
    // Navegacion con cruceta: devuelve el nuevo indice seleccionado.
    size_t move(const Catalog& catalog,size_t selected,int dx,int dy)const;
    bool at_top(const Catalog& catalog,size_t selected)const;   // arriba de aqui esta la barra
    // -8 favorite action; -2..-7 barra de navegacion; -1 no hit; >=0 catalogue index.
    int hit(const Catalog& catalog,size_t selected,int x,int y)const;
    void close();~View(){close();}
private:
    std::unique_ptr<Artwork> images;
    TTF_Font* wordmark=nullptr;SDL_Texture *logo_big=nullptr,*logo_small=nullptr;
    // Estado de animacion.
    Uint32 last_tick=0;float dt=0,alpha=1;bool busy=false;
    std::string scene;size_t last_selected=0,previous=0;
    float focus_t=1,hero_t=1,enter_t=1,scroll_y=0,ring[4]={0,0,0,0},ring_alpha=0,nav_x=0,nav_w=0,nav_alpha=0;
    std::vector<float> row_x;
    std::unordered_map<std::string,Uint32> seen;      // primera vez que se dibujo cada imagen
    mutable SDL_Rect nav_rect[6]={};
    bool approach(float& value,float target,float rate);
    bool advance(float& t,float seconds);
    void text(int x,int y,const std::string& message,SDL_Color color,int width=1160,TTF_Font* type=nullptr,int max_height=0);
    // Una sola linea, recortada con "…" si no cabe. align: 0 izquierda, 1 centro, 2 derecha.
    int line(int x,int y,const std::string& message,SDL_Color color,int width,TTF_Font* type=nullptr,int align=0);
    void rect(SDL_Rect r,SDL_Color color);
    void gradient(SDL_Rect r,SDL_Color top,SDL_Color bottom);
    void fade_x(SDL_Rect r,SDL_Color color,bool to_right);   // degradado de opaco a transparente
    void fade_y(SDL_Rect r,SDL_Color color,bool to_bottom);
    void corners(SDL_Rect r,int radius,SDL_Color background);
    void pill(SDL_Rect r,SDL_Color color,SDL_Color background);
    void brand(int x,int y,int size,TTF_Font* type);     // logo + "XuperTV"
    bool image(const std::string& url,const std::string& key,SDL_Rect bounds,bool cover);
    void card(const Entry& entry,SDL_Rect r,int art_h,int style,float focus,int number);
    void hero(const Catalog& catalog,const Entry* entry,size_t selected,int mode,int height,bool full_memory);
    void topbar();
};
}
