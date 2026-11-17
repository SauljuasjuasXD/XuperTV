// uso: zip_extract_test <zip> <entrada> <esperado> ; ademas comprueba rechazos.
#include "zip_extract.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FALLO linea %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static std::string slurp(const std::string& p){std::ifstream f(p,std::ios::binary);return std::string(std::istreambuf_iterator<char>(f),{});}
int main(int argc,char** argv){
    if(argc!=4)return 2;
    const std::string zip=argv[1],entry=argv[2],expected=slurp(argv[3]);std::string error;
    CHECK(!expected.empty());
    CHECK(xs::zip_extract_entry(zip,entry,"zip_out.bin",expected.size(),error));
    CHECK(slurp("zip_out.bin")==expected);
    CHECK(xs::zip_extract_entry(zip,"stored.txt","zip_stored.bin",1024,error)&&slurp("zip_stored.bin")=="sin comprimir\n");
    CHECK(!xs::zip_extract_entry(zip,"no/existe","zip_none.bin",1024,error));
    CHECK(!xs::zip_extract_entry(zip,entry,"zip_small.bin",expected.size()-1,error));        // limite de tamano
    // ZIP danado: se altera un byte de los datos comprimidos -> CRC o inflate deben fallar.
    std::string bad=slurp(zip);CHECK(bad.size()>200);bad[bad.size()/3]^=0x5a;
    {std::ofstream f("zip_bad.zip",std::ios::binary);f.write(bad.data(),static_cast<std::streamsize>(bad.size()));}
    CHECK(!xs::zip_extract_entry("zip_bad.zip",entry,"zip_bad.bin",expected.size(),error));
    CHECK(slurp("zip_bad.bin").empty());                                                     // no deja restos
    CHECK(!xs::zip_extract_entry(argv[3],entry,"zip_notzip.bin",expected.size(),error));     // no es un ZIP
    std::puts("zip_extract_test OK");return 0;
}
