#include "subtitle_text.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FALLO linea %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
int main(){
    const std::string srt="1\n00:00:01,000 --> 00:00:02,000\n";
    std::string a=srt+"ni\xc3\xb1o \xc2\xbfqu\xc3\xa9?\n";const std::string expected=a;       // UTF-8 valido: intacto
    CHECK(std::strcmp(xs::normalize_subtitle(a),".srt")==0&&a==expected);
    std::string bom="\xef\xbb\xbf"+expected;CHECK(std::strcmp(xs::normalize_subtitle(bom),".srt")==0&&bom==expected);
    std::string latin=srt+"ni\xf1o \xbfqu\xe9?\n";xs::normalize_subtitle(latin);CHECK(latin==expected);   // Windows-1252
    std::string quotes="\x93hola\x94 \x85";xs::normalize_subtitle(quotes);CHECK(quotes=="\xe2\x80\x9chola\xe2\x80\x9d \xe2\x80\xa6");
    std::string le("\xff\xfe",2);for(char c:std::string("WEBVTT\n")){le+=c;le+='\0';}le+="\xf1";le+='\0';     // UTF-16 LE
    CHECK(std::strcmp(xs::normalize_subtitle(le),".vtt")==0&&le=="WEBVTT\n\xc3\xb1");
    std::string be("\xfe\xff",2);for(char c:std::string("hi")){be+='\0';be+=c;}be+=std::string("\xd8\x3d\xde\x00",4); // par sustituto U+1F600
    xs::normalize_subtitle(be);CHECK(be=="hi\xf0\x9f\x98\x80");
    std::string ass="\r\n[Script Info]\nTitle: x\n";CHECK(std::strcmp(xs::normalize_subtitle(ass),".ass")==0);
    std::string vtt="WEBVTT\n\n00:01.000 --> 00:02.000\nhola\n";CHECK(std::strcmp(xs::normalize_subtitle(vtt),".vtt")==0);
    std::string empty;CHECK(std::strcmp(xs::normalize_subtitle(empty),".srt")==0&&empty.empty());
    CHECK(xs::valid_utf8("ok")&&!xs::valid_utf8("\xc3")&&!xs::valid_utf8("\xff")&&!xs::valid_utf8("a\x80"));
    std::puts("subtitle_text_test OK");return 0;
}
