#pragma once
#include "ICanServerData.h"
#include <QLibrary>
#include <QApplication>
#include <QDebug>
//传入域ID
inline ICanServerData *getCanServerDataInstance(int nDomainId,QString relativePath,bool allowLose = true)
{
#ifdef _WIN32
#ifdef DEBUG
    QLibrary lib(relativePath + "/CanServerDatad.dll");
#else
    QLibrary lib(relativePath + "/CanServerData.dll");
#endif
    typedef ICanServerData*(*InitFunc)(int,bool);
    InitFunc initFun = (InitFunc)lib.resolve("getCanServerDataInter");
    if(initFun)
    {
        return initFun(nDomainId, allowLose);
    }
    return nullptr;
#else
#ifdef DEBUG
    QLibrary lib(relativePath + "/libCanServerData.so");
#else
    QLibrary lib(relativePath + "/libCanServerData.so");
#endif
    typedef ICanServerData*(*InitFunc)(int,bool);
    InitFunc initFun = (InitFunc)lib.resolve("getCanServerDataInter");
    if(initFun)
    {
        return initFun(nDomainId, allowLose);
    }
    return nullptr;
#endif
}
