#ifndef OCRRECOGNITIONWIDGET_H
#define OCRRECOGNITIONWIDGET_H

#include <QWidget>
#include "IUiPluginBase.h"
#include "Comm/CustomMoveWidget.h"
#include "Comm/definestruct.h"
#include "ImageDeal/imagewidget.h"
#include "ImageDeal/image_downloader.h"
#include <QTimer>
#include "BackendDataExport.h"
#include "IBackendData.h"
#include "ISimulatorData.h"
#include "SimulatorDataExport.h"

class QValidator;

namespace Ui {
class OcrRecognitionWidget;
}

class OcrRecognitionWidget : public CustomMoveWidget,public IBackendDataListener,ISimulatorDataListener
{
    Q_OBJECT

public:
    explicit OcrRecognitionWidget(QWidget *parent = 0);
    ~OcrRecognitionWidget();

    /**
     * @brief initUi
     * 界面初始化函数。
     * @param pPlugin 界面框架指针
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
     * @brief initData
     * 插件数据初始化函数。
     * @param void
     * @return 无返回值
     */
    void initData();

    /**
     * @brief resetQss
     * 切肤函数。
     * @param strStyleName(day:白天  night:夜晚)
     * @return 无返回值
     */
    virtual void resetQss(const QString& strStyleName);

    /**
     * @brief registerProtocols
     * 虚函数的具体实现，初始化插件信号注册函数。
     * @param strStyleName
     * @return 无返回值
     */
    virtual void registerProtocols();

    /**
     * @brief loadQssStyleFile
     * 给予UI界面控件设置Qss图片样式。
     * @param void
     * @return 无返回值
     */
    void loadQssStyleFile();

    /**
     * @brief setLineEditUseEdit
     * 设置QLineEdit是否可编辑
     * @param void(bStatus为true：可编辑 false:不可编辑)
     * @return 无返回值
     */
    void setLineEditUseEdit(bool bStatus);

    /**
     * @brief loadPictureToUi
     * 加载图片信息至界面显示
     * @param strPictureName(图片名称)
     * @return 无返回值
     */
    void loadPictureToUi(QString strPictureName);

    /**
     * @brief setOcrTargetInfoToLineEdit
     * 将目标信息显示到界面
     * @param dData(目标详情信息)
     * @return 无返回值
     */
    void setOcrTargetInfoToLineEdit(TargetInfo dData);

    /**
     * @brief XlhIsFull
     * 判断序列号是否已满
     * @param void
     * @return void
     */
    void XlhIsFull();

    /**
     * @brief setInvalidData
     * 将UI界面显示的数据置为无效
     * @param void
     * @return void
     */
    void setInvalidData();

    /**
     * @brief peekNextTargetNumber 预览当前协议的默认目标编号：002使用纯序号，其他协议使用MMdd+4位序号。
     *        033、002、HL等协议分别保存序号；换日则当前协议从 1 起。
     */
    QString peekNextTargetNumber();

    /** @brief 上报成功后按当天规则占用序号并写入 config/OcrRecognition.ini */
    void commitTargetNumberSerial();

    /** @brief 从本地配置读取已用序号和对应日期 */
    void loadTargetNumberSerial();

    /** @brief 将当前序号和日期写入本地配置 */
    void saveTargetNumberSerial();

    /** @brief 协议发生切换时，载入新协议独立保存的序号 */
    void syncTargetNumberProtocol();

    /** @brief 根据当前协议刷新目标编号输入限制 */
    void updateTargetNumberValidator();

    /** @brief 校验当前协议下的目标编号 */
    bool isTargetNumberValid(const QString &targetNumber) const;

    /**
     * @brief showAndHidePushStatus
     * 显隐推流状态
     * @param bShow(true:显示推流状态 false:隐藏推流状态)
     * @return void
     */
    void showAndHidePushStatus(bool bShow);

    /**
    * @brief onMsgData 函数
    * 接收数据
    * @param data 前后端自定义协议 。
    * @return 无返回值
    */
    void OnMsgData(BackendData data);

    /**
    * @brief onMsgData 函数
    * 接收数据
    * @param data 前后端自定义协议 。
    * @return 无返回值
    */
    void OnMsgData(SimulatorData data);

    /**
     * @brief dealTargetTrack 处理目标信息 上报1401K 绘制航迹
     * @param cmd   0 清空 1画线 2画点
     * @param lon  经度
     * @param lat 纬度
     * @param elev 高程
     */
    void dealTargetTrack(int cmd,double lon ,double lat,double elev);

public slots:
    /**
     * @brief slotFinishedForgetdeviceStatus
     * 从DeviceManagement插件接收获取设备状态,用于sse重连
     * @param strData（反馈消息内容）
     * @return void
     */
    void slotFinishedForgetdeviceStatus(QString strData,int statusCode);

    /**
     * @brief slotRecvTargetNum 接收TargetTable目标编号修改
     */
    void slotRecvTargetNum(int);

