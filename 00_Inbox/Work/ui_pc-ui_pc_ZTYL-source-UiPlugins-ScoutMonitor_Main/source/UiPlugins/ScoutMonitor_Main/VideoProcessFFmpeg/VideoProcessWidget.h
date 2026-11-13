/*
* File：VideoProcessWidget.h
* Author：huangjin
* Created：2025-08-08
* Last Modified：2025-08-12
* Description：该文件用于实现视频控制的接口
*/
#ifndef VIDEOPROCESSWIDGET_H
#define VIDEOPROCESSWIDGET_H

#include <QWidget>
#include <QImage>
#include <FFmpegWorker.h>
#include "IUiPluginBase.h"
#include <QSettings>
#include <QLabel>
#include "SnapShotShowWidget.h"
#include "utils/DiskSpaceChecker.h"
#include "utils/ScreenStreamPusher.h"

enum CommandType
{
    Command_Relay = 3,
    Command_Snapshot= 4,
    Command_Record= 5,
    Command_ScreenPush = 8
};
enum WorkerState{
    State_Idle,         //未播放
    State_Pulling,      //拉流中
    State_Playing,      //播放中
    State_Stopping,     //正在stop
    State_Reconnecting, //断线重连中
    State_Error         //错误
};
typedef struct pluginmsg{
    int pluginid;           //发送的时候填上该值，该值是自身插件号，当接收的信息插件号是自身时，表示插件接收到自身发送的信号，返回,不作处理
    int type;               //下一级类型：1,菜单 2弹窗 3数据插件
    int curtype;            //当前类型：1,菜单 2弹窗 3数据插件
    int uppermenuid;        //0表示没有上级菜单，即一级菜单 其他值时上级菜单编号
    char uppermenuname[64]; //上级菜单名，比如“飞行控制”
    int id;                 //当前功能编号
    char name[64];          //当前功能名
    char msg[128];          //附加信息,返回信息时使用
    int  len;               //消息长度
    int state;              //操作状态,返回状态信息时使用
}pluginmsg;

namespace Ui {
class VideoProcessWidget;
}

class VideoProcessWidget : public QWidget
{
    Q_OBJECT

public:
    explicit VideoProcessWidget(QWidget *parent = 0);
    ~VideoProcessWidget();
    /**
    * @brief initData 函数
    * 初始化
    * @param
    * @return 空。
    * */
    void initUi(IUiPluginBase *pPlugin);
    void initData();
    virtual void resetQss(const QString& strStyleName);
    virtual void registerProtocols();

public slots:
    void slotSetPStr(QString url);
    /**
    * @brief sltResetPullStream 函数
    * 接收参数设置重置拉流地址信号处理
    * @QString url 拉流地址
    * @return 空。
    * */
    void sltReSetPullStream(QString& url);

private slots:
    void sltsignalPluginMsg(pluginmsg pMsg);
    /** 处理复用 MessageBox 的“关闭投屏”确认结果。 */
    void sltConfirmClicked(QString text);
    /**
    * @brief sltErrorOccured 函数
    * 处理错误信号
    * @ErrorType value 详见枚举定义
    * @return 空。
    * */
    void sltErrorOccured(ErrorType value);
    /**
    * @brief sltHideTip 函数
    * 隐藏拉流提示
    * @return 空。
    * */
    void sltHideTip();
    /**
    * @brief sltStartPlayClicked 函数
    * 开始播放按钮控制接口
    * @param
    * @return 空。
    * */
    void sltStartPlayClicked();

    /**
    * @brief sltStopPlayClicked 函数
    * 停止播放按钮控制接口
    * @param
    * @return 空。
    * */
    void sltStopPlayClicked();

    /**
    * @brief sltSnapshotClicked 函数
    * 截图按钮控制接口
    * @param
    * @return 空。
    * */
    void sltSnapshotClicked();


    /**
    * @brief sltRecordVideoClicked 函数
    * 录制控制按钮控制接口
    * @param
    * @return 空。
    * */
    void sltRecordVideoClicked();

    /**
    * @brief sltRelayVideoClicked 函数
    * 视频上报控制按钮控制接口
    * @param
    * @return 空。
    * */
    void sltRelayVideoClicked();

    void sltScreenPushBtnClicked();

    void sltChangeBtnClicked();

    /**
    * @brief sltDoReconect 函数
    * 重连定时器超时接口
    * @param
    * @return 空。
    * */
    void sltDoReconect();

    void sltVideoStatusChanged(VideoStatus status, const QString &message);

    void sltVideoStatusTimerout();

    void sltLowDiskSpace();

    void sltRecoverFunc();

    void sltUpdateScreenPushStyle(int state);

signals:
    void signalStartPlay(const QString &url, const QVariant &winIdVar);

    void signalStopPlay();

    void signalPluginMsg(pluginmsg pMsg);

    void signalHintMessage(QString, QString, MessageType);

    /** 调用与软件退出相同的自定义双按钮确认弹窗。 */
    void signalMessageText(QString, QString, QString, QString, QString);

    /**
    * @brief signalPullVideoStatus 函数
    * 发送拉流状态
    * @param
    * VideoStatus
     {
     Status_Idle = 0,        // 空闲（未启动）
     Status_Connecting,      // 正在连接 / 拉流中
     Status_Playing,         // 正在播放
     Status_Reconnecting,    // 断线重连中
     Status_Failed,          // 拉流失败
     Status_Stopped,         // 停止
     };
    * @param message
    * @return 空。
    * */
    void signalPullVideoStatus(VideoStatus status, QString message);

private:
     //调度重连，保证多次调用只触发一次
    void scheduleReconnect(int delayMs);

    /**
    * @brief getParaValue 函数
    * 获取参数
    * @param groupName:参数所属组名称 paraKey 参数键
    * @return 无
    */
    QVariant getParaValue(QString groupName, QString paraKey);

    void recoverFunc();

public:
    Ui::VideoProcessWidget *ui;
    FFmpegWorker           *m_pFFmpegWorker;
    QThread                *m_workerThread;
    IUiPluginBase          *m_pPlugin = nullptr;
    std::unique_ptr<QSettings> m_pSettings;

    bool firstReconnect = true;
    QLabel *labelVideo;
    int testIndex = 0;
    QTimer *m_reconnectTimer;
    WorkerState m_workerState;
    SnapShotShowWidget *m_pShowSnapWgt = nullptr;
    VideoStatus m_currentStatus = Status_Idle;// 定时上报视频采集状态
    QString m_extraMsg = "";
    QTimer *m_pVideoStatusTimer  = nullptr;//  上报标志位
    bool m_relayHandFlag         = false;//  录像标志位
    bool m_recordHandFlag        = false;// 推流标志位

    ScreenStreamPusher *m_pScreenPusher = nullptr;

    QTimer *m_recoverFunTimer = nullptr;
};

#endif // VIDEOPROCESSWIDGET_H
