// Renderiza la interfaz con un catalogo sintetico y guarda capturas BMP. Comprueba tambien
// la cache de imagenes, la navegacion con cruceta y las zonas tactiles.
#include "view.hpp"
#include <cassert>
#include <iostream>
static void snapshot(xs::View& view,const char* file){auto* s=SDL_CreateRGBSurfaceWithFormat(0,1280,720,32,SDL_PIXELFORMAT_RGBA32);assert(s);assert(SDL_RenderReadPixels(view.renderer,nullptr,s->format->format,s->pixels,s->pitch)==0);assert(SDL_SaveBMP(s,file)==0);SDL_FreeSurface(s);}
// Dibuja hasta que terminan las animaciones (y llegan las imagenes) y captura.
static void settle(xs::View& view,const xs::Catalog& c,size_t selected,const char* status,const char* file,int frames=90){
    for(int i=0;i<frames;++i){view.draw(c,selected,status,true);SDL_Delay(8);}
    if(file)snapshot(view,file);
}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    xs::View view;std::string error;assert(view.open(error));
    const std::string base=argv[1];
    {xs::Artwork art(view.renderer);
     for(const char* name:{"logo.png","poster.jpg","poster.webp"}){const auto url=base+"/"+name,key=xs::artwork_key(url);SDL_Texture* texture=nullptr;const auto start=SDL_GetTicks();while(!texture&&SDL_GetTicks()-start<10000){texture=art.get(url,key);art.poll();SDL_Delay(10);}assert(texture);int w=0,h=0;SDL_QueryTexture(texture,nullptr,nullptr,&w,&h);assert(w>0&&h>0&&w<=512&&h<=768);}
     assert(!art.get("file:///etc/passwd","0123456789abcdef"));assert(!art.get(base+"/logo.png","../invalid"));}
    auto with_art=[&](xs::Entry& e,const char* name){e.artwork=base+"/"+name;e.artwork_cache=xs::artwork_key(e.artwork);};
    view.resume=[](const xs::Entry& e){return e.content_id=="visto"?754.0:0.0;};

    // Inicio por filas: seguir viendo, explorar y canales favoritos.
    xs::Catalog home;home.title="Inicio";
    for(int i=0;i<5;++i){xs::Entry e;e.kind=xs::EntryKind::Episode;e.title=i%2?"Horizonte - T1 E"+std::to_string(i+3):"El ultimo faro";e.content_id="visto";e.row="Seguir viendo";if(i%2){e.title="Horizonte";e.series_id="serie"+std::to_string(i);e.description="Episodio "+std::to_string(i+3);}with_art(e,i%2?"poster.webp":"poster.jpg");home.entries.push_back(e);}
    for(auto pair:{std::pair<const char*,xs::EntryKind>{"TV en vivo",xs::EntryKind::LiveSection},{"Peliculas",xs::EntryKind::Section},{"Series",xs::EntryKind::Section},{"Continuar viendo",xs::EntryKind::Resume},{"Favoritos",xs::EntryKind::Favorites},{"Apoyar el desarrollo",xs::EntryKind::Donation}}){xs::Entry e;e.title=pair.first;e.kind=pair.second;e.program_type=e.title=="Series"?"0":"1";e.row="Explorar";home.entries.push_back(e);}
    for(int i=0;i<7;++i){xs::Entry e;e.kind=xs::EntryKind::Channel;e.title="Canal favorito "+std::to_string(i+1);e.channel_code="c";e.row="Tus canales favoritos";with_art(e,"logo.png");home.entries.push_back(e);}
    settle(view,home,1,"Elige que quieres ver. X: buscar en todo el catalogo.","home.bmp");
    // Cruceta: derecha dentro de la fila, abajo a la siguiente fila, arriba vuelve; la barra queda sobre la primera.
    assert(view.move(home,1,1,0)==2&&view.move(home,0,-1,0)==0&&view.move(home,4,1,0)==4);
    assert(view.move(home,1,0,1)==6&&view.move(home,6,0,-1)==1&&view.move(home,11,0,1)==11&&view.move(home,9,0,1)==13);
    assert(view.at_top(home,3)&&!view.at_top(home,6));
    assert(view.hit(home,1,60,320)==0&&view.hit(home,1,330,320)==1&&view.hit(home,1,1250,715)==-1);
    settle(view,home,7,"","home_explorar.bmp");
    view.nav_focus=2;settle(view,home,1,"","menu.bmp",60);view.nav_focus=-1;

    xs::Catalog channels;channels.title="TV en vivo / Deportes";for(int i=0;i<12;++i){xs::Entry e;e.title="Canal de prueba "+std::to_string(i+1);e.kind=xs::EntryKind::Channel;with_art(e,"logo.png");channels.entries.push_back(e);}
    view.favorite=true;view.active_nav=1;settle(view,channels,1,"Canal guardado en Favoritos.","channels.bmp");
    assert(view.hit(channels,1,150,170)==-8&&view.hit(channels,1,60,240)==0&&view.move(channels,1,0,1)==6&&view.at_top(channels,4)&&!view.at_top(channels,5));
    view.favorite=false;

    xs::Catalog categories;categories.title="TV en vivo";for(const char* name:{"Deportes","Noticias","Cine y series","Infantil","Musica","Documentales","Entretenimiento","Cultura","Internacional"}){xs::Entry e;e.title=name;e.kind=xs::EntryKind::Category;categories.entries.push_back(e);}
    settle(view,categories,5,"Elige una categoria.","categories.bmp");assert(view.hit(categories,5,60,220)==0&&view.hit(categories,5,400,340)==5);

    xs::Catalog movies;movies.title="Peliculas / Estrenos";for(int i=0;i<20;++i){xs::Entry e;e.kind=xs::EntryKind::Asset;e.title=i%2?"Horizonte: una nueva aventura":"El ultimo faro";e.program_type=i%3==0?"0":"1";
        if(i%2)e.description="Una tripulacion improvisada cruza el oceano en busca de una isla que no aparece en ningun mapa.";with_art(e,i%2?"poster.webp":"poster.jpg");if(i==5)e.artwork_cache.clear();movies.entries.push_back(e);}
    view.active_nav=2;settle(view,movies,3,"Portadas de prueba; no representan el catalogo real.","movies.bmp");
    assert(view.move(movies,3,0,1)==10&&view.move(movies,10,0,-1)==3&&view.columns(movies)==7);
    // Cuadro intermedio: el foco se desliza y la fila se desplaza.
    for(int i=0;i<5;++i){view.draw(movies,11,"",true);SDL_Delay(16);}snapshot(view,"movies_transicion.bmp");settle(view,movies,11,"",nullptr,40);

    xs::Catalog episodes;episodes.title="Horizonte";episodes.description="Una aventura para descubrir nuevos mundos. Selecciona un episodio y continua desde donde lo dejaste.";
    episodes.artwork=base+"/poster.jpg";episodes.artwork_cache=xs::artwork_key(episodes.artwork);
    for(int i=0;i<9;++i){xs::Entry e;e.kind=xs::EntryKind::Episode;e.title="Episodio "+std::to_string(i+1)+" - Un nuevo comienzo";if(i==1)e.content_id="visto";if(i%2==0)with_art(e,"poster.webp");episodes.entries.push_back(e);}
    settle(view,episodes,2,"A: reproducir. B: volver a las portadas.","detail.bmp");
    assert(view.move(episodes,2,0,1)==3&&view.move(episodes,0,0,-1)==0&&view.at_top(episodes,0)&&!view.at_top(episodes,1));
    view.splash("Cargando tu catalogo...");snapshot(view,"splash.bmp");view.donation();snapshot(view,"donation.bmp");
    std::cout<<"UI: inicio por filas, rejillas, lista de episodios, navegacion y zonas tactiles correctas\n";return 0;
}
