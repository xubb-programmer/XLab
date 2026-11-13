#ifndef OTHERSETWIDGET_H
#define OTHERSETWIDGET_H

#include <QWidget>
#include "IUiPluginBase.h"
#include "definestruct.h"
#include <QSettings>
#include <QList>

namespace Ui {
class OtherSetWidget;
}
class OtherSetWidget : public QWidget
{
    Q_OBJECT

public:
    explicit OtherSetWidget(QWidget *parent = 0);
    ~OtherSetWidget();

    /**
     * @brief initUi
     * 插件初始化界面
     * @param plugin（插件指针）
     * @return 无返回值
     */
    void initUi(IUiPluginBase *plugin);

    /**
     * @brief initConfig
     * 插件初始化配置
     * @return 无返回值
     */
    void initConfig();

    /**
     * @brief carList 获取[Car]配置列表
     */
    const QList<CarInfo>& carList() const { return m_carList; }

    /**
     * @brief loadQssStyle
     * 设置组件样式
     * @param void
     * @return 无返回值
     */
    void loadQssStyle();

    /**
     * @brief sendParaValue 发送参数信息
     */
    void sendParaValue();
private slots:
    /**
     * @brief slotHttpSetClicked
     * HTTP通信地址配置
     * @param void
     * @return void
     */
    void slotHttpSetClicked(bool);
    /**
     * @brief slotOcrSetClicked
     * OCR截图路径配置
     * @param void
     * @return void
     */
    void slotOcrSetClicked(bool);
    /**
     * @brief slotSnaoShotSetClicked
     * 视频截图路径配置
     * @param void
     * @return void
     */
    void slotSnaoShotSetClicked(bool);
    /**
     * @brief slotRecordSetClicked
     * 视频录像路径配置
     * @param void
     * @return void
     */
    void slotRecordSetClicked(bool);
    /**
     * @brief slotTargetSetClicked
     * 目标上报地址配置
     * @param void
     * @return void
     */
    void slotTargetSetClicked(bool);
    /**
     * @brief slotVideoSetClicked
     * 视频上报地址配置
     * @param void
     * @return void
     */
    void slotVideoSetClicked(bool);

    /**
     * @brief slotVideoReportClicked 复用原视频上报按钮的启停控制
     */
    void slotVideoReportClicked(bool);

    /**
     * @brief slotCarSelect 车辆选择
     */
    void slotCarSelect(int );

public slots:
    /**
     * @brief slotcurrentDeviceIp
     * 接收设备选择 发送的IP信息
     * @return void
     */
    void slotcurrentDeviceIp(QString ip);

    /**
     * @brief slotVideoReportStateChanged 接收视频处理模块反馈的上报状态
     */
    void slotVideoReportStateChanged(int state);

signals:
    // 发送消息至MessageBox插件
    void signalMessageInfoText(QString,QString,MessageType);

    /**
     * @brief signalStreamPullAdress 发送客户端拉流参数
     * @param addresss 拉流地址
     */
    void signalStreamPullAdress(QString addresss);

    /**
     * @brief signalSwitchHttp 切换通信地址
     * @param addresss
     */
    void signalSwitchHttp(QString addresss);

    /**
     * @brief signalTargetReportAddress  目标上报地址ip port
     * @param ip
     * @param port
     */
    void signalTargetReportAddress(QString ip,int port);

    /**
     * @brief signalVideoReportControl 请求启动或停止视频上报（1启动，2停止）
     */
    void signalVideoReportControl(int state);

private:

    /**
     * @brief 从URL中提取IP地址
     * @param url URL字符串
     * @return 提取的IP地址，如果提取失败则返回空字符串
     */
    QString extractIpFromUrl(const QString &url);

    /**
     * @brief 解析视频URL
     * @param url 视频URL字符串
     * @return 包含IP和端口的结构体
     */
    UrlComponents parseVideoUrl(const QString &url);

    /**
     * @brief getParaValue 函数
     * 获取参数
     * @param groupName:参数所属组名称 paraKey 参数键
     * @return 无
     */
    QVariant getParaValue(QString groupName,QString paraKey);

    /**
     * @brief setParaValue 函数
     * 设置参数
     * @param groupName:参数所属组名称 paraKey 参数键 value 参数值
     * @return 无
     */
    void setParaValue(QString groupName,QString paraKey,QString value);

    /**
     * @brief selectPath 选择路径
     * @param path 当前路径
     * @return  新路径
     */
    QString selectPath(QString path);

    /**
     * @brief loadCarConfig 从communication.ini读取[Car]配置
     */
    void loadCarConfig();

    /**
     * @brief findCarIndexByName 按车辆名称查找索引
     */
    int findCarIndexByName(const QString &name) const;

private:
    Ui::OtherSetWidget *ui;
    IUiPluginBase *m_pPlugin = nullptr;//界面基类
    std::unique_ptr<QSettings> m_pSettingsCommunication;
    std::unique_ptr<QSettings> m_pSettingsVideoProcess;
    QString m_httpUrl="192.168.0.100:3000";//http通信地址
    QString m_httpUrlPort="3000";//http通信地址端口
    QString m_czIp = "192.168.0.5";//目标上报目的IP
    QString m_carType="033";//当前选中车辆名称 //0:1501 033 1:002车
    int m_czPort = 6100;//目标上报目的端口
    QString m_ocrPath = "D:/";//OCR图片存储路径
    QString m_outputUrl = "udp://10.1.2.12:5703?pkt_size=1316";//视频上报地址
    QString m_inputUrl="rtmp://192.168.0.100:1935/live/stream";//拉流视频地址
    QString m_snapshotPath ;//截图路径
    QString m_recordPath ;//录像路径
    QList<CarInfo> m_carList;//[Car]配置列表
    int m_videoReportState = 1;//视频上报按钮当前请求状态：1启动，2停止
};

#endif // OTHERSETWIDGET_H
