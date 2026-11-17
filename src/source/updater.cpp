#include "updater.hpp"
#include "installation.hpp"
#include "zip_extract.hpp"
#include <curl/curl.h>
#include <mbedtls/sha256.h>
#include <cstdio>
#include <fstream>
#include <vector>
// Por defecto se actualiza desde el repositorio de XuperTV. Se puede cambiar al compilar
// (make UPDATE_REPO=usuario/fork) o en la SD con update.json: {"repo":"usuario/fork"}.
#ifndef XS_UPDATE_REPO
#define XS_UPDATE_REPO ""
#endif
namespace xs {
namespace {
constexpr size_t kMaxJson=256*1024,kMaxSums=64*1024,kMaxDownload=100u*1024*1024,kMaxNro=100u*1024*1024,kMinNro=0x1000;
const char* kDefaultRepo="SauljuasjuasXD/XuperTV";
const char* kNroName="xuperswitch.nro";
const char* kZipSuffix="_Switch.zip";
const char* kZipEntry="switch/xuperswitch/xuperswitch.nro";
const char* kSumsName="SHA256SUMS.txt";

struct Download{
    std::string* text=nullptr;size_t limit=0;
    FILE* file=nullptr;mbedtls_sha256_context sha{};size_t total=0;
    const std::atomic<bool>* cancel=nullptr;bool too_big=false;
};
size_t on_data(char* data,size_t size,size_t count,void* user){
    auto* d=static_cast<Download*>(user);const size_t n=size*count;
    if(d->cancel&&d->cancel->load())return 0;
    if(d->total+n>d->limit){d->too_big=true;return 0;}
    d->total+=n;
    if(d->text){d->text->append(data,n);return n;}
    if(mbedtls_sha256_update_ret(&d->sha,reinterpret_cast<const unsigned char*>(data),n))return 0;
    return std::fwrite(data,1,n,d->file)==n?n:0;
}
// Solo HTTPS (tambien en redirecciones), TLS verificado, tiempos limitados.
bool fetch(const std::string& url,const std::string& ca,Download& d,std::string& error){
    CURL* c=curl_easy_init();if(!c){error="No se pudo iniciar cURL.";return false;}
    curl_slist* headers=curl_slist_append(nullptr,"Accept: application/vnd.github+json");
    curl_easy_setopt(c,CURLOPT_URL,url.c_str());
    curl_easy_setopt(c,CURLOPT_HTTPHEADER,headers);
    curl_easy_setopt(c,CURLOPT_USERAGENT,"XuperSwitch-updater");
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(c,CURLOPT_PROTOCOLS_STR,"https");curl_easy_setopt(c,CURLOPT_REDIR_PROTOCOLS_STR,"https");
#else
    curl_easy_setopt(c,CURLOPT_PROTOCOLS,CURLPROTO_HTTPS);curl_easy_setopt(c,CURLOPT_REDIR_PROTOCOLS,CURLPROTO_HTTPS);
#endif
    curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,1L);
    curl_easy_setopt(c,CURLOPT_MAXREDIRS,5L);
    curl_easy_setopt(c,CURLOPT_FAILONERROR,1L);
    curl_easy_setopt(c,CURLOPT_CONNECTTIMEOUT,15L);
    curl_easy_setopt(c,CURLOPT_LOW_SPEED_LIMIT,1024L);
    curl_easy_setopt(c,CURLOPT_LOW_SPEED_TIME,30L);
    curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,on_data);
    curl_easy_setopt(c,CURLOPT_WRITEDATA,&d);
    {std::ifstream cert(ca);if(cert.good())curl_easy_setopt(c,CURLOPT_CAINFO,ca.c_str());}
    const CURLcode rc=curl_easy_perform(c);
    curl_slist_free_all(headers);curl_easy_cleanup(c);
    if(rc==CURLE_OK)return true;
    if(d.cancel&&d.cancel->load())error="Actualizacion cancelada.";
    else if(d.too_big)error="La respuesta es demasiado grande.";
    else error=std::string("Fallo de red: ")+curl_easy_strerror(rc);
    return false;
}
std::string hex_of(const unsigned char* h,size_t n){
    static const char* digits="0123456789abcdef";std::string s;
    for(size_t i=0;i<n;++i){s+=digits[h[i]>>4];s+=digits[h[i]&15];}return s;
}
}

