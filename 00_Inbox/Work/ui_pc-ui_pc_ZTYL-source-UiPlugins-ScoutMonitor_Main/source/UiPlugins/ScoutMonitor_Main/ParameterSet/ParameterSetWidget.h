/*
* File：ParameterSetWidget.h
* Author：马庆超
* Created：2024-06-01
* Last Modified：2024-06-01
* Description：该文件用于实现参数设置样式及功能下发
*/


#ifndef PARAMETERSETWIDGET_H
#define PARAMETERSETWIDGET_H

#include <QWidget>
#include "IUiPluginBase.h"
#include "GenericComponent/ParaContentWidget.h"
#include "GenericComponent/ModelContentWidget.h"
#include "GenericComponent/OcrRecognitionSetWidget.h"
#include "GenericComponent/ScreenPushSetWidget.h"
#include "GenericComponent/NetworkSetWidget.h"
#include "GenericComponent/OtherSetWidget.h"
#include "Comm/CustomMoveWidget.h"
#include "BackendDataExport.h"
#include "IBackendData.h"
#include <QPointer>

namespace Ui {
class ParameterSetWidget;
}

class QButtonGroup;

class ParameterSetWidget : public CustomMoveWidget,public IBackendDataListener
{
    Q_OBJECT

public:
    explicit ParameterSetWidget(QWidget *parent = 0);
    ~ParameterSetWidget();

    /**
     * @brief initUi
     * 界面初始化函数。
     * @param pPlugin 界面框架指针
     * @return 无返回值
     */
    void initUi(IUiPluginBase *pPlugin);

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
     * @param void
     * @return 无返回值
     */
    virtual void resetQss(const QString& strStyleName);

    /**
     * @brief registerProtocols
     * 虚函数的具体实现，初始化插件信号注册函数。
     * @param void
     * @return 无返回值
     */
    virtual void registerProtocols();

    /**
     * @brief addWidget
     * 将参数和模型算法两个参数设置主界面按钮对应的UI界面加到垂直布局，根据点击不同按钮显示不同界面。
     * @param name（加入到布局的界面别名） widget（接入到布局的UI界面指针）
     * @return 无返回值
     */
    void addWidget(QString name,QWidget *widget);

    /**
     * @brief loadQssStyleFile
     * 设置主设置界面Qss样式。
     * @param void
     * @return 无返回值
     */
    void loadQssStyleFile();

    /**
    * @brief onMsgData 函数
    * 接收数据
    * @param data 前后端自定义协议 。
    * @return 无返回值
    */
    void OnMsgData(BackendData data);

public slots:
    /**
     * @brief slotRecQPointFromMianBoard 从MianBoard插件接收位置信息
     */
    void slotRecQPointFromMianBoard(QPoint);

protected slots:
    /**
     * @brief slotPara_pushButton
     * 点击参数按钮执行的槽函数(按钮高亮，参数UI界面显示)。
     * @param void
     * @return 无返回值
     */
    void slotPara_pushButton();

    /**
     * @brief slotModel_pushButton
     * 点击模型算法按钮执行的槽函数(按钮高亮，模型算法UI界面显示)。
     * @param void
     * @return 无返回值
     */
    void slotModel_pushButton();

    /**
     * @brief slotCloseUi
     * 点击设置界面中的关闭按钮，界面进行隐藏。
     * @param void
     * @return 无返回值
     */
    void slotCloseUi();

    /**
     * @brief slotListClicked
     * 根据点击加入到布局界面的按钮，显示对应界面。
     * @param name(加入到布局的界面别名)
     * @return 无返回值
     */
    void slotListClicked(QString name);

    /**
     * @brief slotPluginMsg
     * 接收中转信号槽消息。
     * @param pMsg(中转信息槽参数)
     * @return 无返回值
     */
    void slotPluginMsg(pluginmsg pMsg);

    /**
     * @brief slotIdentifyRespons
     * 识别结果反馈。
     * @param nType(1:OCR识别 2:目标识别) nStatus(1:失败 2:成功)
     * @return 无返回值
     */
    void slotIdentifyRespons(int nType,int nStatus);

    /**
     * @brief slotSwitchBigScreen  接收大屏切换信号，并转发zmq
     * @param json  小屏窗口号
     */
    void slotSwitchBigScreen(QJsonObject json);

    /**
     * @brief slotUIBtnClicked 界面切换
     * @param index 0流媒体 1桌面推流 2网口设置 3OCR设置 4模型算法
     */
    void slotUIBtnClicked(int index);

    /**
     * @brief slotVideoReportControl 转发其他设置中的视频上报启停请求
     */
    void slotVideoReportControl(int state);

    /**
     * @brief slotBackendData 处理大屏切换反馈数据
     */
    void slotBackendData(BackendData);

signals:
    // 点击参数按钮和模型算法按钮发送的信号
    void signalListClicked(QString);

    // 发送消息至MessageBox插件
    void signalMessageInfoText(QString,QString,MessageType);

    // 中转信号
    void signalPluginMsg(pluginmsg);

    // 发送目标识别开关信息
    void signalIdentify(int nType, int nStatus);

    // 转发大屏切换反馈数据
    void signalRecBackendData(BackendData data);

    /**
     * @brief signalToVideoProcessFFmpeg 发送拉流参数给VideoProcessFFmpeg插件
     */
    void signalToVideoProcessFFmpeg(QString);

    /**
     * @brief signalZLMParPushStatus 发送推流状态
     * @param status 推流状态 0-推流完成 1-推流准备中 2-推流错误
     * @param reason 错误原因  推流错误情况下有值，其他情况下为空字符串
     */
    void signalZLMParPushStatus(int status,QString reason);


private:
    Ui::ParameterSetWidget *ui;
    IUiPluginBase *m_pPlugin = nullptr; //界面插件指针
    ParaContentWidget* m_pParaContentWidget = nullptr; //桌面推流UI界面指针
    ModelContentWidget* m_pModelContentWidget = nullptr; //模型算法UI界面指针
    OcrRecognitionSetWidget* m_pOcrRecognitionSetWidget = nullptr; //Ocr识别区域设置
    ScreenPushSetWidget* m_pScreenPushSetWidget = nullptr; //屏幕推流UI界面
    NetworkSetWidget* m_pNetworkSetWidget = nullptr; //网口设置UI界面
    OtherSetWidget* m_pOtherSetWidget = nullptr; //其他设置UI界面
    QPointer<QWidget> m_pDeviceInfoWidget; //嵌入的设备信息插件界面（所有权仍归设备插件）
    IBackendData *m_pBackendData = nullptr;//后端发送前端
    QMap<QString,QWidget *> m_widgetMap; //用于存储UI界面指针、算法UI界面指针及其对应的界面别名
    QButtonGroup *m_pButtonGroup = nullptr; //设置页签按钮组
    int m_currentPageIndex = 0; //当前显示的设置页

};

#endif // PARAMETERSETWIDGET_H
