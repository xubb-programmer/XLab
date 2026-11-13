#ifndef TARGETTABLEWIDGET_H
#define TARGETTABLEWIDGET_H

#include <QWidget>
#include "IUiPluginBase.h"
#include "definestruct.h"
#include "Component/TargetItem.h"
#include "Comm/TargetInfoSocket.h"
#include "Comm/TcpComm.h"

/** 软件代号 **/
enum En_1501_Soft_Id
{
    EIDSS  = 0x64,  //以太网服务  100
    ZCZH   = 0x65,  //侦指软件    101
    QBCL   = 0x66,  //情报软件    102
    CSGD   = 0x67,  //车上光电软件 103
    CXGD   = 0x74,  //车下光电软件 116
    CSUAV  = 0x6E,  //车上无人机软件  110
    CXUAV  = 0x75,  //车下无人机软件  117
    Info_Process_Server = 0x77  //代表本服务  119
};

/** 信息类型**/
enum En_1501_Msg_Type
{
    ZCXX = 80,  //侦察信息    //0x50
    LDZDXX = 10,  //雷达炸点用的这个值，实际抓取
};

/** 信息代号 **/
enum En_1501_Msg_Code
{
    Uav_Target      = 69,    //无人机目标信息
    Uav_Burst_Point = 60,    //无人机炸点信息
    GD_Target       = 80,    //光电目标信息
    GD_Burst_Point  = 89,    //光电炸点信息
    LD_Target       = 81,    //雷达目标信息
    LD_Burst_Point  = 33,    //雷达炸点信息
    WRC_Target      = 84,    //无人车目标信息   - 应陈丽修改  84 修改为80

};

namespace Ui {
class TargetTableWidget;
}

class TargetTableWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TargetTableWidget(QWidget *parent = 0);
    ~TargetTableWidget();

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
     * @brief initConfig
     * 初始化配置
     */
    void initConfig();

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
     * @param strStyleName
     * @return 无返回值
     */
    virtual void registerProtocols();

    /**
     * @brief addTargetInfo
     * 添加目标信息。
     * @param void
     * @return 无返回值
     */
    void addTargetInfo(TargetInfo dData);

    /**
     * @brief updateTargetItemUI
     * 依据存储到容器中的目标卡片信息重新刷新界面显示。
     * @param void
     * @return 无返回值
     */
    void updateTargetItemUI();

    /**
     * @brief loadQssStyleFile
     * 给予UI界面控件设置Qss图片样式。
     * @param void
     * @return 无返回值
     */
    void loadQssStyleFile();

    /**
     * @brief sendTarget
     * 识别出的上报目标信息到车长席
     * @param info(1501目标结构体)
     * @return 无返回值
     */
    void sendTarget(UavTargetInfoWithObserver1501 info);

    /**
     * @brief sendBurstTarget
     * 识别出的炸点信息到车长席
     * @param info(1501炸点结构体)
     * @return 无返回值
     */
    void sendBurstTarget(BurstTargetInfoWithObserver1501 info);

    /**
     * @brief sendTargetToZCC
     * 识别出的上报目标信息到1501 033
     * @param info
     * @return 无返回值
     */
    void sendTargetTo15ZCC(TargetInfo info);

    /**
     * @brief sendTargetToZZT
     * 识别出的上报目标信息到002车
     * @param info
     * @return 无返回值
     */
    void sendTargetToZZT(TargetInfo info);

    /**
     * @brief sendTargetToZZC
     *  将目标经纬高打包为 TCP 帧并发送 ZZC
     * @param info 目标信息（经度、纬度、高程及时间等）
     */
    void sendTargetToZZC(TargetInfo info);

    /**
     * @brief sendTargetToMarineHL
     * (HL)目标上报：OutHeader19 + InnerHeader30 + TargetInfo_Marine，UDP 发送
     * @param info 目标列表中的目标信息
     */
    void sendTargetToMarineHL(TargetInfo info);

    /**
     * @brief headBag
     * 上报目标信息到车长席帧头
     * @param p0(数据内容) sourceID(源ID) tgtID(目的ID) msgType(消息类型) msgCode(消息码) msgLen(信息长度)
     * @return 无返回值
     */
    void headBag(unsigned char *p0, int sourceID, int tgtID, int msgType, int msgCode, unsigned short msgLen);

    /**
     * @brief setScrollBarStyleSheet
     * 设置tableview的scrollbar的样式
     * @param void
     * @return 无返回值
     */
    void setScrollBarStyleSheet();

    /**
     * @brief sleep_msec  延迟
     * @param msec  毫秒
     */
    void sleep_msec(int msec);

