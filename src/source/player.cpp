#include "player.hpp"
#include "catalog.hpp"
#include "trace.hpp"
#include "playback_error.hpp"
#include "subtitle_text.hpp"
#include "view.hpp"
#include <mpv/client.h>
#include <mpv/render.h>
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <cstdio>
#ifdef __SWITCH__
#include <switch.h>
#endif
namespace xs {
static void update(void* p){static_cast<std::atomic<bool>*>(p)->store(true);}
// Preferencias que se conservan entre videos mientras la app esta abierta.
static bool hardware_decode=false;          // decodificacion por hardware (experimental)
static std::string preferred_subtitle;      // idioma elegido; vacio = sin subtitulos
std::string play(const std::string& source,SDL_Renderer* renderer,const std::vector<std::pair<std::string,std::string>>& headers,const PlaybackOptions& options,PlaybackOutcome* outcome) {
    if(!renderer||!valid_source(source))return "Origen o pantalla no admitidos.";
    trace("play:begin");
    mpv_handle* mpv=mpv_create();mpv_render_context* context=nullptr;SDL_Texture* texture=nullptr;
    void* pixels=nullptr;std::atomic<bool> dirty{true};std::string error;bool subtitle_written=false;
#ifdef __SWITCH__
    const std::string subtitle_base="sdmc:/switch/xuperswitch/subtitle_cache";
#else
    const std::string subtitle_base="subtitle_cache";
#endif
    std::string subtitle_cache=subtitle_base+".srt";
    auto cleanup=[&]{
        trace("play:free_context_begin");
        if(context)mpv_render_context_free(context);
        trace("play:free_context_end");
        if(mpv)mpv_terminate_destroy(mpv);
        trace("play:mpv_destroyed");
        if(texture)SDL_DestroyTexture(texture);
        std::free(pixels);
        if(subtitle_written)std::remove(subtitle_cache.c_str());
#ifdef __SWITCH__
        appletSetMediaPlaybackState(false);
#endif
        trace("play:return_to_menu");
    };
    if(!mpv){cleanup();return "No se pudo iniciar libmpv.";}
    auto option=[&](const char* name,const char* value){int rc=mpv_set_option_string(mpv,name,value);
        if(rc<0&&error.empty())error=std::string(name)+": "+mpv_error_string(rc);};
    option("config","no");option("load-scripts","no");option("terminal","no");
    option("vo","libmpv");option("hwdec","no");
    option("osd-level","0");option("sub-visibility","yes");
    option("sub-auto","no");option("audio-pitch-correction","yes");
    option("speed",std::to_string(options.speed).c_str());option("volume",std::to_string(options.volume).c_str());
    option("pause",options.paused?"yes":"no");
    option("network-timeout","15");option("tls-verify","yes");option("demuxer-max-bytes","33554432");
    // Opciones tolerantes: si esta variante de libmpv no conoce alguna, se ignora.
    auto soft=[&](const char* name,const char* value){mpv_set_option_string(mpv,name,value);};
    // Rendimiento: cuando el decodificador no alcanza el tiempo real, descarta cuadros
    // en el decodificador en vez de acumular retraso (el video se iba quedando atras
    // del audio y cada vez se mostraban menos cuadros hasta reabrir el contenido).
    soft("framedrop","decoder+vo");soft("vd-lavc-threads","3");soft("vd-lavc-fast","yes");soft("vd-lavc-skiploopfilter","all");
    soft("sws-fast","yes");soft("sws-scaler","fast-bilinear");soft("hwdec",hardware_decode?"auto-copy":"no");
    // Subtitulos: aparecen tras adelantar en MKV, sin huecos entre lineas, legibles a 360p.
    soft("demuxer-mkv-subtitle-preroll","yes");soft("sub-fix-timing","yes");soft("stretch-image-subs-to-screen","yes");
    soft("sub-border-size","3");soft("sub-shadow-offset","1");
    soft("volume-max","300");      // permite subir el volumen por encima del 100 %
#ifdef __SWITCH__
    option("ao","hos");option("sub-fonts-dir","sdmc:/switch/xuperswitch");option("sub-font","DejaVu Sans");const char* ca="sdmc:/switch/xuperswitch/cacert.pem";
    if(std::ifstream(ca).good())option("tls-ca-file",ca);
#elif defined(XUPERSWITCH_TEST)
    option("ao","null");
#endif
    if(headers.size()>8){cleanup();return "Demasiadas cabeceras de video.";}
    std::vector<std::string> header_lines;std::vector<mpv_node> header_nodes;
    for(const auto& header:headers){
        if((header.first!="Content-Auth"&&header.first!="Content-License"&&header.first!="App"&&header.first!="App-Version"&&header.first!="Ranger-Id"&&header.first!="Pragma")||header.second.empty()||header.second.size()>16384||
           std::any_of(header.second.begin(),header.second.end(),[](unsigned char c){return c<32||c==127;})){cleanup();return "Cabecera de video no admitida.";}
        header_lines.push_back(header.first+": "+header.second);
    }
    if(!header_lines.empty()){
        header_nodes.resize(header_lines.size());
        for(size_t i=0;i<header_lines.size();++i){header_nodes[i].format=MPV_FORMAT_STRING;header_nodes[i].u.string=header_lines[i].data();}
        mpv_node_list list{static_cast<int>(header_nodes.size()),header_nodes.data(),nullptr};
        mpv_node node{};node.format=MPV_FORMAT_NODE_ARRAY;node.u.list=&list;
        const int result=mpv_set_option(mpv,"http-header-fields",MPV_FORMAT_NODE,&node);
        if(result<0){cleanup();return "No se pudieron configurar las cabeceras de video.";}
    }
    if(!error.empty()){cleanup();return error;}
    int rc=mpv_initialize(mpv);
        if(rc<0){error=mpv_error_string(rc);cleanup();return error;}
    trace("play:mpv_initialized");
    mpv_request_log_messages(mpv,"v");
    mpv_render_param init[]={{MPV_RENDER_PARAM_API_TYPE,const_cast<char*>(MPV_RENDER_API_TYPE_SW)},{MPV_RENDER_PARAM_INVALID,nullptr}};
    rc=mpv_render_context_create(&context,mpv,init);
    if(rc<0){error=std::string("Render software: ")+mpv_error_string(rc);cleanup();return error;}
    mpv_render_context_set_update_callback(context,update,&dirty);
    constexpr int width=640,height=360;size_t stride=width*4;
    pixels=std::aligned_alloc(64,stride*height);
    texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ABGR8888,SDL_TEXTUREACCESS_STREAMING,width,height);
    if(!pixels||!texture){error="No se pudo crear el buffer de video.";cleanup();return error;}
    SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_NONE);
    const char* load[]={"loadfile",source.c_str(),nullptr};rc=mpv_command(mpv,load);
    if(rc<0){error=mpv_error_string(rc);cleanup();return error;}
    trace("play:file_requested");
