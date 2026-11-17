#include "updater.hpp"
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FALLO linea %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
int main(){
    int v[3];
    CHECK(xs::parse_version("v0.14.1",v)&&v[0]==0&&v[1]==14&&v[2]==1);
    CHECK(!xs::parse_version("0.14",v));CHECK(!xs::parse_version("v1.2.3-beta",v));CHECK(!xs::parse_version("",v));
    CHECK(xs::version_newer("v0.14.2","0.14.1"));CHECK(xs::version_newer("v0.15.0","0.14.9"));
    CHECK(xs::version_newer("1.0.0","0.99.99"));CHECK(!xs::version_newer("v0.14.1","0.14.1"));
    CHECK(!xs::version_newer("v0.13.9","0.14.1"));CHECK(!xs::version_newer("basura","0.14.1"));
    CHECK(xs::valid_repo("SauljuasjuasXD/XuperTV"));
    CHECK(xs::valid_repo("jonaruacho/Xuper-TV-by-JonaRuacho"));
    CHECK(!xs::valid_repo(""));CHECK(!xs::valid_repo("solo"));CHECK(!xs::valid_repo("a/b/c"));
    CHECK(!xs::valid_repo("a/b?x=1"));CHECK(!xs::valid_repo("/b"));CHECK(!xs::valid_repo("a/"));

    const char* good=R"({"tag_name":"v0.15.0","draft":false,"prerelease":false,"assets":[
        {"name":"xuperswitch.nro","browser_download_url":"https://github.com/o/r/releases/download/v0.15.0/xuperswitch.nro"},
        {"name":"SHA256SUMS.txt","browser_download_url":"https://github.com/o/r/releases/download/v0.15.0/SHA256SUMS.txt"}]})";
    xs::UpdateInfo info;std::string err;
    CHECK(xs::parse_release(good,"xuperswitch.nro","_Switch.zip","SHA256SUMS.txt",info,err));
    CHECK(info.tag=="v0.15.0"&&!info.zip&&info.asset_name=="xuperswitch.nro"&&info.asset_url.find("xuperswitch.nro")!=std::string::npos&&!info.sums_url.empty());
    // Release del repositorio original: solo trae el paquete *_Switch.zip.
    CHECK(xs::parse_release(R"({"tag_name":"v0.15.0","assets":[{"name":"X_0.15.0_Fuentes.zip","browser_download_url":"https://x/f"},
        {"name":"X_0.15.0_Switch.zip","browser_download_url":"https://x/z"},{"name":"SHA256SUMS.txt","browser_download_url":"https://x/s"}]})","xuperswitch.nro","_Switch.zip","SHA256SUMS.txt",info,err));
    CHECK(info.zip&&info.asset_name=="X_0.15.0_Switch.zip"&&info.asset_url=="https://x/z");
    CHECK(!xs::parse_release("no es json","xuperswitch.nro","_Switch.zip","SHA256SUMS.txt",info,err));
    CHECK(!xs::parse_release(R"({"tag_name":"v9.9.9","prerelease":true,"assets":[]})","xuperswitch.nro","_Switch.zip","SHA256SUMS.txt",info,err));
    CHECK(!xs::parse_release(R"({"tag_name":"v9.9.9","assets":[{"name":"xuperswitch.nro","browser_download_url":"http://x/y"},
        {"name":"SHA256SUMS.txt","browser_download_url":"https://x/s"}]})","xuperswitch.nro","_Switch.zip","SHA256SUMS.txt",info,err));
    CHECK(!xs::parse_release(R"({"tag_name":"v9.9.9","assets":[{"name":"xuperswitch.nro","browser_download_url":"https://x/y"}]})","xuperswitch.nro","_Switch.zip","SHA256SUMS.txt",info,err));

    std::string hex;
    const std::string h(64,'A');
    CHECK(xs::find_sum(h+"  xuperswitch.nro\n","xuperswitch.nro",hex)&&hex==std::string(64,'a'));
    CHECK(xs::find_sum("x\r\n"+h+" *xuperswitch.nro\r\n","xuperswitch.nro",hex));
    CHECK(!xs::find_sum(h+"  otro.zip\n","xuperswitch.nro",hex));
    CHECK(!xs::find_sum(h+"xuperswitch.nro\n","xuperswitch.nro",hex));
    CHECK(!xs::find_sum(std::string(64,'z')+"  xuperswitch.nro\n","xuperswitch.nro",hex));

    unsigned char nro[0x80]{};nro[0x10]='N';nro[0x11]='R';nro[0x12]='O';nro[0x13]='0';
    CHECK(xs::looks_like_nro(nro,sizeof(nro)));
    nro[0x13]='1';CHECK(!xs::looks_like_nro(nro,sizeof(nro)));CHECK(!xs::looks_like_nro(nro,0x20));
    std::puts("updater_test OK");return 0;
}
