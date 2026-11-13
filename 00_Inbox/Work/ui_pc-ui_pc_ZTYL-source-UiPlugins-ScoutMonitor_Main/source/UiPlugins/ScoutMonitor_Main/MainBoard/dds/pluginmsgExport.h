#pragma once
#include "Ipluginmsg.h"
#ifdef _WIN32
#include "WinSock2.h"
#include "Windows.h"
#else
#include "dlfcn.h"
#endif
//传入域ID
inline Ipluginmsg *getpluginmsgInstance(int nDomainId,std::string relativePath,bool allowLose = true)
{ 
#ifdef _WIN32
#ifdef DEBUG
    std::string path = relativePath + "/pluginmsgd.dll";
#else
    std::string path = relativePath + "/pluginmsg.dll";
#endif
    size_t size = path.length();
    wchar_t *buffer = new wchar_t[size+1];
    MultiByteToWideChar(CP_ACP,0,path.c_str(),size,buffer,size*sizeof(wchar_t));
    buffer[size] = 0;
    HMODULE model = LoadLibrary(buffer);
    typedef Ipluginmsg*(*InitFunc)(int,bool);
    delete buffer;
    if(model)
    {
        InitFunc initFun = (InitFunc)GetProcAddress(model,"getpluginmsgInter");
        return initFun(nDomainId, allowLose);
    }
#else
#ifdef DEBUG
    std::string path = relativePath + "/libpluginmsgd.so";
#else
    std::string path = relativePath + "/libpluginmsg.so";
#endif
    void *libm_handle = NULL;
    libm_handle = dlopen(path.c_str(),RTLD_LAZY);
    typedef Ipluginmsg*(*InitFunc)(int,bool);
    if(libm_handle)
    {
        InitFunc initFun = (InitFunc)dlsym(libm_handle,"getpluginmsgInter");
        return initFun(nDomainId, allowLose);
    }
#endif
    return nullptr;
}
