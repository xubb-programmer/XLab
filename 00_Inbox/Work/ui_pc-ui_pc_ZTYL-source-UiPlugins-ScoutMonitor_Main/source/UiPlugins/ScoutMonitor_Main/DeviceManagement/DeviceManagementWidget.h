#ifndef DEVICEMANAGEMENTWIDGET_H
#define DEVICEMANAGEMENTWIDGET_H

#include <QWidget>
#include "IUiPluginBase.h"
#include "ToolClass/HardwareInterface.h"
#include "Comm/CustomMoveWidget.h"
#include "Comm/httpworker.h"
#include <QTimer>
#include <QVector>

typedef struct pluginmsg
{
    int pluginid; //发送的时候填上该值，该值是自身插件号，当接收的信息插件号是自身时，表示插件接收到自身发送的信号，返回,不作处理
    int type; //下一级类型：1,菜单 2弹窗 3数据插件
    int curtype; //当前类型：1,菜单 2弹窗 3数据插件
    int uppermenuid; //0表示没有上级菜单，即一级菜单 其他值时上级菜单编号
    char uppermenuname[64];//上级菜单名，比如“雷达工作”
    int id; //当前功能编号
    char name[64]; //当前功能名
    char msg[128]; //附加信息,返回信息时使用
    int  len; //消息长度
    int state;	//操作状态,返回状态信息时使用(功能确认弹窗,1:取消，2:确定)
}pluginmsg;

typedef struct InterFacesData
{
    QString InterfaceName; //接口名称
    bool IsOnline; // 在线状态
    QString TransSpeed; //传输速率
}InterFacesData;

typedef struct productInfo
{
    QString productName = QString::fromUtf8(u8"智图影链"); // 产品名称
    QString productNum = "--"; // 产品编号
    QString productTime = "--"; //生产日期
}productInfo;

namespace Ui {
class DeviceManagementWidget;
}

class DeviceManagementWidget : public CustomMoveWidget
{
    Q_OBJECT

public:
    explicit DeviceManagementWidget(QWidget *parent = 0);
    ~DeviceManagementWidget();

    /**
     * @brief initUi
     * 界面初始化
     * @param pPlugin(插件指针)
     * @return 无返回值
     */
    void initUi(IUiPluginBase *pPlugin);

    /**
     * @brief initCfg
     * 读取配置文件获取发送时的基础UI
     * @param void
     * @return 无返回值
     */
    void initCfg();

    /**
     * @brief initComponent
     * 初始化控制组成
     * @param void
     * @return 无返回值
     */
    void initComponent();

    /**
     * @brief initData
     * 初始化数据
     * @param void
     * @return 无返回值
     */
    void initData();

    /**
     * @brief resetQss
     * 切换皮肤
     * @param void
     * @return 无返回值
     */
    virtual void resetQss(const QString& strStyleName);

    /**
     * @brief registerProtocols
     * 注册连接
     * @param void
     * @return 无返回值
     */
    virtual void registerProtocols();

    /**
     * @brief loadQssStyleFile
     * 设置Qss消息
     * @param void
     * @return 无返回值
     */
    void loadQssStyleFile();

    /**
     * @brief setDeviceOutline
     * 设备离线
     * @param void
     * @return 无返回值
     */
    void setDeviceOutline();

    /**
     * @brief setMenuButtonStatus
     * 设置一级菜单按钮状态。
     * @param nId(按钮编号) nStatus(1:未开启 2:开启种)
     * @return 无返回值
     */
    void setMenuButtonStatus(int nId,int nStatus);

public slots:

    /**
     * @brief slotSwitchHttp 从ParameterSet插件接收http通信地址切换
     * @param address
     */
    void slotSwitchHttp(QString address);

protected slots:
    /**
     * @brief slotCloseUi
     * 关闭弹窗
     * @param void
     * @return 无返回值
     */
    void slotCloseUi();

    /**
     * @brief slotPluginMsg
     * 中转信号槽处理
     * @param pMsg(消息内容)
     * @return 无返回值
     */
    void slotPluginMsg(pluginmsg pMsg);

    /**
     * @brief slotDeviceQTimerOut
     * 定时发送设备状态查询指令
     * @param void
     * @return 无返回值
     */
    void slotDeviceQTimerOut();

    /**
     * @brief slotCheckOutLineQTimerOut
     * 超时将设备置为离线状态
     * @param void
     * @return 无返回值
     */
    void slotCheckOutLineQTimerOut();

    /**
     * @brief slotFinishedForDeviceStatus
     * http请求后的返回信息处理
     * @param void
     * @return 无返回值
     */
    void slotFinishedForDeviceStatus(QString strData,int statusCode);

    /**
     * @brief slotFlushUi
     * 刷新界面显示信息
     * @param void
     * @return 无返回值
     */
    void slotFlushUi();

    /**
     * @brief slotDeviceStatusShow
     * 刷新设备在线状态
     * @param bStatus(设备在线状态)
     * @return 无返回值
     */
    void slotDeviceStatusShow(bool bStatus);

signals:
    void signalGetDeviceInfo(QString);
    void signalFlushUi();

    //向MainBoard插件发送设备状态
    void signalDeviceStatusShow(bool);
    //向MainBoard插件发送HDMI_IN状态
    void signalHDMIINStatusShow(bool);
    // 中转信号
    void signalPluginMsg(pluginmsg);

    /**
     * @brief signalDeviceStatusToParameterSet
     * 将获取设备状态数据发送给ParameterSet插件
     * @param void
     * @return 无返回值
     */
    void signalDeviceStatusToParameterSet(QString strData,int statusCode);

private:
    /** 按标签实际宽度在末尾省略设备名称，并保留完整悬浮提示。 */
    void updateProductNameDisplay(const QString &fullName);

    Ui::DeviceManagementWidget *ui;
    IUiPluginBase *m_pPlugin = nullptr; //界面插件指针
    QTimer* m_pDeviceQTimer = nullptr; // 定时发送设备状态查询信息
    QTimer* m_pCheckOutLineQTimer = nullptr; // 定期检测让设备置为离线状态
    QVector<InterFacesData> m_vInterFacesData;//设备接口信息
    productInfo m_productInfo;//产品信息
    HttpWorker *m_worker; // Http数据发送接口类
    QString m_strUrl = ""; // http通信基础Url
    HardwareInterface* m_HDMI = nullptr; //HDMI界面指针
    HardwareInterface* m_HDMI1 = nullptr; //HDMI1界面指针
    HardwareInterface* m_HDMI2 = nullptr; //HDMI2界面指针
    HardwareInterface* m_Eth = nullptr; //以太网1界面指针
    HardwareInterface* m_Eth1 = nullptr; //以太网2界面指针
    HardwareInterface* m_Typc = nullptr; //Type界面指针
    bool m_Recognition = false; //记录识别开关状态

};

#endif // DEVICEMANAGEMENTWIDGET_H
