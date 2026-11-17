// Solo Switch. En libnx todo hilo creado con pthread/std::thread nace en el nucleo
// por defecto del proceso (cpuid -2), asi que libmpv, FFmpeg, el audio y la interfaz
// compartian UN solo nucleo. Con -Wl,--wrap=threadCreate repartimos los hilos nuevos
// entre los nucleos 0-2; si el nucleo pedido no esta permitido, se usa el de siempre.
#ifdef __SWITCH__
#include <switch.h>
#include <atomic>
extern "C" {
Result __real_threadCreate(Thread* t,ThreadFunc entry,void* arg,void* stack_mem,size_t stack_sz,int prio,int cpuid);
Result __wrap_threadCreate(Thread* t,ThreadFunc entry,void* arg,void* stack_mem,size_t stack_sz,int prio,int cpuid){
    static std::atomic<unsigned> next{0};
    if(cpuid!=-2)return __real_threadCreate(t,entry,arg,stack_mem,stack_sz,prio,cpuid);
    static const int order[3]={1,2,0};
    const Result rc=__real_threadCreate(t,entry,arg,stack_mem,stack_sz,prio,order[next.fetch_add(1)%3]);
    return R_SUCCEEDED(rc)?rc:__real_threadCreate(t,entry,arg,stack_mem,stack_sz,prio,-2);
}
}
#endif
