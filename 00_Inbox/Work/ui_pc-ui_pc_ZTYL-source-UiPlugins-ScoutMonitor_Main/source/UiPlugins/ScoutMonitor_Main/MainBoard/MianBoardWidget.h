#ifndef MIANBOARDWIDGET_H
#define MIANBOARDWIDGET_H

#include <IUiPluginBase.h>
#include <QWidget>
#include <QTimer>
#include <QLibrary>
#include <QKeyEvent>



typedef struct pluginmsg{
    int pluginid;           //发送的时候填上该值，该值是自己的插件号，当接收的信息插件号是自己时，表示自己发自己，返回,不作处理
    int type;               //下一级类型：1,一级菜单 2,二级菜单 3弹窗 4数据插件 ，可扩展
    int curtype;            //当前类型：1,一级菜单 2,二级菜单 3弹窗 4数据插件 ，可扩展
    int uppermenuid;        //0表示没有上级菜单，即一级菜单 其他值时上级菜单编号
    char uppermenuname[64]; //上级菜单名，比如“飞行控制”
    int id;                 //当前功能编号
    char name[64];          //当前功能名
    char msg[128];          //附加信息,返回信息时使用
    int  len;               //消息长度
    int state;              //操作状态,返回状态信息时使用
}pluginmsg;

enum VideoStatus
{
    Status_Idle = 0,        // 空闲（未启动）
    Status_Connecting,      // 正在连接 / 拉流中
    Status_Playing,         // 正在播放
    Status_Reconnecting,    // 断线重连中
    Status_Failed,          // 拉流失败
    Status_Stopped,         // 停止
};



namespace Ui {
class MianBoardWidget;
}

class MianBoardWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MianBoardWidget(QWidget *parent = 0);
    ~MianBoardWidget();
    //初始化
    void init(IUiPluginBase *pPlugin);
    void initData();
    //切换皮肤
    virtual void resetQss(const QString& strStyleName);

    void registerProtocols();
    void closeWidget();
signals:

    void signalMessageText(QString,QString,QString,QString,QString);
    void SignalCloseSoft();//关闭软件
    void signalConnectState(char);
    void  signalPluginMsg(pluginmsg);

    /**
     * @brief signalQPointToParameterSet 向ParameterSet插件发送位置信息
     */
    void signalQPointToParameterSet(QPoint);


protected:
    void resizeEvent(QResizeEvent *event);
    bool eventFilter(QObject *watched, QEvent *event);
    void showEvent(QShowEvent *event); //add ydf
private slots:
    void slotTimeout();
    void sltConfirmClicked(QString text);

    /**
     * @brief slotDeviceStatusShow
     * 设备在线离线显示
     * @param online true:在线 false：离线
     */
    void slotDeviceStatusShow(bool online);

    /**
     * @brief slotHDMIINStatusShow
     * HDMI_IN在线离线显示
     * @param online true:在线 false：离线
     */
    void slotHDMIINStatusShow(bool online);

    /**
     * @brief slotPushVideoStatusShow 接收推流状态
     * @param status 推流状态 0-推流完成 1-推流准备中 2-推流错误
     * @param reason 错误原因  推流错误情况下有值，其他情况下为空字符串
     */
    void slotPushVideoStatusShow(int status,QString reason);

    /**
     * @brief slotPushVideoStatusShow 接收拉流状态
     * @param status 拉流状态 0-拉流完成 1-拉流准备中 2-拉流错误
     * @param reason 错误原因  拉流错误情况下有值，其他情况下为空字符串
     */
    void slotPullVideoStatusShow(VideoStatus status,QString reason);


private:
    //加载qss
    void loadQss();
private:
    Ui::MianBoardWidget *ui;
    IUiPluginBase *m_pPlugin = nullptr;
    QTimer *m_pTimer = nullptr;
    int m_pushVideoStatus=-1; //推流视频状态 0-推流完成 1-推流准备中 2-推流错误
    QString m_pushVideoreason="";//推流错误原因
    int m_pullVideoStatus=-1; //拉流视频状态 0-拉流完成 1-拉流准备中 2-拉流错误
    QString m_pullVideoreason="";//拉流错误原因
    bool m_HDMIINstatus=false;//HDMI_IN状态
    bool m_DealStatus=false;//处理状态
    bool m_DeviceStatus=false;//设备状态
};

#endif // MIANBOARDWIDGET_H
