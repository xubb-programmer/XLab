/**
 * @brief The NetworkSetWidget class
 * Author:hante
 * Created：2025-11-21
 * Last Modified：2025-11-21
 * Description：实现网口设置主界面功能（cpp的编码为UTF-8-BOM，解决“接口名”“接口状态”字符串编译报C2001问题 ）
 */


#ifndef NETWORKSETWIDGET_H
#define NETWORKSETWIDGET_H

#include <QWidget>
#include "IUiPluginBase.h"
#include "definestruct.h"
#include "Comm/httpworker.h"
#include "NetWorkItemWidget.h"

namespace Ui {
class NetworkSetWidget;
}

class NetworkSetWidget : public QWidget
{
    Q_OBJECT

public:
    explicit NetworkSetWidget(QWidget *parent = 0);
    ~NetworkSetWidget();

    /**
     * @brief initUi
     * 插件数据初始化交互
     * @param void
     * @return 无返回值
     */
    void initUi(IUiPluginBase *plugin);

    /**
     * @brief initCfg
     * 读取配置文件获取发送时的基础UI
     * @param void
     * @return 无返回值
     */
    void initCfg();

    /**
     * @brief initData
     * 插件数据初始化交互
     * @param void
     * @return 无返回值
     */
    void initData();

    /**
     * @brief loadQssStyle
     * 设置界面Qss初始化样式表
     * @param void
     * @return void
     */
    void loadQssStyle();

signals:
    // 发送消息至MessageBox插件
    void signalMessageInfoText(QString,QString,MessageType);

    // 下发网络设置数据
    void signalSendNetSetPara(QString, QJsonObject);
    //向sse发送重连信号
    void signalOnLineToSSE(QString);
    /**
     * @brief signalproductNum 发送当前的连接产品编号
     * @param productNum
     */
    void signalproductNum(QString productNum);

private slots:
    /**
     * @brief slotSendNetsetData
     * 接收处理“下发”按钮下发的网络设置数据，并通过http转发
     * @param netdata 网络设置数据
     * @return void
     */
    void slotSendNetsetData(NetSetData netdata);

    /**
     * @brief slotNetWorkSetTimeout 判断网口设置是否成功 10s未收到回执默认成功
     */
    void slotNetWorkSetTimeout();

    /**
     * @brief slotHttpTimeout 判断HTTP是否正常 5s未收到状态默认不在线
     */
    void slotHttpTimeout();

public slots:

    /**
     * @brief slotFinishedForgetdeviceStatus
     * 从DeviceManagement插件接收获取设备状态
     * @param strData（反馈消息内容）
     * @return void
     */
    void slotFinishedForgetdeviceStatus(QString strData,int statusCode);

    /**
     * @brief slotFinishedForNetWorkSet
     * 接收网口设置http反馈
     * @param strData（反馈消息内容）
     * @return void
     */
    void slotFinishedForNetWorkSet(QString strData);

private:
    Ui::NetworkSetWidget *ui;
    IUiPluginBase *m_pPlugin = nullptr;//基类界面
    NetWorkItemWidget  *m_pNetWorkItem1=nullptr;//网口1界面
    NetWorkItemWidget  *m_pNetWorkItem2=nullptr;//网口2界面
    QString m_strUrl = "10.100.24.230"; // http通信基础Url
    NetSetData m_netData1;//网口1信息
    NetSetData m_netData2;//网口2信息
    NetSetData m_setNetData;//当前设置的网络信息
    QTimer *m_pNetWorkSet = nullptr;//定时器 判断网口设置是否成功
    QTimer *m_pNethttp = nullptr;//定时器 判断网络是否在线
    bool bIshttp=false;//判断网络是否在线
};

#endif // NETWORKSETWIDGET_H
