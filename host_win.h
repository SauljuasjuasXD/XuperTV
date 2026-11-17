// Solo para compilar la version de PC en Windows (MSYS2). No se usa en el NRO.
#ifdef _WIN32
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>
#include <io.h>
#include <direct.h>
namespace std{inline void* aligned_alloc(size_t,size_t size){return std::malloc(size);}}
#define mkdir(path,mode) mkdir(path)
#endif
