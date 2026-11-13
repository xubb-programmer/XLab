#ifndef SignalTransitWIDGET_H
#define SignalTransitWIDGET_H

#include <QWidget>
#include "MutualExclusive.h"
#include "CanServerDataExport.h"
#include <QDateTime>

struct WGstate
{
    //调平、状态应答
    bool bWG1 = false;
    bool bWG2 = false;
};

//chensen 20200410
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

class SignalTransitWidget :public QWidget ,public ICanServerDataListener
{
    Q_OBJECT
public:
    SignalTransitWidget(QWidget *parent = 0);

    void initData();

    //注册连接
    virtual void registerProtocols();

    //
    virtual void OnMsgData(CanServerData data);

public slots:

    //按钮被点击 -- 筛选一次操作后在执行
    void sltBtnClicked(QString name);
    void sltPluginMsg(pluginmsg msg);  //chensen 20200529

signals:
    //按钮被点击
    void btnClicked(QString);

    //状态互斥
    void notifyMutualChanged(QString);

    //桅杆状态
    void signalWGState(bool *);

    //统一互斥逻辑指针
    void signalInitMutual(void *p);

    //按钮被选中
    void btnSelectChanged(QString,bool);

    //发送弹窗提示
    void signalHintMessage(QString,QString);
	
	    //chensen 20200410 begin
    void signalPluginMsg(pluginmsg);
    void signalPermissionChanged(int,bool);
    //chensen 20200410 end

private:
    ICanServerData *m_pData = nullptr;
    WGstate m_WGstate;
    bool m_bWG = true;
    MutualExclusive m_MutualExclusive;//互斥状态
    QDateTime m_lastTime;//上一次弹窗响应时间
};

#endif // SignalTransitWIDGET_H
