#include "view.hpp"
#include "trace.hpp"
#include "donation_qr.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
// Fuentes (Inter, licencia OFL) y logo incrustados en el binario: la app no depende de
// archivos sueltos en la SD para dibujarse. Las rutas son relativas a src/.
#ifdef _WIN32
#define XS_RODATA ".section .rdata,\"dr\"\n"
#else
#define XS_RODATA ".section .rodata\n"
#endif
#define XS_EMBED(name,file) __asm__(XS_RODATA ".balign 8\n.global xs_" #name "\nxs_" #name ":\n.incbin \"" file "\"\n.global xs_" #name "_end\nxs_" #name "_end:\n.byte 0\n.text\n");\
    extern "C"{extern const unsigned char xs_##name[],xs_##name##_end[];}
XS_EMBED(font_regular,"assets/Inter-Regular.ttf")
XS_EMBED(font_semibold,"assets/Inter-SemiBold.ttf")
XS_EMBED(font_display,"assets/InterDisplay-Bold.ttf")
XS_EMBED(logo,"assets/logo.rgba")
XS_EMBED(logo_small,"assets/logo_small.rgba")
namespace xs {
namespace {
// Paleta: negro carbon, grises neutros y rojo como unico color de acento.
constexpr SDL_Color bg{20,20,20,255},surface{31,31,31,255},surface2{54,54,54,255},white{255,255,255,255},muted{179,179,179,255},dim{128,128,128,255},accent{229,9,20,255},gold{245,197,66,255},ink{20,20,20,255},slate{66,66,66,255},coal{28,28,28,255};
// Pantalla logica 1280x720: barra superior (BAR), contenido de X0 a X0+CW, pie desde FOOT.
constexpr int X0=48,CW=1184,BAR=64,FOOT=690,GAP=16,ROW_H=238;
constexpr SDL_Rect favorite_area{X0,160,760,32};
const char* nav[]={"Inicio","TV en vivo","Películas","Series","Favoritos","♥ Apoyar"};
const char* version="0.14.1";
enum Mode{Grid=0,List=1,Rows=2};
enum Style{Poster=0,Logo=1,Wide=2,Tile=3,Label=4,Episode=5};
struct Cell{SDL_Rect r;int art_h;int row;int style;};
// Posiciones absolutas (sin desplazar) de todas las tarjetas y el desplazamiento que
// deja visible la seleccion. draw() anima hacia esos objetivos; hit() y move() los leen.
struct Plan{int mode=Grid,cols=1,top=214,hero=196;std::vector<Cell> cells;std::vector<std::pair<size_t,size_t>> rows;float scroll_y=0;std::vector<float> row_x;SDL_Rect clip{0,204,1280,FOOT-204};};
bool sectioned(const Catalog& c){return !c.entries.empty()&&!c.entries[0].row.empty();}
int row_style(const Entry& e){return e.kind==EntryKind::Channel?Logo:e.kind==EntryKind::Episode||e.kind==EntryKind::Asset?Wide:Tile;}
Plan make_plan(const Catalog& c,size_t selected){
    Plan p;const size_t n=c.entries.size();if(!n)return p;
    selected=std::min(selected,n-1);
    if(sectioned(c)){
        p.mode=Rows;p.hero=236;p.top=244;
        for(size_t b=0;b<n;){size_t e=b+1;while(e<n&&c.entries[e].row==c.entries[b].row)++e;p.rows.push_back({b,e});b=e;}
        p.row_x.assign(p.rows.size(),0);p.cells.reserve(n);size_t current=0;
        for(size_t r=0;r<p.rows.size();++r){
            const int style=row_style(c.entries[p.rows[r].first]),w=style==Logo?214:250,art=style==Logo?120:141,h=style==Wide?art+50:style==Logo?art+30:art;
            for(size_t i=p.rows[r].first;i<p.rows[r].second;++i){const int j=static_cast<int>(i-p.rows[r].first);
                p.cells.push_back({{X0+j*(w+GAP),p.top+static_cast<int>(r)*ROW_H+40,w,h},art,static_cast<int>(r),style});
                if(i==selected){current=r;const int total=static_cast<int>(p.rows[r].second-p.rows[r].first)*(w+GAP)-GAP;
                    p.row_x[r]=static_cast<float>(std::clamp((j-2)*(w+GAP),0,std::max(0,total-CW)));}
            }
        }
        const int content=static_cast<int>(p.rows.size())*ROW_H;
        p.scroll_y=static_cast<float>(std::clamp(static_cast<int>(current)*ROW_H,0,std::max(0,content-(FOOT-p.top))));
        p.clip={0,p.top,1280,FOOT-p.top};return p;
    }
    const EntryKind kind=c.entries[0].kind;p.cells.reserve(n);
    if(kind==EntryKind::Episode){
        p.mode=List;p.hero=0;p.top=92;const int h=96,x=456,w=776;
        for(size_t i=0;i<n;++i)p.cells.push_back({{x,p.top+static_cast<int>(i)*(h+10),w,h},h,0,Episode});
        const int visible=(FOOT-p.top+10)/(h+10);
        p.scroll_y=static_cast<float>(std::clamp(static_cast<int>(selected)-2,0,std::max(0,static_cast<int>(n)-visible))*(h+10));
        p.clip={x-20,p.top-12,w+40,FOOT-p.top+12};return p;
    }
    int w=284,art=96,h=96,style=Label;p.cols=4;
    if(kind==EntryKind::Asset){p.cols=7;w=155;art=232;h=art+34;p.hero=312;p.top=330;style=Poster;}
    else if(kind==EntryKind::Channel){p.cols=5;w=224;art=126;h=art+34;p.hero=212;p.top=230;style=Logo;}
    for(size_t i=0;i<n;++i)p.cells.push_back({{X0+static_cast<int>(i%p.cols)*(w+GAP),p.top+static_cast<int>(i/p.cols)*(h+GAP),w,h},art,0,style});
    const int visible=std::max(1,(FOOT-24-p.top+GAP)/(h+GAP)),row=static_cast<int>(selected)/p.cols;
    p.scroll_y=static_cast<float>((row>=visible?row-visible+1:0)*(h+GAP));
    p.clip={0,p.top-12,1280,FOOT-p.top+12};return p;
}
const char* label(const Entry& e){switch(e.kind){
    case EntryKind::Channel:return "EN VIVO";case EntryKind::Asset:return e.program_type=="0"?"SERIE":"PELÍCULA";
    case EntryKind::Episode:return "EPISODIO";case EntryKind::VodPage:case EntryKind::SearchPage:return "MÁS RESULTADOS";
    case EntryKind::Category:case EntryKind::VodCategory:return "CATEGORÍA";default:return "EXPLORAR";}}
SDL_Color tint(const Entry& e){return e.kind==EntryKind::Category||e.kind==EntryKind::VodCategory?SDL_Color{46,46,46,255}:slate;}
const char* glyph(const Entry& e){switch(e.kind){
    case EntryKind::Channel:case EntryKind::LiveSection:return "◉";
    case EntryKind::Asset:case EntryKind::Section:return e.program_type=="0"?"☰":"▶";
    case EntryKind::Episode:return "▶";case EntryKind::Resume:return "↻";case EntryKind::Favorites:return "★";
    case EntryKind::Donation:return "♥";case EntryKind::VodPage:case EntryKind::SearchPage:return "→";default:return "❯";}}
std::string clock_text(double seconds){const int n=std::max(0,static_cast<int>(seconds));char b[24];
    if(n>=3600)std::snprintf(b,sizeof(b),"%d:%02d:%02d",n/3600,n/60%60,n%60);else std::snprintf(b,sizeof(b),"%d:%02d",n/60,n%60);return b;}
float ease(float t){t=std::clamp(t,0.0f,1.0f);const float u=1-t;return 1-u*u*u;}       // ease-out cubica
Uint32 next_cp(const std::string& s,size_t& i){
    const unsigned char c=static_cast<unsigned char>(s[i++]);Uint32 cp=c;int n=0;
    if(c>=0xf0){cp=c&7;n=3;}else if(c>=0xe0){cp=c&15;n=2;}else if(c>=0xc0){cp=c&31;n=1;}
    while(n-->0&&i<s.size()&&(static_cast<unsigned char>(s[i])&0xc0)==0x80)cp=cp<<6|(static_cast<unsigned char>(s[i++])&63);
    return cp;
}
bool has(TTF_Font* f,Uint32 cp){return cp<=0x20||TTF_GlyphIsProvided32(f,cp);}
bool covered(TTF_Font* f,const std::string& s){for(size_t i=0;i<s.size();)if(!has(f,next_cp(s,i)))return false;return true;}
// Una linea en la que los simbolos que no trae la fuente principal salen de la de respaldo.
SDL_Surface* render_mixed(TTF_Font* main,TTF_Font* alt,const std::string& s,SDL_Color color){
    struct Run{SDL_Surface* surface;int y;};std::vector<Run> runs;int w=0,h=0;const int ascent=std::max(TTF_FontAscent(main),TTF_FontAscent(alt));
    for(size_t i=0;i<s.size();){
        size_t e=i;const bool in_main=has(main,next_cp(s,e));
        while(e<s.size()){size_t k=e;const Uint32 cp=next_cp(s,k);if(cp>0x20&&has(main,cp)!=in_main)break;e=k;}
        TTF_Font* f=in_main?main:alt;
        if(SDL_Surface* part=TTF_RenderUTF8_Blended(f,s.substr(i,e-i).c_str(),color)){const int y=ascent-TTF_FontAscent(f);runs.push_back({part,y});w+=part->w;h=std::max(h,y+part->h);}
        i=e;
    }
    SDL_Surface* out=runs.empty()?nullptr:SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_ARGB8888);int x=0;
    if(out)SDL_FillRect(out,nullptr,0);
    for(auto& run:runs){if(out){SDL_SetSurfaceBlendMode(run.surface,SDL_BLENDMODE_NONE);SDL_Rect d{x,run.y,run.surface->w,run.surface->h};SDL_BlitSurface(run.surface,nullptr,out,&d);x+=run.surface->w;}SDL_FreeSurface(run.surface);}
    return out;
}
}
// ---------------------------------------------------------------- texto en cache
const TextCache::Item& TextCache::get(SDL_Renderer* renderer,TTF_Font* font,const std::string& value,SDL_Color color,int width,bool wrap){
    if(!renderer||!font||value.empty()||width<=0)return empty;
    char head[64];std::snprintf(head,sizeof(head),"%p|%02x%02x%02x|%d|%d|",static_cast<void*>(font),color.r,color.g,color.b,width,wrap?1:0);
    const std::string key=head+value;
    auto found=items.find(key);if(found!=items.end()){found->second.used=frame;return found->second;}
    SDL_Surface* s=nullptr;const SDL_Color opaque{color.r,color.g,color.b,255};
    TTF_Font* alt=nullptr;{const auto f=fallback.find(font);if(f!=fallback.end())alt=f->second;}
    if(wrap)s=TTF_RenderUTF8_Blended_Wrapped(alt&&!covered(font,value)?alt:font,value.c_str(),opaque,width);
    else{
        std::string one=value;const auto newline=one.find('\n');if(newline!=std::string::npos)one.resize(newline);
        int w=0,h=0;
        if(TTF_SizeUTF8(font,one.c_str(),&w,&h)==0&&w>width){
            while(!one.empty()){size_t cut=one.size()-1;while(cut>0&&(static_cast<unsigned char>(one[cut])&0xc0)==0x80)--cut;one.resize(cut);
                const std::string trimmed=one+"…";if(TTF_SizeUTF8(font,trimmed.c_str(),&w,&h)!=0||w<=width){one=trimmed;break;}}
        }
        if(!one.empty())s=alt&&!covered(font,one)?render_mixed(font,alt,one,opaque):TTF_RenderUTF8_Blended(font,one.c_str(),opaque);
    }
    Item item;item.used=frame;
    if(s){item.texture=SDL_CreateTextureFromSurface(renderer,s);item.w=s->w;item.h=s->h;SDL_FreeSurface(s);
        if(item.texture)SDL_SetTextureBlendMode(item.texture,SDL_BLENDMODE_BLEND);}
    return items.emplace(key,item).first->second;
}
void TextCache::tick(){
    ++frame;if(items.size()<=320)return;
    for(auto i=items.begin();i!=items.end();){if(frame-i->second.used>240){if(i->second.texture)SDL_DestroyTexture(i->second.texture);i=items.erase(i);}else ++i;}
}
void TextCache::clear(){for(auto& i:items)if(i.second.texture)SDL_DestroyTexture(i.second.texture);items.clear();}
// ---------------------------------------------------------------- base
bool View::open(std::string& error){
    trace("view:init");if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS)!=0){error=SDL_GetError();return false;}if(TTF_Init()!=0){error=TTF_GetError();return false;}
    window=SDL_CreateWindow("XuperTV",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,1280,720,SDL_WINDOW_RESIZABLE);
    if(!window){error=SDL_GetError();return false;}renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);if(!renderer)renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
    if(!renderer){error=SDL_GetError();return false;}SDL_RenderSetLogicalSize(renderer,1280,720);SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"1");
