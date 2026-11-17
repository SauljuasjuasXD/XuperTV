#include "zip_extract.hpp"
#include <zlib.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
namespace xs {
namespace {
uint32_t le16(const unsigned char* p){return p[0]|(p[1]<<8);}
uint32_t le32(const unsigned char* p){return p[0]|(p[1]<<8)|(p[2]<<16)|(static_cast<uint32_t>(p[3])<<24);}
struct File{FILE* f=nullptr;~File(){if(f)std::fclose(f);}};
bool read_at(FILE* f,long offset,unsigned char* out,size_t n){return std::fseek(f,offset,SEEK_SET)==0&&std::fread(out,1,n,f)==n;}
}
bool zip_extract_entry(const std::string& zip_path,const std::string& entry,const std::string& out_path,
                       size_t max_size,std::string& error){
    File zip;zip.f=std::fopen(zip_path.c_str(),"rb");
    if(!zip.f||std::fseek(zip.f,0,SEEK_END)!=0){error="No se pudo abrir el ZIP.";return false;}
    const long size=std::ftell(zip.f);
    if(size<22){error="ZIP incompleto.";return false;}
    // Fin del directorio central: firma PK\5\6 en los ultimos 64 KiB + 22 bytes.
    const long tail_size=size<65557?size:65557;std::vector<unsigned char> tail(static_cast<size_t>(tail_size));
    if(!read_at(zip.f,size-tail_size,tail.data(),tail.size())){error="No se pudo leer el ZIP.";return false;}
    long eocd=-1;for(long i=tail_size-22;i>=0;--i)if(le32(&tail[static_cast<size_t>(i)])==0x06054b50){eocd=i;break;}
    if(eocd<0){error="ZIP sin directorio central.";return false;}
    const uint32_t count=le16(&tail[static_cast<size_t>(eocd)+10]),cd_size=le32(&tail[static_cast<size_t>(eocd)+12]),cd_offset=le32(&tail[static_cast<size_t>(eocd)+16]);
    if(cd_size>4u*1024*1024||static_cast<uint64_t>(cd_offset)+cd_size>static_cast<uint64_t>(size)){error="Directorio del ZIP invalido.";return false;}
    std::vector<unsigned char> cd(cd_size);
    if(cd_size&&!read_at(zip.f,static_cast<long>(cd_offset),cd.data(),cd.size())){error="No se pudo leer el directorio del ZIP.";return false;}
    size_t pos=0;bool found=false;uint32_t method=0,crc=0,packed=0,unpacked=0,local=0,flags=0;
    for(uint32_t i=0;i<count&&pos+46<=cd.size();++i){
        const unsigned char* h=&cd[pos];if(le32(h)!=0x02014b50)break;
        const uint32_t name_len=le16(h+28),extra_len=le16(h+30),comment_len=le16(h+32);
        if(pos+46+name_len>cd.size())break;
        if(name_len==entry.size()&&std::memcmp(h+46,entry.data(),name_len)==0){
            flags=le16(h+8);method=le16(h+10);crc=le32(h+16);packed=le32(h+20);unpacked=le32(h+24);local=le32(h+42);found=true;break;
        }
        pos+=46+name_len+extra_len+comment_len;
    }
    if(!found){error="El ZIP no contiene "+entry+".";return false;}
    if((flags&1)||(method!=0&&method!=8)||packed==0xffffffffu||unpacked==0xffffffffu){error="Formato de ZIP no admitido.";return false;}
    if(unpacked>max_size){error="El archivo dentro del ZIP es demasiado grande.";return false;}
    unsigned char lh[30];
    if(!read_at(zip.f,static_cast<long>(local),lh,sizeof(lh))||le32(lh)!=0x04034b50){error="Cabecera local del ZIP invalida.";return false;}
    const uint64_t data=static_cast<uint64_t>(local)+30+le16(lh+26)+le16(lh+28);
    if(data+packed>static_cast<uint64_t>(size)||std::fseek(zip.f,static_cast<long>(data),SEEK_SET)!=0){error="Datos del ZIP fuera de rango.";return false;}
    File out;out.f=std::fopen(out_path.c_str(),"wb");
    if(!out.f){error="No se pudo escribir en la SD.";return false;}
    std::vector<unsigned char> in(64*1024),buf(64*1024);
    z_stream z{};const bool deflated=method==8;
    if(deflated&&inflateInit2(&z,-MAX_WBITS)!=Z_OK){error="No se pudo iniciar zlib.";return false;}
    uint32_t left=packed,written=0;uLong sum=crc32(0L,Z_NULL,0);bool ok=true,finished=!deflated;
    while(ok&&left>0){
        const size_t n=left<in.size()?left:in.size();
        if(std::fread(in.data(),1,n,zip.f)!=n){ok=false;break;}
        left-=static_cast<uint32_t>(n);
        if(!deflated){
            if(written+n>unpacked||std::fwrite(in.data(),1,n,out.f)!=n){ok=false;break;}
            sum=crc32(sum,in.data(),static_cast<uInt>(n));written+=static_cast<uint32_t>(n);continue;
        }
        z.next_in=in.data();z.avail_in=static_cast<uInt>(n);
        do{ // Repite mientras el buffer de salida se llene: puede quedar salida pendiente.
            z.next_out=buf.data();z.avail_out=static_cast<uInt>(buf.size());
            const int rc=inflate(&z,Z_NO_FLUSH);
            if(rc==Z_BUF_ERROR)break;              // sin progreso posible: falta entrada
            if(rc!=Z_OK&&rc!=Z_STREAM_END){ok=false;break;}
            const size_t produced=buf.size()-z.avail_out;
            if(written+produced>unpacked||std::fwrite(buf.data(),1,produced,out.f)!=produced){ok=false;break;}
            sum=crc32(sum,buf.data(),static_cast<uInt>(produced));written+=static_cast<uint32_t>(produced);
            if(rc==Z_STREAM_END)finished=true;
        }while(ok&&!finished&&z.avail_out==0);
    }
    if(deflated)inflateEnd(&z);
    const bool closed=std::fclose(out.f)==0;out.f=nullptr;
    if(!ok||!closed||!finished||written!=unpacked||static_cast<uint32_t>(sum)!=crc){std::remove(out_path.c_str());error="El ZIP esta danado (tamano o CRC incorrecto).";return false;}
    return true;
}
}