#ifdef __SWITCH__
    appletSetMediaPlaybackState(true);
    PadState pad;padConfigureInput(1,HidNpadStyleSet_NpadStandard);padInitializeDefault(&pad);padUpdate(&pad);
#endif
    bool running=true,paused=false,first_frame=true,menu=false,loaded=false;
    int page=0,selection=0,aspect=0;double pending_start=options.start;
    int64_t external_id=-1,external_index=-1;
    const std::vector<double> speeds{0.5,0.75,1,1.25,1.5,1.75,2};
    const std::vector<std::string> ratios{"Original","16:9","4:3","Rellenar"};
    PlaybackOutcome result;bool auto_next=options.auto_next;std::string notice,media_hint;
    Uint32 visible_until=SDL_GetTicks()+5000,last_draw=0;
    auto property=[&](const char* name){double value=0;mpv_get_property(mpv,name,MPV_FORMAT_DOUBLE,&value);return value;};
    auto flag=[&](const char* name){int value=0;mpv_get_property(mpv,name,MPV_FORMAT_FLAG,&value);return value!=0;};
    auto set_number=[&](const char* name,double value){if(mpv_set_property(mpv,name,MPV_FORMAT_DOUBLE,&value)<0)notice="Esta opcion no esta disponible en este video.";};
    auto command=[&](const char* name,const std::string& value,const char* mode=nullptr){const char* args[]={name,value.c_str(),mode,nullptr};
        if(mpv_command(mpv,args)<0)notice="No se pudo aplicar esa opcion al video.";};
    auto show=[&]{visible_until=SDL_GetTicks()+4000;};
    auto seek_to=[&](double amount){if(!flag("seekable")){notice="Esta emision no permite adelantar o retroceder.";show();return;}
        command("seek",std::to_string(amount),"relative+exact");show();};
    auto toggle_pause=[&]{int value=!flag("pause");
        if(mpv_set_property(mpv,"pause",MPV_FORMAT_FLAG,&value)<0)notice="No se pudo pausar.";
        show();};
    struct Track {int64_t id;std::string type,label;bool selected;std::string lang;};
    auto tracks=[&]{std::vector<Track> rows;mpv_node node{};
        if(mpv_get_property(mpv,"track-list",MPV_FORMAT_NODE,&node)>=0){
            if(node.format==MPV_FORMAT_NODE_ARRAY)for(int i=0;i<node.u.list->num;++i){const auto& n=node.u.list->values[i];
        if(n.format!=MPV_FORMAT_NODE_MAP)continue;
                Track t{0,"","",false,""};std::string lang,title,codec;
                for(int j=0;j<n.u.list->num;++j){const std::string key=n.u.list->keys[j];const auto& v=n.u.list->values[j];
                    if(key=="id"&&v.format==MPV_FORMAT_INT64)t.id=v.u.int64;
                    if(key=="selected"&&v.format==MPV_FORMAT_FLAG)t.selected=v.u.flag;
                    if(v.format==MPV_FORMAT_STRING){const std::string value=v.u.string;
                        if(key=="type")t.type=value;
        if(key=="lang")lang=value;
        if(key=="title")title=value;
        if(key=="codec")codec=value;}
                }
                t.label=(lang.empty()?"Pista "+std::to_string(t.id):lang)+(title.empty()?"":" - "+title)+(codec.empty()?"":" ["+codec+"]");
                if(t.label.size()>120)t.label.resize(120);
                t.lang=lang;
                rows.push_back(std::move(t));
            }
            mpv_free_node_contents(&node);
        }return rows;};
    auto cycle_track=[&](const std::string& type){auto rows=tracks();std::vector<Track> choices;
        for(const auto& t:rows)if(t.type==type)choices.push_back(t);
        if(choices.empty()){notice="No hay pistas alternativas en este video.";show();return;}
        size_t selected=0;for(size_t i=0;i<choices.size();++i)if(choices[i].selected)selected=i;
        const auto& next=choices[(selected+1)%choices.size()];
        mpv_set_property_string(mpv,type=="audio"?"aid":"sid",std::to_string(next.id).c_str());
        notice=next.label;show();};
    auto cycle_speed=[&](int direction){double speed=property("speed");size_t i=0;for(size_t k=0;k<speeds.size();++k)if(std::abs(speeds[k]-speed)<0.01)i=k;
        set_number("speed",speeds[(i+speeds.size()+direction)%speeds.size()]);show();};
    auto cycle_aspect=[&]{aspect=(aspect+1)%4;set_number("video-aspect-override",aspect==1?16.0/9:aspect==2?4.0/3:-1);
        set_number("panscan",aspect==3?1:0);notice="Pantalla: "+ratios[aspect];show();};
    auto request_item=[&](int index){if(index>=0&&static_cast<size_t>(index)<options.items.size()){result.next_item=index;running=false;}};
    auto draw_text=[&](int x,int y,const std::string& message,SDL_Color color,int width=1150){if(!options.font||message.empty())return;
        SDL_Surface* surface=TTF_RenderUTF8_Blended_Wrapped(options.font,message.c_str(),color,width);
        if(!surface)return;
        SDL_Texture* label=SDL_CreateTextureFromSurface(renderer,surface);SDL_Rect dst{x,y,surface->w,surface->h};
        if(label){SDL_RenderCopy(renderer,label,nullptr,&dst);SDL_DestroyTexture(label);}SDL_FreeSurface(surface);};
    // Descarga un subtitulo del servicio, lo normaliza (UTF-8 + formato) y lo activa.
    auto load_service_sub=[&](int64_t index){
        if(index<0||static_cast<size_t>(index)>=options.subtitles.size())return false;
        int on=1;
        if(external_index==index&&external_id>=0){mpv_set_property_string(mpv,"sid",std::to_string(external_id).c_str());mpv_set_property(mpv,"sub-visibility",MPV_FORMAT_FLAG,&on);notice="Subtitulo seleccionado";return true;}
        SDL_SetRenderDrawColor(renderer,20,20,20,255);SDL_RenderClear(renderer);SDL_RenderCopy(renderer,texture,nullptr,nullptr);
        SDL_SetRenderDrawColor(renderer,20,20,20,255);SDL_Rect loading{20,550,1240,140};SDL_RenderFillRect(renderer,&loading);
        draw_text(48,580,"Cargando subtitulos...",{238,243,250,255});SDL_RenderPresent(renderer);
        std::string data,subtitle_error;
        if(!options.subtitle_loader||!options.subtitle_loader(static_cast<size_t>(index),data,subtitle_error)){
            notice=subtitle_error.empty()?"No se pudo descargar el subtitulo.":subtitle_error;return false;}
        const char* extension=normalize_subtitle(data);
        if(external_id>=0){command("sub-remove",std::to_string(external_id));external_id=-1;external_index=-1;}
        if(subtitle_written){std::remove(subtitle_cache.c_str());subtitle_written=false;}
        subtitle_cache=subtitle_base+extension;
        std::ofstream file(subtitle_cache,std::ios::binary|std::ios::trunc);file.write(data.data(),static_cast<std::streamsize>(data.size()));file.close();
        if(!file){notice="No se pudo guardar el subtitulo temporal.";return false;}
        subtitle_written=true;
        const char* args[]={"sub-add",subtitle_cache.c_str(),"select",options.subtitles[static_cast<size_t>(index)].c_str(),nullptr};
        if(mpv_command(mpv,args)<0){notice="Formato de subtitulos no compatible.";return false;}
        external_index=index;mpv_get_property(mpv,"sid",MPV_FORMAT_INT64,&external_id);mpv_set_property(mpv,"sub-visibility",MPV_FORMAT_FLAG,&on);
        preferred_subtitle=options.subtitles[static_cast<size_t>(index)];notice="Subtitulo cargado";return true;};
    auto select_embedded_sub=[&](const Track& t){int on=1;mpv_set_property_string(mpv,"sid",std::to_string(t.id).c_str());
        mpv_set_property(mpv,"sub-visibility",MPV_FORMAT_FLAG,&on);if(!t.lang.empty())preferred_subtitle=t.lang;};
    // Un solo boton recorre: desactivados -> pistas del video -> subtitulos del servicio.
    auto cycle_subs=[&]{struct Choice{int kind;int64_t value;std::string label;};std::vector<Choice> choices{{0,-1,"Subtitulos desactivados"}};
        const auto rows=tracks();int64_t sid=-1;
        const bool visible=mpv_get_property(mpv,"sid",MPV_FORMAT_INT64,&sid)>=0&&flag("sub-visibility");size_t current=0;
        for(size_t i=0;i<rows.size();++i)if(rows[i].type=="sub"&&rows[i].id!=external_id){if(visible&&rows[i].id==sid)current=choices.size();choices.push_back({1,static_cast<int64_t>(i),rows[i].label});}
        for(size_t i=0;i<options.subtitles.size();++i){if(visible&&external_id>=0&&sid==external_id&&external_index==static_cast<int64_t>(i))current=choices.size();choices.push_back({2,static_cast<int64_t>(i),options.subtitles[i]});}
        if(choices.size()==1){notice="Este video no tiene subtitulos.";show();return;}
        const Choice next=choices[(current+1)%choices.size()];
        if(next.kind==0){mpv_set_property_string(mpv,"sid","no");preferred_subtitle.clear();notice=next.label;}
        else if(next.kind==1){select_embedded_sub(rows[static_cast<size_t>(next.value)]);notice="Subtitulos: "+next.label;}
        else if(load_service_sub(next.value))notice="Subtitulos: "+next.label;
        show();};
    // Al abrir un video, vuelve a activar el idioma de subtitulos elegido la ultima vez.
    auto restore_subtitle=[&]{if(preferred_subtitle.empty())return;
        for(const auto& t:tracks())if(t.type=="sub"&&t.lang==preferred_subtitle){select_embedded_sub(t);return;}
        for(size_t i=0;i<options.subtitles.size();++i)if(options.subtitles[i]==preferred_subtitle){const std::string keep=notice;load_service_sub(static_cast<int64_t>(i));notice=keep;return;}};
    auto clock=[](double value){int n=std::max(0,static_cast<int>(value));char b[32];std::snprintf(b,sizeof(b),"%02d:%02d:%02d",n/3600,n/60%60,n%60);return std::string(b);};
    struct Row {std::string label;int action;int64_t value=0;};
    auto menu_rows=[&]{std::vector<Row> rows;
        if(page==0){rows={{flag("pause")?"Continuar":"Pausar",1},{"Audio / idioma",2},{"Subtitulos",3},{"Calidad / fuente",4},
            {"Episodios / canales",5},{"Velocidad: "+std::to_string(property("speed")).substr(0,4)+"x",6},
            {"Pantalla: "+ratios[aspect],7},{"Volumen: "+std::to_string(static_cast<int>(property("volume")+0.5))+" %  (hasta 300)",8},
            {flag("mute")?"Activar sonido":"Silenciar",9},{"Sincronia audio: "+std::to_string(property("audio-delay")).substr(0,5)+" s",10},
            {"Sincronia subtitulos: "+std::to_string(property("sub-delay")).substr(0,5)+" s",11},
            {"Tamano subtitulos: "+std::to_string(property("sub-scale")).substr(0,4),12},{"Brillo: "+std::to_string(static_cast<int>(property("brightness"))),13},
            {"Retroceder 60 segundos",14},{"Adelantar 60 segundos",15},{"Reiniciar video",16},{std::string("Decodificacion: ")+(hardware_decode?"hardware (experimental)":"software"),25},{"Informacion del video",17},{std::string("Siguiente episodio automatico: ")+(auto_next?"Si":"No"),24},{"Cerrar reproductor",18}};}
        if(page==1||page==2){if(page==2)rows.push_back({"Desactivar subtitulos",20,-1});for(const auto& t:tracks())if(t.type==(page==1?"audio":"sub"))rows.push_back({(t.selected?"* ":"  ")+t.label,page==1?19:20,t.id});}
        if(page==2)for(size_t i=0;i<options.subtitles.size();++i)rows.push_back({"Servicio: "+options.subtitles[i],23,static_cast<int64_t>(i)});
        if(page==3)for(size_t i=0;i<options.variants.size();++i)rows.push_back({(static_cast<int>(i)==options.variant?"* ":"  ")+options.variants[i],21,static_cast<int64_t>(i)});
        if(page==4)for(size_t i=0;i<options.items.size();++i)rows.push_back({(static_cast<int>(i)==options.item?"* ":"  ")+options.items[i],22,static_cast<int64_t>(i)});
        if(rows.empty())rows.push_back({"No hay opciones disponibles para este video",0});
        return rows;};
    // Estado y utilidades de la interfaz animada del reproductor.
    float hud=0,menu_t=0,cursor=118,cursor_target=118;Uint32 pulse_at=0,seek_at=0,volume_at=0;int seek_dir=1;
    {const Uint32 start=SDL_GetTicks();if(start>4000){pulse_at=seek_at=volume_at=start-4000;}}
    TextCache local_texts;TextCache& texts=options.texts?*options.texts:local_texts;
    TTF_Font* f_small=options.font_small?options.font_small:options.font;TTF_Font* f_title=options.font_title?options.font_title:options.font;TTF_Font* f_chip=options.font_chip?options.font_chip:f_small;
    auto box=[&](SDL_Rect r,int red,int green,int blue,float a){if(a<=0.004f||r.w<=0||r.h<=0)return;
        SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);SDL_SetRenderDrawColor(renderer,static_cast<Uint8>(red),static_cast<Uint8>(green),static_cast<Uint8>(blue),static_cast<Uint8>(255*std::min(1.0f,a)));SDL_RenderFillRect(renderer,&r);};
    // Rectangulo de esquinas redondeadas real (el video queda detras, no se pueden tapar esquinas).
    auto rounded=[&](SDL_Rect r,int radius,int red,int green,int blue,float a){if(r.w<=0||r.h<=0)return;radius=std::min(radius,std::min(r.w,r.h)/2);
        for(int i=0;i<radius;++i){const double dy=radius-i-0.5;const int inset=radius-static_cast<int>(std::sqrt(radius*radius-dy*dy)+0.5);
            box({r.x+inset,r.y+i,r.w-2*inset,1},red,green,blue,a);box({r.x+inset,r.y+r.h-1-i,r.w-2*inset,1},red,green,blue,a);}
        box({r.x,r.y+radius,r.w,r.h-2*radius},red,green,blue,a);};
    auto shade=[&](int y,int h,bool from_top,float a){for(int i=0;i<h;i+=4){const float t=static_cast<float>(i)/h;const float k=from_top?1-t:t;box({0,y+i,1280,4},0,0,0,a*k*k);}};
    auto put=[&](int x,int y,const std::string& value,SDL_Color color,float a,int width,TTF_Font* type,int align=0){if(!type||a<=0.01f)return 0;
        const auto& item=texts.get(renderer,type,value,color,width,false);if(!item.texture)return 0;
        SDL_Rect dst{x+(align==1?(width-item.w)/2:align==2?width-item.w:0),y,item.w,item.h};
        SDL_SetTextureAlphaMod(item.texture,static_cast<Uint8>(255*std::min(1.0f,a)));SDL_RenderCopy(renderer,item.texture,nullptr,&dst);return item.w;};
    auto measure=[&](const std::string& value,TTF_Font* type,int width){return type?texts.get(renderer,type,value,SDL_Color{245,245,247,255},width,false).w:0;};
    while(running){
#ifdef __SWITCH__
        if(!appletMainLoop())break;
#endif
        bool activate=false,back=false,settings=false,audio=false,subs=false,slower=false,faster=false,ratio=false,mute=false;
        int horizontal=0,vertical=0,jump=0,item_delta=0;SDL_Event event;
        while(SDL_PollEvent(&event)){
            if(event.type==SDL_QUIT)running=false;
            if(event.type==SDL_KEYDOWN&&!event.key.repeat)switch(event.key.keysym.sym){
                case SDLK_ESCAPE:back=true;break;case SDLK_SPACE:activate=true;break;case SDLK_RETURN:activate=true;break;
                case SDLK_x:case SDLK_TAB:settings=true;break;case SDLK_y:audio=true;break;case SDLK_s:subs=true;break;
                case SDLK_LEFT:horizontal-=1;break;case SDLK_RIGHT:horizontal+=1;break;case SDLK_UP:vertical-=1;break;case SDLK_DOWN:vertical+=1;break;
                case SDLK_PAGEUP:item_delta=-1;break;case SDLK_PAGEDOWN:item_delta=1;break;
                case SDLK_LEFTBRACKET:slower=true;break;case SDLK_RIGHTBRACKET:faster=true;break;case SDLK_f:ratio=true;break;case SDLK_m:mute=true;break;
                case SDLK_j:jump=-60;break;case SDLK_l:jump=60;break;
            }
        }
#ifdef __SWITCH__
        padUpdate(&pad);u64 down=padGetButtonsDown(&pad);
        back|=down&HidNpadButton_B;activate|=down&HidNpadButton_A;settings|=down&(HidNpadButton_X|HidNpadButton_Plus);audio|=down&HidNpadButton_Y;subs|=down&HidNpadButton_Minus;
        if(down&HidNpadButton_Left)horizontal=-1;
        if(down&HidNpadButton_Right)horizontal=1;
        if(down&HidNpadButton_Up)vertical=-1;
        if(down&HidNpadButton_Down)vertical=1;
        if(down&HidNpadButton_ZL)jump=-60;
        if(down&HidNpadButton_ZR)jump=60;
        if(down&HidNpadButton_L)item_delta=-1;
        if(down&HidNpadButton_R)item_delta=1;
#endif
        if(back){if(menu){if(page){page=0;selection=0;}else menu=false;}else running=false;show();}
        if(settings){menu=!menu;page=0;selection=0;notice.clear();show();}
        if(audio)cycle_track("audio");
        if(subs)cycle_subs();
        if(slower)cycle_speed(-1);
        if(faster)cycle_speed(1);
        if(ratio)cycle_aspect();
        if(mute){int value=!flag("mute");mpv_set_property(mpv,"mute",MPV_FORMAT_FLAG,&value);show();}
        if(jump)seek_to(jump);
        if(item_delta)request_item(options.item+item_delta);
        if(menu){auto rows=menu_rows();selection=std::clamp(selection+vertical,0,static_cast<int>(rows.size())-1);
            const auto row=rows[selection];
            if(activate||horizontal){const int direction=horizontal?horizontal:1;
                switch(row.action){
                    case 1:toggle_pause();break;case 2:page=1;selection=0;break;case 3:page=2;selection=0;break;
                    case 4:page=3;selection=0;break;case 5:page=4;selection=0;break;case 6:cycle_speed(direction);break;case 7:cycle_aspect();break;
                    case 8:set_number("volume",std::clamp(property("volume")+direction*5,0.0,300.0));volume_at=SDL_GetTicks();break;
                    case 9:{int value=!flag("mute");mpv_set_property(mpv,"mute",MPV_FORMAT_FLAG,&value);break;}
                    case 10:set_number("audio-delay",property("audio-delay")+direction*0.1);break;
                    case 11:set_number("sub-delay",property("sub-delay")+direction*0.25);break;
                    case 12:set_number("sub-scale",std::clamp(property("sub-scale")+direction*0.1,0.5,3.0));break;
                    case 13:set_number("brightness",std::clamp(property("brightness")+direction*5,-100.0,100.0));break;
                    case 14:seek_to(-60);break;case 15:seek_to(60);break;
                    case 16:if(flag("seekable"))command("seek","0","absolute+exact");else notice="Esta emision no permite reiniciar.";break;
                    case 17:{char* format=mpv_get_property_string(mpv,"video-format");notice=(format?std::string(format):"Video")+" | "+std::to_string(static_cast<int>(property("width")))+"x"+std::to_string(static_cast<int>(property("height")))+" | "+std::to_string(property("container-fps")).substr(0,5)+" fps";mpv_free(format);break;}
                    case 18:running=false;break;
                    case 19:mpv_set_property_string(mpv,"aid",std::to_string(row.value).c_str());notice="Audio seleccionado";page=0;selection=1;break;
                    case 20:{mpv_set_property_string(mpv,"sid",row.value<0?"no":std::to_string(row.value).c_str());int value=row.value>=0;mpv_set_property(mpv,"sub-visibility",MPV_FORMAT_FLAG,&value);
                        if(row.value<0)preferred_subtitle.clear();else for(const auto& t:tracks())if(t.type=="sub"&&t.id==row.value&&!t.lang.empty())preferred_subtitle=t.lang;
                        notice="Subtitulos actualizados";page=0;selection=2;break;}
                    case 25:hardware_decode=!hardware_decode;mpv_set_property_string(mpv,"hwdec",hardware_decode?"auto-copy":"no");
                        notice=hardware_decode?"Hardware activado. Si el video falla, vuelve a software.":"Decodificacion por software.";break;
                    case 21:if(row.value!=options.variant){result.next_variant=static_cast<int>(row.value);running=false;}break;
                    case 22:if(row.value!=options.item)request_item(static_cast<int>(row.value));break;
                    case 24:auto_next=!auto_next;break;
                    case 23:if(load_service_sub(row.value)){page=0;selection=2;}break;

                }show();
            }
        }else{if(activate){toggle_pause();pulse_at=SDL_GetTicks();}
        if(horizontal){seek_to(horizontal*10);if(flag("seekable")){seek_dir=horizontal;seek_at=SDL_GetTicks();}}
        // Arriba/abajo: volumen directo, hasta 300 % para contenido que suena bajo.
        if(vertical){set_number("volume",std::clamp(property("volume")-vertical*5,0.0,300.0));volume_at=SDL_GetTicks();}}
        for(;;){
            mpv_event* e=mpv_wait_event(mpv,0);
        if(e->event_id==MPV_EVENT_NONE)break;
            if(e->event_id==MPV_EVENT_SHUTDOWN)running=false;
            if(e->event_id==MPV_EVENT_LOG_MESSAGE){
                const auto* log=static_cast<mpv_event_log_message*>(e->data);
                const std::string hint=log&&log->text?playback_error_hint(log->text):"";
                if(!hint.empty()&&hint!=media_hint&&(media_hint.empty()||hint.rfind("HTTP ",0)==0||media_hint.rfind("HTTP ",0)!=0)){
                    media_hint=hint;trace(("play:classified_error "+hint).c_str());
                }
            }
            if(e->event_id==MPV_EVENT_FILE_LOADED){loaded=true;
        if(pending_start>0&&flag("seekable"))command("seek",std::to_string(pending_start),"absolute+exact");
                pending_start=0;restore_subtitle();}
            if(e->event_id==MPV_EVENT_END_FILE){trace("play:end_file");auto* end=static_cast<mpv_event_end_file*>(e->data);
                if(end&&end->reason==MPV_END_FILE_REASON_ERROR){
                    error=media_hint.empty()?mpv_error_string(end->error):"No se pudo abrir el video: "+media_hint;
                    char failure[80];std::snprintf(failure,sizeof(failure),"play:end_error mpv=%d",end->error);trace(failure);
                }
                result.ended=end&&end->reason==MPV_END_FILE_REASON_EOF;running=false;}
        }
        if(!running)break;
        bool fresh=dirty.exchange(false);
        if(fresh){int size[2]={width,height};mpv_render_param frame[]={{MPV_RENDER_PARAM_SW_SIZE,size},{MPV_RENDER_PARAM_SW_FORMAT,const_cast<char*>("rgb0")},
            {MPV_RENDER_PARAM_SW_STRIDE,&stride},{MPV_RENDER_PARAM_SW_POINTER,pixels},{MPV_RENDER_PARAM_INVALID,nullptr}};
            rc=mpv_render_context_render(context,frame);
        if(rc<0){error=mpv_error_string(rc);break;}
            if(SDL_UpdateTexture(texture,nullptr,pixels,static_cast<int>(stride))<0){error=SDL_GetError();break;}
            if(first_frame&&loaded){trace("play:first_frame");first_frame=false;}
        }
        const Uint32 now=SDL_GetTicks();paused=flag("pause");
        // Interfaz del reproductor: capas que entran y salen con fundidos y desplazamientos.
        const bool want_hud=menu||paused||now<visible_until;
        const bool animating=std::fabs(hud-(want_hud?1.0f:0.0f))>0.004f||std::fabs(menu_t-(menu?1.0f:0.0f))>0.004f||now-pulse_at<650||now-seek_at<650||now-volume_at<1900||(menu&&std::fabs(cursor-cursor_target)>0.5f);
        if(fresh||animating||now-last_draw>=100){
            const float frame_dt=std::min(0.05f,(now-last_draw)/1000.0f);last_draw=now;
            auto glide=[&](float& v,float target,float rate){const float d=target-v;if(std::fabs(d)<0.004f)v=target;else v+=d*std::min(1.0f,frame_dt*rate);};
            glide(hud,want_hud?1.0f:0.0f,9);glide(menu_t,menu?1.0f:0.0f,11);
            SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_NONE);SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);SDL_RenderCopy(renderer,texture,nullptr,nullptr);
            if(options.font){
                const SDL_Color white{255,255,255,255},soft{179,179,179,255},teal{229,9,20,255},gold{245,197,66,255},ink{20,20,20,255};  // "teal" es ahora el rojo de acento
                const int off=static_cast<int>((1-hud)*18);
                const double pos=property("time-pos"),duration=property("duration"),level=property("volume");
                if(hud>0.01f){
                    shade(0,150,true,0.88f*hud);shade(500,220,false,0.92f*hud);
                    put(48,30-off,options.title.empty()?"Reproduciendo":options.title,white,hud,880,f_title);
                    std::string under=duration>0?"":"EN VIVO";
                    if(options.items.size()>1)under+=(under.empty()?"":"   ·   ")+std::to_string(options.item+1)+" de "+std::to_string(options.items.size());
                    if(paused)under+=(under.empty()?"":"   ·   ")+std::string("EN PAUSA");
                    put(50,72-off,under,duration>0?soft:teal,hud,880,f_chip);
                    put(932,40-off,"B   Salir",soft,hud*0.8f,300,f_chip,2);
                    // Barra de progreso con posicion y tiempo restante.
                    const int by=636+off,bw=1184;
                    rounded({48,by,bw,6},3,255,255,255,0.22f*hud);
                    if(duration>0){const int played=static_cast<int>(bw*std::clamp(pos/duration,0.0,1.0));
                        rounded({48,by,std::max(6,played),6},3,teal.r,teal.g,teal.b,hud);rounded({48+played-8,by-5,16,16},8,255,255,255,hud);
                        put(48,by+18,clock(pos),white,hud,300,f_small);put(932,by+18,"-"+clock(duration-pos),soft,hud,300,f_small,2);
                    }else{rounded({48,by,bw,6},3,teal.r,teal.g,teal.b,0.55f*hud);rounded({48,by+20,10,10},5,235,64,64,hud);put(66,by+14,"EN VIVO",white,hud,300,f_chip);}
                    put(0,by+48,"A  Pausa      ◀ ▶  10 s      ZL / ZR  60 s      ▲ ▼  Volumen      Y  Audio      −  Subtítulos      X  Opciones",soft,hud*0.75f,1280,f_chip,1);
                    if(!menu&&!notice.empty()){const int w=measure(notice,f_small,900);rounded({640-w/2-20,112,w+40,40},20,0,0,0,0.72f*hud);put(640-w/2,122,notice,white,hud,900,f_small);}
                }
                // Indicador central: fijo en pausa, destello al reanudar.
                {const float age=(now-pulse_at)/650.0f;const float a=paused?hud:(age<1?1-age:0);
                    if(a>0.01f){const int size=96+(paused?0:static_cast<int>(28*std::min(1.0f,age)));
                        rounded({640-size/2,360-size/2,size,size},size/2,0,0,0,0.55f*a);put(640-size/2+(paused?0:3),344,paused?"❚❚":"▶",white,a,size,f_title,1);}}
                // Aviso de salto al adelantar o retroceder.
                if(now-seek_at<650){const float a=1-(now-seek_at)/650.0f;const int cx=seek_dir>0?930:350,shift=static_cast<int>((1-a)*24)*(seek_dir>0?1:-1);
                    rounded({cx-56+shift,338,112,44},22,0,0,0,0.6f*a);put(cx-56+shift,348,seek_dir>0?"10 s  ▶▶":"◀◀  10 s",white,a,112,f_small,1);}
                // Volumen: la barra pasa a dorado cuando supera el 100 %.
                if(now-volume_at<1900){const float age=(now-volume_at)/1900.0f;const float a=age<0.8f?1.0f:(1-age)/0.2f;
                    const bool boosted=level>100.5;const int filled=static_cast<int>(210*std::clamp(level/300.0,0.0,1.0));
                    rounded({1168,196,64,308},16,0,0,0,0.62f*a);put(1168,208,"♪",white,a,64,f_small,1);
                    rounded({1196,240,8,210},4,255,255,255,0.22f*a);
                    rounded({1196,240+210-filled,8,std::max(8,filled)},4,boosted?gold.r:teal.r,boosted?gold.g:teal.g,boosted?gold.b:teal.b,a);
                    box({1190,240+140,20,2},255,255,255,0.7f*a);        // marca del 100 %
                    put(1168,464,std::to_string(static_cast<int>(level+0.5))+"%",boosted?gold:white,a,64,f_chip,1);}
                // Panel de opciones: se desliza desde la derecha y oscurece el video.
                if(menu_t>0.01f){const float t=1-(1-menu_t)*(1-menu_t);const int px=1280-static_cast<int>(480*t);
                    box({0,0,1280,720},0,0,0,0.42f*menu_t);box({px,0,480,720},20,20,20,0.97f);box({px,0,1,720},255,255,255,0.10f);
                    static const char* pages[]={"Opciones","Audio","Subtítulos","Calidad","Episodios y canales"};
                    put(px+30,30,pages[std::clamp(page,0,4)],white,menu_t,420,f_title);
                    put(px+32,74,page==0?"A  elegir      ◀ ▶  ajustar      B  cerrar":"A  seleccionar      B  volver",soft,menu_t*0.8f,420,f_chip);
                    const auto rows=menu_rows();const int count=static_cast<int>(rows.size()),visible=12,first=std::clamp(selection-6,0,std::max(0,count-visible));
                    cursor_target=static_cast<float>(118+(selection-first)*46);
                    if(menu_t<0.2f)cursor=cursor_target;else glide(cursor,cursor_target,22);
                    rounded({px+16,static_cast<int>(cursor),448,42},10,255,255,255,0.13f*menu_t);rounded({px+16,static_cast<int>(cursor)+9,4,24},2,teal.r,teal.g,teal.b,menu_t);
                    for(int i=first;i<count&&i<first+visible;++i){std::string text=rows[static_cast<size_t>(i)].label;const bool marked=text.rfind("* ",0)==0;
                        if(marked||text.rfind("  ",0)==0)text.erase(0,2);
                        put(px+34,127+(i-first)*46,text,i==selection?white:soft,menu_t,marked?380:414,options.font);
                        if(marked)put(px+424,127+(i-first)*46,"✓",teal,menu_t,30,options.font);}
                    if(count>visible)put(px+30,684,std::to_string(selection+1)+" / "+std::to_string(count),soft,menu_t*0.7f,200,f_chip);
                    if(!notice.empty())put(px+30,count>visible?660:684,notice,teal,menu_t,420,f_chip);
                }
                texts.tick();
            }