bool updater_check(const std::string& config_path,const std::string& ca,const std::string& current_version,
                   UpdateInfo& out,std::string& status){
    std::string repo=XS_UPDATE_REPO;if(repo.empty())repo=kDefaultRepo;
    InstallationJson config;
    if(read_installation_json(config_path,config)){
        auto r=config.find("repo");if(r!=config.end()&&r->is_string())repo=r->get<std::string>();
    }
    if(!valid_repo(repo)){status="update.json invalido; usa {\"repo\":\"usuario/nombre\"}.";return false;}
    if(curl_global_init(CURL_GLOBAL_DEFAULT)!=CURLE_OK){status="No se pudo iniciar cURL.";return false;}
    std::string body;Download d;d.text=&body;d.limit=kMaxJson;
    if(!fetch("https://api.github.com/repos/"+repo+"/releases/latest",ca,d,status))return false;
    UpdateInfo info;
    if(!parse_release(body,kNroName,kZipSuffix,kSumsName,info,status))return false;
    if(!version_newer(info.tag,current_version)){status="Ya tienes la ultima version ("+current_version+").";return false;}
    out=info;status="Version "+info.tag+" disponible. Pulsa ZR otra vez para instalar.";return true;
}

UpdateResult updater_install(const UpdateInfo& info,const std::string& nro_path,const std::string& ca,
                             const std::atomic<bool>& cancel){
    UpdateResult result;
    if(curl_global_init(CURL_GLOBAL_DEFAULT)!=CURLE_OK){result.status="No se pudo iniciar cURL.";return result;}
    // 1) Hash esperado del archivo publicado, de la misma release.
    std::string sums,expected;{Download d;d.text=&sums;d.limit=kMaxSums;d.cancel=&cancel;
        if(!fetch(info.sums_url,ca,d,result.status))return result;}
    if(!find_sum(sums,info.asset_name,expected)){result.status=std::string(kSumsName)+" no lista "+info.asset_name+".";return result;}
    // 2) Descarga calculando el SHA256 al vuelo.
    const std::string download=nro_path+".dl",fresh=nro_path+".new",backup=nro_path+".bak";
    Download d;d.limit=kMaxDownload;d.cancel=&cancel;mbedtls_sha256_init(&d.sha);
    if(mbedtls_sha256_starts_ret(&d.sha,0)){mbedtls_sha256_free(&d.sha);result.status="No se pudo iniciar SHA256.";return result;}
    d.file=std::fopen(download.c_str(),"wb");
    if(!d.file){mbedtls_sha256_free(&d.sha);result.status="No se pudo escribir en la SD.";return result;}
    const bool fetched=fetch(info.asset_url,ca,d,result.status);
    const bool closed=std::fclose(d.file)==0;
    unsigned char digest[32]{};const bool hashed=mbedtls_sha256_finish_ret(&d.sha,digest)==0;mbedtls_sha256_free(&d.sha);
    auto fail=[&](const std::string& why){const std::string message=why;std::remove(download.c_str());std::remove(fresh.c_str());result.status=message;return result;};
    if(!fetched)return fail(result.status);
    if(!closed||!hashed)return fail("No se pudo guardar la descarga.");
    // 3) Verificar el hash antes de abrir o extraer nada.
    if(hex_of(digest,32)!=expected)return fail("El SHA256 no coincide; no se instalo nada.");
    // 4) Obtener el NRO nuevo: extraido del ZIP oficial o el archivo descargado tal cual.
    if(info.zip){std::string why;
        if(!zip_extract_entry(download,kZipEntry,fresh,kMaxNro,why))return fail(why);
        std::remove(download.c_str());
    }else if(std::rename(download.c_str(),fresh.c_str())!=0)return fail("No se pudo preparar el NRO descargado.");
    {std::ifstream f(fresh,std::ios::binary|std::ios::ate);const auto size=f.tellg();std::vector<unsigned char> head(0x80);
     f.seekg(0);f.read(reinterpret_cast<char*>(head.data()),head.size());
     if(!f||size<static_cast<std::streamoff>(kMinNro)||!looks_like_nro(head.data(),head.size()))return fail("El archivo descargado no es un NRO valido.");}
    // 5) Reemplazo: actual -> .bak, nuevo -> actual; si falla, se restaura.
    std::remove(backup.c_str());
    if(std::rename(nro_path.c_str(),backup.c_str())!=0)return fail("No se pudo respaldar el NRO actual.");
    if(std::rename(fresh.c_str(),nro_path.c_str())!=0){
        const bool restored=std::rename(backup.c_str(),nro_path.c_str())==0;
        return fail(restored?"No se pudo instalar; se conservo la version anterior.":"Error grave: restaura xuperswitch.nro.bak a mano.");
    }
    result.ok=true;result.status="Actualizado a "+info.tag+". Sal a hbmenu y vuelve a abrir la app.";return result;
}
}
