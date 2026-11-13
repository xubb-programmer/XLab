#pragma once

//VIDEOWINCHANGE
#include <QtCore>
struct VideoWinChange
{
    short uId;
    short uLength;
    char cWinid;
    int iSendId;
    char cSrcId;
    char cSendTime[24];
};


class IVideoWinChangeListener
{
public:
    virtual void OnMsgData(VideoWinChange data) = 0 ;

        virtual void OnMsgData(VideoWinChange data,int nDomain,QString strTopic)
        {}

};

class IVideoWinChange
{
public:

    //???????
    virtual bool registListener(IVideoWinChangeListener* pListener) = 0 ;

    //???????
    virtual bool publishMessage(const VideoWinChange& data) = 0 ;
};

extern "C" Q_DECL_EXPORT IVideoWinChange* getVideoWinChangeInter(int nDomainID,bool allowLose = true);
