#pragma once

//PLUGINMSG
#include <string>
struct pluginmsg
{
	int pluginid;
	int type;
	int curtype;
	int uppermenuid;
	char uppermenuname[64];
	int id;
	char name[64];
	char msg[128];
	int len;
	int state;
};


class IpluginmsgListener
{
public:
	virtual void OnMsgData(pluginmsg data) = 0 ;

        virtual void OnMsgData(pluginmsg data,int nDomain,std::string strTopic)
        {}

};

class Ipluginmsg
{
public:

	//注册订阅者
	virtual bool registListener(IpluginmsgListener* pListener) = 0 ;

    //取消订阅
    virtual void unregistListener(IpluginmsgListener* pListener) = 0;

	//发布信息
	virtual bool publishMessage(const pluginmsg& data) = 0 ;
};
#ifdef _WIN32
extern "C" __declspec(dllexport) Ipluginmsg* getpluginmsgInter(int nDomainID,bool allowLose = true);
#else
extern "C" Ipluginmsg* getpluginmsgInter(int nDomainID,bool allowLose = true);
#endif
