#pragma once
#include "IVideoWinChange.h"
#include <QLibrary>
#include <QApplication>
#include <QDebug>
//传入域ID
inline IVideoWinChange *getVideoWinChangeInstance(int nDomainId,QString relativePath,bool allowLose = true)
{
#ifdef DEBUG
    QLibrary lib(relativePath + "/VideoWinChanged.dll");
#endif
#ifdef NDEBUG
    QLibrary lib(relativePath + "/VideoWinChange.dll");
#endif
    typedef IVideoWinChange*(*InitFunc)(int,bool);
    InitFunc initFun = (InitFunc)lib.resolve("getVideoWinChangeInter");
    if(initFun)
    {
        return initFun(nDomainId, allowLose);
    }
    return nullptr;
}
