#pragma once

//CANSERVERDATA
#include <QtCore>
struct CanServerData
{
	int ID;
	int DevID;
	int DataLen;
	char Data[128];
};


class ICanServerDataListener
{
public:
	virtual void OnMsgData(CanServerData data) = 0 ;

        virtual void OnMsgData(CanServerData data,int nDomain,QString strTopic)
        {}

};

class ICanServerData
{
public:

	//注册订阅者
	virtual bool registListener(ICanServerDataListener* pListener) = 0 ;

	//发布信息
	virtual bool publishMessage(const CanServerData& data) = 0 ;
};

extern "C" Q_DECL_EXPORT ICanServerData* getCanServerDataInter(int nDomainID,bool allowLose = true);
