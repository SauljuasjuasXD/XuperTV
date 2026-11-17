#pragma once
#define JSON_NOEXCEPTION 1
#include "json.hpp"
#include <atomic>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <string>
// Autoactualizacion desde GitHub Releases. Solo instala un NRO cuyo SHA256 figure
// en SHA256SUMS.txt de la misma release. Las funciones puras viven aqui para
// poder probarlas en PC (tests/updater_test.cpp); la red y la SD, en updater.cpp.
namespace xs {
// asset_*: el NRO suelto si la release lo trae; si no, el paquete "*_Switch.zip" (zip=true).
struct UpdateInfo{std::string tag,asset_url,asset_name,sums_url;bool zip=false;};
struct UpdateResult{bool ok=false;std::string status;};

// "v0.14.1" / "0.14.1" -> {0,14,1}. Rechaza cualquier otro formato (sin sufijos).
inline bool parse_version(const std::string& text,int out[3]){
    size_t i=(!text.empty()&&(text[0]=='v'||text[0]=='V'))?1:0;
    for(int part=0;part<3;++part){
        if(i>=text.size()||!std::isdigit(static_cast<unsigned char>(text[i])))return false;
        long value=0;size_t digits=0;
        while(i<text.size()&&std::isdigit(static_cast<unsigned char>(text[i]))){
            if(++digits>6)return false;
            value=value*10+(text[i]-'0');++i;
        }
        out[part]=static_cast<int>(value);
        if(part<2){if(i>=text.size()||text[i]!='.')return false;++i;}
    }
    return i==text.size();
}
// true solo si ambas versiones son validas y `candidate` es estrictamente mayor.
inline bool version_newer(const std::string& candidate,const std::string& current){
    int a[3],b[3];if(!parse_version(candidate,a)||!parse_version(current,b))return false;
    for(int i=0;i<3;++i)if(a[i]!=b[i])return a[i]>b[i];
    return false;
}
// "owner/name" con caracteres seguros para formar una URL.
inline bool valid_repo(const std::string& repo){
    const auto slash=repo.find('/');
    if(slash==std::string::npos||slash==0||slash+1>=repo.size()||repo.find('/',slash+1)!=std::string::npos)return false;
    for(char c:repo)if(c!='/'&&c!='.'&&c!='-'&&c!='_'&&!std::isalnum(static_cast<unsigned char>(c)))return false;
    return true;
}
// Lee la respuesta de /releases/latest. Ignora borradores y prereleases; exige HTTPS.
inline bool parse_release(const std::string& json,const std::string& nro_name,const std::string& zip_suffix,const std::string& sums_name,UpdateInfo& out,std::string& error){
    auto doc=nlohmann::json::parse(json,nullptr,false);
    if(!doc.is_object()){error="Respuesta de GitHub invalida.";return false;}
    auto flag=[&](const char* key){auto i=doc.find(key);return i!=doc.end()&&i->is_boolean()&&i->get<bool>();};
    if(flag("draft")||flag("prerelease")){error="La ultima release no es estable.";return false;}
    auto tag=doc.find("tag_name");
    if(tag==doc.end()||!tag->is_string()){error="La release no tiene version.";return false;}
    UpdateInfo info;info.tag=tag->get<std::string>();
    auto assets=doc.find("assets");
    if(assets==doc.end()||!assets->is_array()){error="La release no tiene archivos.";return false;}
    for(const auto& a:*assets){
        if(!a.is_object())continue;
        auto n=a.find("name"),u=a.find("browser_download_url");
        if(n==a.end()||u==a.end()||!n->is_string()||!u->is_string())continue;
        const auto url=u->get<std::string>();
        if(url.rfind("https://",0)!=0)continue;
        const auto name=n->get<std::string>();
        if(name==sums_name)info.sums_url=url;
        else if(name==nro_name){info.asset_url=url;info.asset_name=name;info.zip=false;}
        else if(info.asset_url.empty()&&name.size()>zip_suffix.size()&&name.compare(name.size()-zip_suffix.size(),zip_suffix.size(),zip_suffix)==0
                &&name.find_first_of("/\\ \t\r\n")==std::string::npos){info.asset_url=url;info.asset_name=name;info.zip=true;}
    }
    if(info.asset_url.empty()){error="La release no incluye "+nro_name+" ni un paquete "+zip_suffix+".";return false;}
    if(info.sums_url.empty()){error="La release no incluye "+sums_name+"; no se instala sin verificar.";return false;}
    out=info;return true;
}
// Busca "<64 hex>  <archivo>" (o "<hex> *<archivo>") y devuelve el hash en minusculas.
inline bool find_sum(const std::string& text,const std::string& filename,std::string& hex){
    size_t pos=0;
    while(pos<text.size()){
        size_t end=text.find('\n',pos);if(end==std::string::npos)end=text.size();
        std::string line=text.substr(pos,end-pos);pos=end+1;
        while(!line.empty()&&(line.back()=='\r'||line.back()==' '))line.pop_back();
        if(line.size()<66)continue;
        std::string name=line.substr(64);
        const auto start=name.find_first_not_of(" *");
        if(start==std::string::npos||start==0||name.substr(start)!=filename)continue;
        std::string h=line.substr(0,64);bool valid=true;
        for(char& c:h){if(!std::isxdigit(static_cast<unsigned char>(c))){valid=false;break;}c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));}
        if(valid){hex=h;return true;}
    }
    return false;
}
// Cabecera de un NRO: magia "NRO0" en el offset 0x10.
inline bool looks_like_nro(const unsigned char* data,size_t size){
    return size>=0x80&&data[0x10]=='N'&&data[0x11]=='R'&&data[0x12]=='O'&&data[0x13]=='0';
}

// Red y SD (updater.cpp). `config_path` es un JSON opcional {"repo":"owner/name"}.
bool updater_check(const std::string& config_path,const std::string& ca,const std::string& current_version,
                   UpdateInfo& out,std::string& status);
UpdateResult updater_install(const UpdateInfo& info,const std::string& nro_path,const std::string& ca,
                             const std::atomic<bool>& cancel);
}
