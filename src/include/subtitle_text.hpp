#pragma once
#include <cstdint>
#include <string>
// Normaliza un subtitulo descargado antes de entregarlo a libmpv: lo deja en UTF-8
// sin BOM (acepta UTF-8, UTF-16 LE/BE y Windows-1252) y deduce su formato.
namespace xs {
inline bool valid_utf8(const std::string& s){
    size_t i=0;const size_t n=s.size();
    while(i<n){const unsigned char c=static_cast<unsigned char>(s[i]);size_t extra=0;
        if(c<0x80)extra=0;else if(c>=0xc2&&c<=0xdf)extra=1;else if(c>=0xe0&&c<=0xef)extra=2;else if(c>=0xf0&&c<=0xf4)extra=3;else return false;
        if(i+extra>=n&&extra)return false;
        for(size_t k=1;k<=extra;++k)if((static_cast<unsigned char>(s[i+k])&0xc0)!=0x80)return false;
        i+=extra+1;}
    return true;
}
inline void append_utf8(std::string& out,uint32_t cp){
    if(cp<0x80)out+=static_cast<char>(cp);
    else if(cp<0x800){out+=static_cast<char>(0xc0|(cp>>6));out+=static_cast<char>(0x80|(cp&0x3f));}
    else if(cp<0x10000){out+=static_cast<char>(0xe0|(cp>>12));out+=static_cast<char>(0x80|((cp>>6)&0x3f));out+=static_cast<char>(0x80|(cp&0x3f));}
    else{out+=static_cast<char>(0xf0|(cp>>18));out+=static_cast<char>(0x80|((cp>>12)&0x3f));out+=static_cast<char>(0x80|((cp>>6)&0x3f));out+=static_cast<char>(0x80|(cp&0x3f));}
}
// Devuelve la extension del formato detectado (".srt" por defecto).
inline const char* normalize_subtitle(std::string& data){
    auto u=[&](size_t i){return static_cast<uint32_t>(static_cast<unsigned char>(data[i]));};
    if(data.size()>=2&&((u(0)==0xff&&u(1)==0xfe)||(u(0)==0xfe&&u(1)==0xff))){
        const bool le=u(0)==0xff;std::string out;out.reserve(data.size());
        auto unit=[&](size_t i){return le?u(i)|(u(i+1)<<8):(u(i)<<8)|u(i+1);};
        for(size_t i=2;i+1<data.size();i+=2){uint32_t c=unit(i);
            if(c>=0xd800&&c<0xdc00&&i+3<data.size()){const uint32_t d=unit(i+2);
                if(d>=0xdc00&&d<0xe000){c=0x10000+((c-0xd800)<<10)+(d-0xdc00);i+=2;}}
            append_utf8(out,c);}
        data.swap(out);
    }else if(data.size()>=3&&u(0)==0xef&&u(1)==0xbb&&u(2)==0xbf)data.erase(0,3);
    else if(!valid_utf8(data)){
        // Windows-1252: la codificacion habitual de los subtitulos antiguos en espanol.
        static const uint16_t high[32]={0x20AC,0x81,0x201A,0x0192,0x201E,0x2026,0x2020,0x2021,0x02C6,0x2030,0x0160,0x2039,0x0152,0x8D,0x017D,0x8F,
                                        0x90,0x2018,0x2019,0x201C,0x201D,0x2022,0x2013,0x2014,0x02DC,0x2122,0x0161,0x203A,0x0153,0x9D,0x017E,0x0178};
        std::string out;out.reserve(data.size()*3/2);
        for(unsigned char c:data)append_utf8(out,c<0x80?c:c<0xa0?high[c-0x80]:c);
        data.swap(out);
    }
    const size_t start=data.find_first_not_of(" \t\r\n");
    const std::string head=start==std::string::npos?"":data.substr(start,16);
    if(head.rfind("WEBVTT",0)==0)return ".vtt";
    if(head.rfind("[Script Info]",0)==0||head.rfind("[V4",0)==0)return ".ass";
    if(head.rfind("<SAMI",0)==0||head.rfind("<sami",0)==0)return ".smi";
    return ".srt";
}
}