    /**
     * @brief slotSwitchHttp 从ParameterSet插件接收http通信地址切换
     * @param address
     */
    void slotSwitchHttp(QString address);

    /**
     * @brief slotPositionSwitch
     * @param statu
     */
    void slotPositionSwitch(bool statu);
protected slots:
    /**
     * @brief slotClose_Btn
     * 点击关闭按钮触发关闭弹窗
     * @param void
     * @return 无返回值
     */
    void slotClose_Btn();

    /**
     * @brief slotModify_Btn
     * 点击目标信息编辑按钮
     * @param bStatus(true:目标界面信息可编辑 false:目标界面信息不可编辑)
     * @return 无返回值
     */
    void slotModify_Btn(bool bStatus);

    /**
     * @brief slotReport_Btn
     * 点击目标信息上报按钮
     * @param void
     * @return 无返回值
     */
    void slotReport_Btn();

    /**
     * @brief slotDownloadCompleted
     * 图片下载完成
     * @param strPath(图片绝对路径)
     * @return 无返回值
     */
    void slotDownloadCompleted(QString strPath);

    /**
     * @brief slotloadFailed
     * 图片下载失败提示
     * @param strContent(提示内容)
     * @return 无返回值
     */
    void slotloadFailed(QString strContent);

    /**
     * @brief slotSendSSEInfo
     * 接收SSE推送的目标信息。
     * @param void
     * @return 无返回值
     */
    void slotSendSSEInfo(QString strEvent,QString strData);

    /**
     * @brief slotPluginMsg
     * 接收一级菜单下发的OCR识别消息
     * @param pMsg
     * @return 无返回值
     */
    void slotPluginMsg(pluginmsg pMsg);

    /**
     * @brief slotIdentify
     * 接收菜单消息目标识别开关
     * @param nType（菜单ID） nStatus(识别开关 1 关 2开)
     * @return void
     */
    void slotIdentify(int nType, int nStatus);

    /**
     * @brief slotIdentifyTimeout 定时器 本次下发超时
     */
    void slotIdentifyTimeout();

    /**
     * @brief slotDeviceOfflineTimeout 判断网络是否在线 重启SSE重连
     */
    void slotDeviceOfflineTimeout();

    /**
     * @brief slotZLMParPushStatus
     * 接收推流状态信息
     * @param status
     * @return 无返回值
     */
    void slotZLMParPushStatus(int status);

    /**
     * @brief slottextChanged 限制输入字节
     */
    void slottextChanged();

    /**
     * @brief slotReportPositionTo1401K 上报位置给1401K 绘制航迹
     */
    void slotReportPositionTo1401K();

signals:
    void signalSendOcrTargetInfo(TargetInfo);

    // 发送目标识别开关信息
    void signalIdentify(int nType, int nStatus);

    // 发送弹窗告警消息
    void signalOcrMessageInfoText(QString,QString,MessageType);

    /**
     * @brief signalSendTargetNum 向TargetTable发送目标编号
     */
    void signalSendTargetNum(int);

private:
    Ui::OcrRecognitionWidget *ui;
    IUiPluginBase *m_pPlugin = nullptr; // 界面插件指针
    ImageWidget* m_pImageWidget = nullptr; // 图片展示出来类 
    ImageDownloader* m_pImageDownloader; // 创建下载器实例
    QString m_strPicturePath;
    QString m_strUrl; // http通信url
    unsigned int m_CurrentNumber = 0; // 本地保存的目标编号序号（不含日期前缀）
    QString m_CurrentNumberDate; // 序号对应的日期 yyMMdd，换日则序号从 1 重计
    QString m_CurrentNumberProtocol; // 当前序号所属协议，协议之间相互独立
    QString m_LastCommittedTargetNumber; // 防止同一目标重复上报时重复占用序号
    QString m_LastCommittedTargetProtocol; // 重复判断同时区分协议
    QValidator *m_targetNumber002Validator = nullptr;
    QValidator *m_targetNumberDefaultValidator = nullptr;
    QTimer *m_pIdentify=nullptr;    //定时器 用于本次下发超时或反馈后，进行下次下发
    QTimer *m_pDeviceOffline = nullptr;//定时器 判断网络是否在线 重启SSE重连
    QTimer *m_pReportPosition = nullptr;//定时器 上报位置给1401K 绘制航迹
    IBackendData *m_pBackendData = nullptr;//给1401K发送数据 绘制航迹
    ISimulatorData *m_pISimulatorData = nullptr;//给1401K QB发送数据 绘制航迹
    bool bIdentify=true;  //当前下发OCR识别状态 true可以下发 false不可以下发
    int m_pushStatus = -1; //推流状态，默认为-1（无效）
    bool m_bTrackSwitch=false;//位置上报开关
    bool m_bSignalPoint=true;//单个点位置上报
    double m_la=0.0;

};

#endif // OCRRECOGNITIONWIDGET_H