#ifdef XUPERSWITCH_TEST
            // Solo en binarios de prueba: guarda capturas periodicas para revisar la interfaz.
            if(const char* shots=std::getenv("XS_PLAYER_SHOTS")){static int shot=0;static Uint32 next=0;
                if(now>=next&&shot<16){next=now+400;char name[96];std::snprintf(name,sizeof(name),"%s_%02d.bmp",shots,shot++);
                    SDL_Surface* s=SDL_CreateRGBSurfaceWithFormat(0,1280,720,32,SDL_PIXELFORMAT_RGBA32);
                    if(s){SDL_RenderReadPixels(renderer,nullptr,s->format->format,s->pixels,s->pitch);SDL_SaveBMP(s,name);SDL_FreeSurface(s);}}}
#endif
            SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_NONE);SDL_RenderPresent(renderer);
        }else SDL_Delay(5);
    }
    result.auto_next=auto_next;
    // A classified network message can arrive after END_FILE in the same queue drain.
    if(!error.empty()&&!media_hint.empty())error="No se pudo abrir el video: "+media_hint;
    result.position=property("time-pos");result.speed=property("speed");result.volume=property("volume");result.paused=flag("pause");
    result.audio_delay=property("audio-delay");result.sub_delay=property("sub-delay");result.sub_scale=property("sub-scale");result.aspect=property("video-aspect-override");
    mpv_get_property(mpv,"aid",MPV_FORMAT_INT64,&result.audio);mpv_get_property(mpv,"sid",MPV_FORMAT_INT64,&result.subtitle);
    if(outcome)*outcome=result;
    cleanup();return error;
}
}