#ifdef __SWITCH__
    const char* path="sdmc:/switch/xuperswitch/font.ttf";
#else
    const char* path="font.ttf";
#endif
    // Inter para el texto e Inter Display (negrita) para titulos. font.ttf queda solo como
    // respaldo de simbolos; si falta en la SD, la interfaz funciona igual.
    struct Face{TTF_Font** slot;const unsigned char *begin,*end;int size;};
    const Face faces[]={{&font,xs_font_regular,xs_font_regular_end,21},{&small,xs_font_regular,xs_font_regular_end,16},{&chip,xs_font_semibold,xs_font_semibold_end,14},
        {&heading,xs_font_display,xs_font_display_end,28},{&title,xs_font_display,xs_font_display_end,40},{&large,xs_font_display,xs_font_display_end,46},{&wordmark,xs_font_display,xs_font_display_end,25}};
    for(const Face& face:faces){
        SDL_RWops* data=SDL_RWFromConstMem(face.begin,static_cast<int>(face.end-face.begin));*face.slot=data?TTF_OpenFontRW(data,1,face.size):nullptr;
        if(!*face.slot){error=TTF_GetError();return false;}
        TTF_SetFontHinting(*face.slot,TTF_HINTING_LIGHT);
        if(TTF_Font* symbols=TTF_OpenFont(path,face.size))texts.fallback[*face.slot]=symbols;
    }
    auto embedded=[&](const unsigned char* pixels,int n){SDL_Texture* t=nullptr;
        if(SDL_Surface* s=SDL_CreateRGBSurfaceWithFormatFrom(const_cast<unsigned char*>(pixels),n,n,32,n*4,SDL_PIXELFORMAT_RGBA32)){t=SDL_CreateTextureFromSurface(renderer,s);SDL_FreeSurface(s);}
        if(t)SDL_SetTextureBlendMode(t,SDL_BLENDMODE_BLEND);return t;};
    logo_big=embedded(xs_logo,144);logo_small=embedded(xs_logo_small,64);
    images=std::make_unique<Artwork>(renderer);trace("view:ready");return true;
}
bool View::approach(float& value,float target,float rate){
    const float delta=target-value;if(std::fabs(delta)<0.5f){value=target;return false;}
    value+=delta*std::min(1.0f,dt*rate);return true;
}
bool View::advance(float& t,float seconds){if(t>=1)return false;t=std::min(1.0f,t+dt/seconds);return t<1;}
void View::rect(SDL_Rect r,SDL_Color c){
    const Uint8 a=static_cast<Uint8>(c.a*alpha);
    SDL_SetRenderDrawBlendMode(renderer,a<255?SDL_BLENDMODE_BLEND:SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer,c.r,c.g,c.b,a);SDL_RenderFillRect(renderer,&r);
}
void View::gradient(SDL_Rect r,SDL_Color a,SDL_Color b){
    const int step=2;       // franjas de 2 px: la mitad de rectangulos, sin diferencia visible
    for(int i=0;i<r.h;i+=step){const float t=r.h>1?static_cast<float>(i)/(r.h-1):0;
        rect({r.x,r.y+i,r.w,std::min(step,r.h-i)},{static_cast<Uint8>(a.r+(b.r-a.r)*t),static_cast<Uint8>(a.g+(b.g-a.g)*t),static_cast<Uint8>(a.b+(b.b-a.b)*t),static_cast<Uint8>(a.a+(b.a-a.a)*t)});}
}
void View::fade_x(SDL_Rect r,SDL_Color c,bool to_right){
    for(int i=0;i<r.w;i+=4){const float t=static_cast<float>(i)/r.w;const float a=to_right?1-t:t;
        rect({r.x+i,r.y,std::min(4,r.w-i),r.h},{c.r,c.g,c.b,static_cast<Uint8>(c.a*a*a)});}
}
void View::fade_y(SDL_Rect r,SDL_Color c,bool to_bottom){
    for(int i=0;i<r.h;i+=2){const float t=static_cast<float>(i)/r.h;const float a=to_bottom?1-t:t;
        rect({r.x,r.y+i,r.w,std::min(2,r.h-i)},{c.r,c.g,c.b,static_cast<Uint8>(c.a*a)});}
}
// Redondea un rectangulo ya pintado tapando sus esquinas con el color de fondo.
void View::corners(SDL_Rect r,int radius,SDL_Color c){
    for(int i=0;i<radius;++i){const double dy=radius-i-0.5;const int inset=radius-static_cast<int>(std::sqrt(radius*radius-dy*dy)+0.5);
        if(inset<=0)continue;
        rect({r.x,r.y+i,inset,1},c);rect({r.x+r.w-inset,r.y+i,inset,1},c);
        rect({r.x,r.y+r.h-1-i,inset,1},c);rect({r.x+r.w-inset,r.y+r.h-1-i,inset,1},c);}
}
// Pastilla de esquinas redondeadas reales: sirve tambien sobre imagenes.
void View::pill(SDL_Rect r,SDL_Color color,SDL_Color){
    const int radius=std::min(std::min(r.w,r.h)/2,12);
    for(int i=0;i<radius;++i){const double dy=radius-i-0.5;const int inset=radius-static_cast<int>(std::sqrt(radius*radius-dy*dy)+0.5);
        rect({r.x+inset,r.y+i,r.w-2*inset,1},color);rect({r.x+inset,r.y+r.h-1-i,r.w-2*inset,1},color);}
    rect({r.x,r.y+radius,r.w,r.h-2*radius},color);
}
void View::text(int x,int y,const std::string& value,SDL_Color color,int width,TTF_Font* type,int max_height){
    const auto& item=texts.get(renderer,type?type:font,value,color,width,true);if(!item.texture)return;
    const int h=max_height>0?std::min(item.h,max_height):item.h;SDL_Rect src{0,0,item.w,h},dst{x,y,item.w,h};
    SDL_SetTextureAlphaMod(item.texture,static_cast<Uint8>(color.a*alpha));SDL_RenderCopy(renderer,item.texture,&src,&dst);
}
int View::line(int x,int y,const std::string& value,SDL_Color color,int width,TTF_Font* type,int align){
    const auto& item=texts.get(renderer,type?type:font,value,color,width,false);if(!item.texture)return 0;
    SDL_Rect dst{x+(align==1?(width-item.w)/2:align==2?width-item.w:0),y,item.w,item.h};
    SDL_SetTextureAlphaMod(item.texture,static_cast<Uint8>(color.a*alpha));SDL_RenderCopy(renderer,item.texture,nullptr,&dst);return item.w;
}
// Dibuja una imagen del servicio; aparece con un fundido la primera vez que esta lista.
bool View::image(const std::string& url,const std::string& key,SDL_Rect b,bool cover){
    auto* t=images?images->get(url,key):nullptr;if(!t||b.w<=0||b.h<=0)return false;int w=0,h=0;SDL_QueryTexture(t,nullptr,nullptr,&w,&h);if(!w||!h)return false;
    const Uint32 now=SDL_GetTicks();auto first=seen.find(key);if(first==seen.end())first=seen.emplace(key,now).first;
    const float shown=std::min(1.0f,(now-first->second)/220.0f);if(shown<1)busy=true;
    SDL_SetTextureBlendMode(t,SDL_BLENDMODE_BLEND);SDL_SetTextureAlphaMod(t,static_cast<Uint8>(255*shown*alpha));
    if(cover){const double scale=std::max(static_cast<double>(b.w)/w,static_cast<double>(b.h)/h);
        const int sw=std::min(w,std::max(1,static_cast<int>(b.w/scale))),sh=std::min(h,std::max(1,static_cast<int>(b.h/scale)));
        SDL_Rect src{(w-sw)/2,(h-sh)/4,sw,sh};SDL_RenderCopy(renderer,t,&src,&b);
    }else{const double scale=std::min(static_cast<double>(b.w)/w,static_cast<double>(b.h)/h);
        SDL_Rect dst{b.x+(b.w-static_cast<int>(w*scale))/2,b.y+(b.h-static_cast<int>(h*scale))/2,static_cast<int>(w*scale),static_cast<int>(h*scale)};SDL_RenderCopy(renderer,t,nullptr,&dst);}
    return true;
}
void View::brand(int x,int y,int size,TTF_Font* type){
    if(SDL_Texture* t=size>40?logo_big:logo_small){SDL_Rect dst{x,y,size,size};SDL_SetTextureAlphaMod(t,static_cast<Uint8>(255*alpha));SDL_RenderCopy(renderer,t,nullptr,&dst);}
    const int tx=x+size+size/4,ty=y+(size-TTF_FontHeight(type))/2;
    const int w=line(tx,ty,"Xuper",white,400,type);line(tx+w,ty,"TV",accent,200,type);
}
void View::splash(const std::string& status){
    alpha=1;rect({0,0,1280,720},bg);brand(430,232,72,large);
    line(430,322,"Tu entretenimiento, en un solo lugar.",muted,560,font);
    rect({430,376,420,3},surface2);const float phase=(SDL_GetTicks()%1400)/1400.0f;const int p=static_cast<int>(300*(0.5f-0.5f*std::cos(phase*6.2831853f)));rect({430+p,376,120,3},accent);
    line(430,398,status,white,560,font);
    line(0,640,"Apoyar el desarrollo es voluntario. Disponible en Inicio.",dim,1280,small,1);
    line(0,676,std::string("Versión ")+version+"  ·  Basado en XuperSwitch de Jona Ruacho",dim,1280,chip,1);texts.tick();SDL_RenderPresent(renderer);
}
// ---------------------------------------------------------------- navegacion
int View::columns(const Catalog& c)const{return make_plan(c,0).cols;}
int View::page_size(const Catalog& c)const{const Plan p=make_plan(c,0);return p.mode==Grid?p.cols*2:p.mode==List?5:1;}
bool View::at_top(const Catalog& c,size_t selected)const{
    if(c.entries.empty())return true;
    const Plan p=make_plan(c,selected);selected=std::min(selected,c.entries.size()-1);
    return p.mode==Rows?p.cells[selected].row==0:selected<static_cast<size_t>(p.cols);
}
size_t View::move(const Catalog& c,size_t selected,int dx,int dy)const{
    const size_t n=c.entries.size();if(!n)return 0;
    selected=std::min(selected,n-1);const Plan p=make_plan(c,selected);
    if(p.mode==Rows){
        int row=p.cells[selected].row;const auto range=p.rows[static_cast<size_t>(row)];int j=static_cast<int>(selected-range.first);
        if(dx)return range.first+static_cast<size_t>(std::clamp(j+dx,0,static_cast<int>(range.second-range.first)-1));
        const int next=std::clamp(row+dy,0,static_cast<int>(p.rows.size())-1);if(next==row)return selected;
        // Conserva la posicion en pantalla: la fila destino empieza sin desplazar.
        const int slot=j-std::clamp(j-2,0,std::max(0,j));const auto target=p.rows[static_cast<size_t>(next)];
        return target.first+static_cast<size_t>(std::min(slot,static_cast<int>(target.second-target.first)-1));
    }
    if(dx<0&&selected>0)--selected;if(dx>0)selected=std::min(selected+1,n-1);
    const size_t cols=static_cast<size_t>(p.cols);
    if(dy<0&&selected>=cols)selected-=cols;if(dy>0)selected=std::min(selected+cols,n-1);
    return selected;
}
int View::hit(const Catalog& c,size_t selected,int x,int y)const{
    if(y<BAR){for(int i=0;i<6;++i)if(x>=nav_rect[i].x&&x<nav_rect[i].x+nav_rect[i].w)return -2-i;return -1;}
    if(c.entries.empty())return -1;
    selected=std::min(selected,c.entries.size()-1);
    if(!sectioned(c)&&c.entries[0].kind==EntryKind::Channel&&c.entries[selected].kind==EntryKind::Channel&&x>=favorite_area.x&&x<favorite_area.x+favorite_area.w&&y>=favorite_area.y&&y<favorite_area.y+favorite_area.h)return -8;
    const Plan p=make_plan(c,selected);
    if(x<p.clip.x||x>=p.clip.x+p.clip.w||y<p.clip.y||y>=FOOT)return -1;
    for(size_t i=0;i<p.cells.size();++i){const Cell& cell=p.cells[i];
        const int cx=cell.r.x-(p.mode==Rows?static_cast<int>(p.row_x[static_cast<size_t>(cell.row)]):0),cy=cell.r.y-static_cast<int>(p.scroll_y);
        if(cy>=FOOT)break;
        if(x>=cx&&x<cx+cell.r.w&&y>=cy&&y<cy+cell.r.h)return static_cast<int>(i);}
    return -1;
}
// ---------------------------------------------------------------- tarjetas
void View::card(const Entry& e,SDL_Rect r,int art_h,int style,float focus,int number){
    // El foco agranda la tarjeta desde su centro.
    const float grow=(style==Episode?0.02f:0.07f)*focus;const int dx=static_cast<int>(r.w*grow/2),dy=static_cast<int>(art_h*grow/2);
    SDL_Rect art{r.x-dx,r.y-dy,r.w+2*dx,art_h+2*dy};
    const SDL_Color caption{static_cast<Uint8>(muted.r+(white.r-muted.r)*focus),static_cast<Uint8>(muted.g+(white.g-muted.g)*focus),static_cast<Uint8>(muted.b+(white.b-muted.b)*focus),255};
    if(style==Episode){
        rect(art,{static_cast<Uint8>(surface.r+(surface2.r-surface.r)*focus),static_cast<Uint8>(surface.g+(surface2.g-surface.g)*focus),static_cast<Uint8>(surface.b+(surface2.b-surface.b)*focus),255});
        corners(art,8,bg);
        line(art.x+8,art.y+(art.h-36)/2,std::to_string(number),focus>0.5f?white:dim,56,heading,1);
        SDL_Rect thumb{art.x+72,art.y+(art.h-72)/2,128,72};rect(thumb,ink);
        if(e.artwork_cache.empty()||!image(e.artwork,e.artwork_cache,thumb,true)){gradient(thumb,slate,coal);line(thumb.x,thumb.y+20,"▶",{255,255,255,190},thumb.w,font,1);}
        const double watched=resume?resume(e):0;
        if(watched>0)rect({thumb.x,thumb.y+thumb.h-4,thumb.w,4},accent);   // empezado (no se conoce la duracion)
        line(art.x+218,art.y+18,e.title,caption,art.w-300,font);
        if(watched>0)line(art.x+218,art.y+52,"Continuar desde "+clock_text(watched),accent,art.w-300,chip);
        else line(art.x+218,art.y+52,e.description.empty()?"Episodio "+std::to_string(number):e.description,dim,art.w-300,small);
        if(focus>0.02f){SDL_Rect button{art.x+art.w-62,art.y+(art.h-40)/2,40,40};pill(button,{255,255,255,static_cast<Uint8>(255*focus)},surface2);line(button.x+2,button.y+6,"▶",{ink.r,ink.g,ink.b,static_cast<Uint8>(255*focus)},40,font,1);}
        return;
    }
    rect(art,surface);bool drawn=false;
    if(!e.artwork_cache.empty())drawn=style==Logo?image(e.artwork,e.artwork_cache,{art.x+18,art.y+12,art.w-36,art.h-24},false):image(e.artwork,e.artwork_cache,art,true);
    if(!drawn&&style!=Logo){gradient(art,tint(e),coal);if(style==Tile)rect({art.x,art.y,4,art.h},accent);}
    if(!drawn){
        if(style==Poster||style==Logo)line(art.x,art.y+(art.h-38)/2,glyph(e),{255,255,255,170},art.w,heading,1);
        else if(style==Label){line(art.x+18,art.y+16,e.title,white,art.w-36,font);line(art.x+18,art.y+art.h-30,label(e),{255,255,255,140},art.w-36,chip);}
        else if(e.kind!=EntryKind::Episode){line(art.x+20,art.y+10,glyph(e),accent,80,large);line(art.x+20,art.y+art.h-44,e.title,white,art.w-36,font);}
        else line(art.x,art.y+(art.h-38)/2,"▶",{255,255,255,170},art.w,heading,1);
    }
    if(style==Wide&&e.kind==EntryKind::Episode){ // progreso de "Seguir viendo"
        fade_y({art.x,art.y+art.h-46,art.w,46},{0,0,0,190},false);
        rect({art.x,art.y+art.h-4,art.w,4},accent);
        if(focus>0.02f){SDL_Rect button{art.x+art.w/2-22,art.y+art.h/2-22,44,44};pill(button,{255,255,255,static_cast<Uint8>(235*focus)},ink);line(button.x+2,button.y+8,"▶",{ink.r,ink.g,ink.b,static_cast<Uint8>(255*focus)},44,font,1);}
    }
    corners(art,7,bg);
    const int below=art.y+art.h+9;
    if(style==Poster||style==Logo)line(r.x+2,below,e.title,caption,r.w-4,small);
    else if(style==Wide&&(e.kind==EntryKind::Episode||e.kind==EntryKind::Asset)){
        line(r.x+2,below,e.title,caption,r.w-4,small);
        const double watched=resume&&e.kind==EntryKind::Episode?resume(e):0;
        if(watched>0)line(r.x+2,below+23,(e.description.empty()||e.row.empty()?"Continuar":e.description)+" · "+clock_text(watched),accent,r.w-4,chip);
    }
    if(focus>0.5f&&favorite&&e.kind==EntryKind::Channel){rect({art.x+art.w-34,art.y+6,28,26},accent);line(art.x+art.w-34,art.y+8,"★",white,28,small,1);}
}
// ---------------------------------------------------------------- cabecera
void View::hero(const Catalog& c,const Entry* e,size_t selected,int mode,int height,bool full_memory){
    const float shown=ease(hero_t);const int slide=static_cast<int>((1-shown)*18);const float base=alpha;
    const std::string count=e?std::to_string(selected+1)+" de "+std::to_string(c.entries.size()):"";
    if(mode==List){
        // Ficha de la serie o pelicula a la izquierda; la lista de episodios va a la derecha.
        SDL_Rect poster{X0,92,176,264};rect(poster,surface);
        if(c.artwork_cache.empty()||!image(c.artwork,c.artwork_cache,poster,true)){gradient(poster,slate,coal);line(poster.x,poster.y+110,"▶",{255,255,255,200},poster.w,heading,1);}
        corners(poster,8,bg);
        line(X0+196,96,"LISTO PARA VER",accent,200,chip);
        text(X0+196,120,c.title.empty()?"Episodios":c.title,white,196,heading,150);
        line(X0+196,292,std::to_string(c.entries.size())+(c.entries.size()==1?" episodio":" episodios"),muted,196,small);
        line(X0+196,318,count,dim,196,small);
        text(X0,376,c.description,muted,380,small,FOOT-376-20);
        if(!full_memory)line(X0,FOOT-22,"Modo álbum: abre con R sobre un juego.",gold,380,chip);
        return;
    }
    const bool rows=mode==Rows;
    const bool item=e&&(rows?(e->kind==EntryKind::Episode||e->kind==EntryKind::Channel||e->kind==EntryKind::Asset):(c.entries[0].kind==EntryKind::Asset||c.entries[0].kind==EntryKind::Channel));
    const bool logo=item&&e->kind==EntryKind::Channel;
    alpha=base*(0.25f+0.75f*shown);
    if(item&&!e->artwork_cache.empty()){
        if(logo){SDL_Rect plate{968,84,264,height-106};rect(plate,surface);image(e->artwork,e->artwork_cache,{plate.x+26,plate.y+14,plate.w-52,plate.h-28},false);corners(plate,10,bg);}
        else{SDL_Rect area{560,0,720,height};
            if(image(e->artwork,e->artwork_cache,area,true)){alpha=base;
                fade_x({560,0,460,height},bg,true);fade_y({560,height-120,720,120},bg,false);rect(area,{bg.r,bg.g,bg.b,60});}}
    }
    alpha=base;fade_y({0,0,1280,110},{0,0,0,210},true);
    alpha=base*shown;const int tx=X0+slide,tw=620;int y=84;
    if(item){line(tx,y,rows?e->row:c.title,rows?accent:muted,tw,rows?chip:small);y+=26;}
    else if(rows){y=96;}
    const std::string name=item||rows?(e?e->title:"Inicio"):(c.title.empty()?"Explorar":c.title);int name_w=0,name_h=0;
    // Los titulos largos bajan un tamano antes de recortarse.
    if(TTF_SizeUTF8(title,name.c_str(),&name_w,&name_h)==0&&name_w>tw)line(tx,y+8,name,white,tw,heading);else line(tx,y,name,white,tw,title);
    y+=62;
    if(item){
        const double watched=resume&&e->kind==EntryKind::Episode?resume(*e):0;
        const bool grouped=rows&&e->kind==EntryKind::Episode&&!e->series_id.empty();     // serie en "Seguir viendo"
        const bool series=grouped||(e->kind==EntryKind::Asset&&e->program_type=="0");
        // Cabecera baja (inicio): el boton va en la misma linea que los datos.
        const bool compact=height>=230&&height<300;SDL_Rect button{tx,y-2,series?196:168,40};
        const int mx=compact?button.x+button.w+20:tx,my=compact?y+9:y;
        const int w=line(mx,my,grouped?"SERIE":label(*e),accent,tw,chip);
        std::string extra="·   "+(grouped&&!e->description.empty()?e->description:count);if(watched>0)extra+="   ·   Continuar desde "+clock_text(watched);
        const int w2=line(mx+w+14,my-2,extra,muted,360,small);
        if(logo&&!rows)line(mx+w+w2+34,my-2,favorite?"★  En tus favoritos  ·  R3 para quitar":"☆  R3: guardar en favoritos",favorite?accent:dim,400,small);
        y+=30;
        if(!e->description.empty()&&height>=300){text(tx,y,e->description,muted,600,font,56);y+=64;}
        if(height>=230){if(!compact)button.y=std::min(y+4,height-52);pill(button,white,bg);
            line(button.x,button.y+6,series?"▶   Ver episodios":logo?"▶   Ver en vivo":watched>0?"▶   Continuar":"▶   Reproducir",ink,button.w,font,1);
            if(!compact)line(button.x+button.w+16,button.y+11,"Pulsa A",dim,200,small);}
    }else if(rows)line(tx,y,"Tu entretenimiento, en un solo lugar.",muted,tw,font);
    else if(e)line(tx,y,std::to_string(c.entries.size())+" opciones   ·   "+count,muted,tw,small);
    alpha=base;
    if(!full_memory)line(X0,BAR+2,"Modo álbum: abre hbmenu con R sobre un juego para reproducir video.",gold,CW,chip,2);
}
// ---------------------------------------------------------------- barra superior
void View::topbar(){
    brand(X0,16,32,wordmark);
    int x=250,target_x=0,target_w=0;
    for(int i=0;i<6;++i){
        const bool active=active_nav==i,focus=nav_focus==i;
        const auto& item=texts.get(renderer,active?chip:small,nav[i],white,300,false);const int w=item.w;
        if(i==5)x=std::max(x,1280-X0-w-150);     // "Apoyar" se separa hacia la derecha
        nav_rect[i]={x-14,12,w+28,40};
        if(focus){target_x=nav_rect[i].x;target_w=nav_rect[i].w;}
        x+=w+34;
    }
    if(nav_focus>=0){if(nav_alpha<=0.01f){nav_x=static_cast<float>(target_x);nav_w=static_cast<float>(target_w);}
        busy|=approach(nav_x,static_cast<float>(target_x),18);busy|=approach(nav_w,static_cast<float>(target_w),18);}
    {const float target=nav_focus>=0?1.0f:0.0f;const float delta=target-nav_alpha;if(std::fabs(delta)>0.01f){nav_alpha+=delta*std::min(1.0f,dt*14);busy=true;}else nav_alpha=target;}
    if(nav_alpha>0.01f){SDL_Rect focus_pill{static_cast<int>(nav_x),14,static_cast<int>(nav_w),36};pill(focus_pill,{255,255,255,static_cast<Uint8>(255*nav_alpha)},ink);}
    for(int i=0;i<6;++i){const bool active=active_nav==i,focus=nav_focus==i;
        line(nav_rect[i].x+14,active?24:22,nav[i],focus?ink:active?white:muted,300,active?chip:small);
        if(active&&!focus)rect({nav_rect[i].x+14,48,nav_rect[i].w-28,2},accent);}
    line(1280-X0-130,24,"X   Buscar",dim,130,chip,2);
}
// ---------------------------------------------------------------- pantalla principal
void View::draw(const Catalog& c,size_t selected,const std::string& status,bool full_memory){
    poll_images();
    const Uint32 now=SDL_GetTicks();dt=last_tick?std::min(0.05f,(now-last_tick)/1000.0f):0.016f;last_tick=now;busy=false;
    const Plan p=make_plan(c,selected);const size_t n=c.entries.size();if(n)selected=std::min(selected,n-1);
    // Escena nueva: entra con un fundido y el foco aparece directamente en su sitio.
    const std::string id=c.title+"|"+std::to_string(n)+"|"+std::to_string(p.mode);bool snap=false;
    if(id!=scene){scene=id;enter_t=0;hero_t=0;focus_t=1;last_selected=previous=selected;scroll_y=p.scroll_y;row_x=p.row_x;snap=true;}
    if(selected!=last_selected){previous=last_selected;last_selected=selected;focus_t=0;hero_t=0.35f;}
    if(row_x.size()!=p.row_x.size())row_x=p.row_x;
    busy|=advance(enter_t,0.32f);busy|=advance(focus_t,0.20f);busy|=advance(hero_t,0.28f);
    busy|=approach(scroll_y,p.scroll_y,13);
    for(size_t r=0;r<row_x.size();++r)busy|=approach(row_x[r],p.row_x[r],13);
    const float entered=ease(enter_t);const int rise=static_cast<int>((1-entered)*22);
    alpha=1;rect({0,0,1280,720},bg);
    const Entry* current=n?&c.entries[selected]:nullptr;
    alpha=entered;hero(c,current,selected,p.mode,p.hero,full_memory);
    // Contenido: solo se dibuja lo visible; la tarjeta enfocada va al final, por encima.
    SDL_RenderSetClipRect(renderer,&p.clip);alpha=entered;
    auto place=[&](size_t i){const Cell& cell=p.cells[i];SDL_Rect r=cell.r;
        r.x-=p.mode==Rows?static_cast<int>(row_x[static_cast<size_t>(cell.row)]):0;r.y-=static_cast<int>(scroll_y)-rise;return r;};
    if(p.mode==Rows)for(size_t r=0;r<p.rows.size();++r){const int y=p.top+static_cast<int>(r)*ROW_H-static_cast<int>(scroll_y)+rise;
        if(y>-40&&y<FOOT)line(X0,y+4,c.entries[p.rows[r].first].row,current&&p.cells[selected].row==static_cast<int>(r)?white:muted,CW,font);}
    const float focused=nav_focus<0?ease(focus_t):0;
    for(size_t i=0;i<n;++i){if(i==selected)continue;const SDL_Rect r=place(i);
        if(r.y>=FOOT+20){if(p.mode==Rows)continue;
            // Adelanta las portadas de las dos filas siguientes para que ya esten al bajar.
            if(p.mode==Grid&&images)for(size_t k=i;k<n&&k<i+2*static_cast<size_t>(p.cols);++k)if(!c.entries[k].artwork_cache.empty())images->get(c.entries[k].artwork,c.entries[k].artwork_cache);
            break;}
        if(r.y+r.h<p.clip.y-20||r.x>1280||r.x+r.w<0)continue;
        card(c.entries[i],r,p.cells[i].art_h,p.cells[i].style,i==previous&&nav_focus<0?1-ease(focus_t):0,static_cast<int>(i)+1);}
    if(current){const SDL_Rect r=place(selected);const Cell& cell=p.cells[selected];
        card(*current,r,cell.art_h,cell.style,focused,static_cast<int>(selected)+1);
        // Marco de foco: se desliza de una tarjeta a la siguiente.
        const float grow=(cell.style==Episode?0.02f:0.07f)*focused;const float tw=r.w*(1+grow),th=cell.art_h*(1+grow);
        const float target[4]={r.x-(tw-r.w)/2-4,r.y-(th-cell.art_h)/2-4,tw+8,th+8};
        for(int k=0;k<4;++k){if(snap||ring_alpha<=0.01f)ring[k]=target[k];else busy|=approach(ring[k],target[k],20);}
    }
    {const float target=current&&nav_focus<0?1.0f:0.0f;const float delta=target-ring_alpha;if(std::fabs(delta)>0.01f){ring_alpha+=delta*std::min(1.0f,dt*14);busy=true;}else ring_alpha=target;}
    if(ring_alpha>0.01f){alpha=entered*ring_alpha;const int x=static_cast<int>(ring[0]),y=static_cast<int>(ring[1]),w=static_cast<int>(ring[2]),h=static_cast<int>(ring[3]);
        rect({x+3,y,w-6,3},white);rect({x+3,y+h-3,w-6,3},white);rect({x,y+3,3,h-6},white);rect({x+w-3,y+3,3,h-6},white);}
    alpha=1;
    if(p.mode==List)fade_y({p.clip.x,p.clip.y,p.clip.w,16},bg,true);
    fade_y({p.clip.x,FOOT-44,p.clip.w,44},bg,false);
    SDL_RenderSetClipRect(renderer,nullptr);
    if(!current)text(X0,p.top+10,c.title=="Favoritos"?"Tus canales favoritos aparecerán aquí.\nSelecciona un canal y pulsa el stick derecho (R3) para guardarlo.":"No hay contenido para mostrar.",muted,850,font,150);
    rect({0,FOOT,1280,720-FOOT},bg);
    const int hints=line(X0,698,"A Abrir   ·   B Volver   ·   X Buscar   ·   Y Inicio   ·   ZR Actualizar   ·   + Salir",dim,CW,chip,2);
    line(X0,696,status,muted,CW-hints-40,small);
    topbar();texts.tick();
    if(seen.size()>400)seen.clear();
    SDL_RenderPresent(renderer);
}
void View::donation(){
    alpha=1;rect({0,0,1280,720},bg);brand(76,52,40,heading);text(76,170,"Apoya el desarrollo",white,720,large);text(76,252,"Tu aportacion ayuda a seguir mejorando\nXuperSwitch by Jona Ruacho.",muted,680,heading,100);
    text(76,382,"Escanea el QR con tu celular y revisa\nel destinatario y el importe en Mercado Pago.",white,680,font,80);
    text(76,492,"Aportacion voluntaria. Puedes usar la app sin donar.",accent,680,font,70);
    text(76,590,"link.mercadopago.com.mx/\nxuperbyjonaruacho",muted,680,font,70);
    rect({842,206,340,340},{255,255,255,255});
    for(int y=0;y<33;++y)for(int x=0;x<33;++x)if(donation_qr[y][x]=='1')rect({848+(x+4)*8,212+(y+4)*8,8,8},{0,0,0,255});
    text(900,565,"MERCADO PAGO",accent,300,small);text(76,680,"B / A / Y  Continuar a la app",white,900,small);texts.tick();SDL_RenderPresent(renderer);
}
void View::close(){
    texts.clear();images.reset();for(auto& pair:texts.fallback)TTF_CloseFont(pair.second);texts.fallback.clear();
    for(auto** t:{&logo_big,&logo_small})if(*t){SDL_DestroyTexture(*t);*t=nullptr;}
    for(auto** f:{&font,&small,&large,&heading,&title,&chip,&wordmark})if(*f){TTF_CloseFont(*f);*f=nullptr;}
    if(renderer){SDL_DestroyRenderer(renderer);renderer=nullptr;}if(window){SDL_DestroyWindow(window);window=nullptr;}if(TTF_WasInit())TTF_Quit();if(SDL_WasInit(SDL_INIT_VIDEO))SDL_Quit();
}
}