public slots:
    /**
     * @brief slotRecvOCRTargetNum 从OCR插件接收目标编号
     */
    void slotRecvOCRTargetNum(int);

    /**
     * @brief slotTargetReportAddress 从ParameterSet插件接收目标上报地址
     */
    void slotTargetReportAddress(QString ip,int port);

protected slots:
    /**
     * @brief slotAdd_pushButton
     * 点击界面添加目标信息按钮触发槽函数。
     * @param void
     * @return 无返回值
     */
    void slotAdd_pushButton();

    /**
     * @brief slotDel_pushButton
     * 点击界面清空目标信息的按钮触发的槽函数。
     * @param void
     * @return 无返回值
     */
    void slotDel_pushButton();

    /**
     * @brief slotModifyTargetInfo
     * 点击界面目标编辑按钮，使得目标信息控件进入可编辑状态，然后编辑好后的信息，触发执行的槽函数。
     * @param void
     * @return 无返回值
     */
    void slotModifyTargetInfo(TargetInfo dData);

    /**
     * @brief slotTargetReport
     * 目标上报
     * @param dData(目标信息)
     * @return 无返回值
     */
    void slotTargetReport(TargetInfo dData);

    /**
     * @brief slotItemMessageInfoText
     * 接收TargetTable类对象中的删除按钮所触发的对单个目标信息的删除。
     * @param void
     * @return 无返回值
     */
    void slotItemMessageInfoText(QString dData);

    /**
     * @brief slotModifyNoInfo
     * 修改目标编号
     * @param str1(历史目标编号),str2(新目标信息)
     * @return 无返回值
     */
    void slotModifyNoInfo(QString str1,TargetInfo str2);

    /**
     * @brief slotTargetItemModifyChecked  Item编辑状态互斥
     * @param item
     * @param checked
     */
    void slotTargetItemModifyChecked(TargetItem *item, bool checked);

    /**
     * @brief slotConfirmBtnClicked
     * 由MessageBox插件点击确认后执行的槽函数。
     * @param dData（MessageBox返回的弹窗主题名称）
     * @return 无返回值
     */
    void slotConfirmBtnClicked(QString dData);

    /**
     * @brief slotShowTable
     * 是否显示目标列表插件
     * @param bStatus(true:显示 false:隐藏)
     * @return 无返回值
     */
    void slotShowTable(bool bStatus);

    /**
     * @brief slotRecvOcrTargetInfo
     * 接收OCR识别插件上报的目标信息
     * @param dData(目标属性信息)
     * @return 无返回值
     */
    void slotRecvOcrTargetInfo(TargetInfo dData);

signals:
    // 发送消息至MessageBox插件
    void signalMessageInfoText(QString,QString,MessageType);

    // 发送目标信息到车上席位 Udp
    void sendUdpTargetInfo(QByteArray dData);

    // 发送目标信息到车上席位 Tcp
    void sendTcpTargetInfo(QByteArray dData);


    /**
     * @brief signalRecParaValue
     * 从ParameterSet插件接收其他设置参数（目标上报IP PORT）
     * @param strData（其他设置参数）
     * @return void
     */
    void signalRecParaValue(QString strData);

    /**
     * @brief signalTargetNum 向OcrRecognition发送目标编号
     */
    void signalTargetNum(int);

private:
    Ui::TargetTableWidget *ui;
    IUiPluginBase *m_pPlugin = nullptr; // 界面插件指针
    QVector<TargetInfo> m_vTargetInfo; // 用于存储目标列表各Item显示的详细数据
    QList<TargetItem*> m_listTargetItem; // 用于存储目标列表Item界面
    int m_nScrollOffset = 10; // 使得每个TargetItem具体布局边框间隔10像素
    QString m_treatDeleteTargetId; //用于存储待删除的目标批号，当点击确认删除时依据该批号进行删除目标
    bool m_bHide = true; // 默认目标列表为隐藏的
    TargetInfoSocket  *m_targetInfoSocket = nullptr;   //upd收发
    TcpComm *m_TcpComm = nullptr;   //tcp收发
    ushort m_num=0;//计数
    QString m_cartype="033";//0:1501 033 1:002车
    int m_OcrNum=0;//ocr目标编号
    int m_maxTargetNumber=0;//目标列表目标编号
    QString m_clientHost;          // 客户端目标主机ip
    quint16 m_clientPort;          // 客户端目标端口
};

#endif // TARGETTABLEWIDGET_H
