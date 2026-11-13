#ifndef PARACONTENTWIDGET_H
#define PARACONTENTWIDGET_H

#include <QWidget>
#include <QHash>
#include <QReadWriteLock>
#include "IUiPluginBase.h"
#include "definestruct.h"
#include "VideoSetWidget.h"
#include "Comm/httpworker.h"
#include "Comm/MulticastClient.h"

namespace Ui {
class ParaContentWidget;
}

class DeviceListWidget;

class ParaContentWidget : public QWidget
{
    Q_OBJECT

    struct DevicePushStreamData
    {
        QString serialNumber;    // 序列号（唯一标识）
        QString deviceName;      // 设备名称
        QString deviceId;        // 设备编号（展示/业务编号）
        QString deviceIp;
        QStringList pushStreamList;
    };

public:
    explicit ParaContentWidget(QWidget *parent = 0);
    ~ParaContentWidget();

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

    /**
     * @brief updateVideoSetData
     * 刷新视频参数设置列表
     * @param VideoSetData
     * @return void
     */
    void updateVideoSetData(QVector<VideoSetData> vdData);

    /**
     * @brief updateAddVideoSetData
     * 手动添加时，并刷新视频参数设置列表
     * @param dData  视频参数
     * @param strListData 数据
     * @param refreshLayout 为 false 时可批量添加后在外部统一调用 updateVideoUI，减少重复布局
     * @return void
     */
    void updateAddVideoSetData(VideoSetData dData, const StreamUi::StreamItemList &strListData, bool refreshLayout = true);

    /**
     * @brief addVideoSetUi
     * 添加一组视频参数设置界面
     * @param strData
     */
    void addVideoSetUi(QVector<VideoSetData> vstrData);

    /**
     * @brief addVideoSetZLMlistUi
     * 添加一组ZLM服务器当前推流地址
     * @param strListData 推流列表数据
     * @param refreshLayout 批量创建时可传 false，最后统一 updateVideoUI
     * @return void
     */
    void addVideoSetZLMlistUi(const StreamUi::StreamItemList &strListData, bool refreshLayout = true);

    /**
     * @brief updateVideoUI
     * UI刷新
     * @param void
     * @return void
     */
    void updateVideoUI();

    /**
     * @brief setPushStream
     * 将设置成功的推流地址存储到本地
     * @param void
     * @return void
     */
    void setPushStream();

    /**
     * @brief getIdleItemNumber();
     * 获取空闲Item视频编号
     * @param void
     * @return int
     */
    int getIdleItemNumber();

    /**
     * @brief setIdentify;
     * 识别开关
     * @param bFlag(true:识别开 false:识别关)
     * @return void
     */
    void setIdentify(VideoSetData dData, bool bFlag);

    /**
     * @brief getPushStreamListByserialNumber
     * 按序列号（唯一标识）查询推流列表；（顺序为哈希遍历顺序）
     */
    QStringList getPushStreamListByserialNumber(const QString &serialNumber) const;


    /**
     * @brief sendSwitchBigScreen 更新m_currentBigScreenData数据，并下发大屏切换指令
     * @param dData
     */
    void sendSwitchBigScreen(VideoSetData,bool);

    /**
     * @brief showDeviceListWidget 显示隐藏DeviceListWidget
     */
    void showDeviceListWidget(QPoint);

    /**
     * @brief clearAllVideoSetItem 清空所有item数据
     */
    void clearAllVideoSetItem();

private:
    /**
     * @brief getDeviceIpByDeviceId
     * 按设备deviceId查询IP
     * @return
     */
    QString getDeviceIpByDeviceId(const QString &deviceId) const;

    /**
     * @brief switchClientToDeviceVideoInput 强制客户端拉取指定设备的视频输入流
     * 地址按设备 IP 和 PushStream/pushstreamName 直接生成，不依赖设备是否上报该流。
     */
    void switchClientToDeviceVideoInput(const QString &deviceAddress);

    /** 对比上方设备列表、设备名称下拉框和内部缓存，记录差异诊断日志。 */
    void logDeviceListDiagnostics(const QString &reason, bool force = false);

    /**
     * @brief selectPushStreamListBySerialNumber 按下拉项内部绑定的设备序列号刷新推流列表
     * @param serialNumber 为空时表示“全部”
     */
    void selectPushStreamListBySerialNumber(const QString &serialNumber);

    /**
     * @brief refreshProductCombo 重建设备名称下拉并选中指定项
     * @param selectSerialNumber 非空时选中该序列号对应的名称项；为空则保持重建前的当前项
     */
    void refreshProductCombo(const QString &selectSerialNumber = QString());

    /**
     * @brief initDefaultVideoSetItems 创建默认 MAX_VIDEOSET_NUM 路 VideoSetWidget，批量结束时统一布局
     */
    void initDefaultVideoSetItems();

    /** 当前是否为默认空窗口布局（无拉流中通道），用于避免重复 clear/init */
    bool isDefaultVideoSetItemsLayout() const;

    /**
     * @brief videoSetWidgetForScreen  根据窗口号返回VideoSetWidget界面类
     * @param smallScreenNum 窗口号
     * @return  VideoSetWidget
     */
    VideoSetWidget *videoSetWidgetForScreen(int smallScreenNum) const;

    /**
     * @brief syncVideoSetDataVectorEntry 更新m_vVideoSetData
     * @param d
     */
    void syncVideoSetDataVectorEntry(const VideoSetData &d);

    /**
     * @brief parseDevicePushStreamData
     * 从单条 JSON 对象解析「序列号 / 设备名称 / 设备编号 / 设备IP / 推流列表」。
     * 序列号与设备 IP 必填；推流列表为数组，非字符串元素跳过。
     * @param obj 一条设备上报的 JSON 对象
     * @param data 输出解析结果（与 obj 一一对应）
     * @return 解析成功为 true，字段缺失/类型错误为 false
     */
    bool parseDevicePushStreamData(const QJsonObject &obj, DevicePushStreamData &data) const;

    /**
     * @brief applyDevicePushStreamDataBatch
     * 将一批设备推流数据原子写入内存缓存：整批在单次写锁内完成。
     * 已存在相同序列号则合并更新展示字段（名称、编号、IP）与推流列表；不存在则插入新记录。
     * @param batch 已解析好的设备数据向量（右值，避免重复拷贝大列表）
     */
    void applyDevicePushStreamDataBatch(QVector<DevicePushStreamData> &&batch);

    /**
     * @brief recordMulticastDeviceActivity 记录本帧组播中出现的设备（以序列号为唯一标识）
     */
    void recordMulticastDeviceActivity(const QVector<DevicePushStreamData> &batch);

    /**
     * @brief checkMulticastDeviceChanges 定时检查：某设备超过 3s 未上报则移除该设备缓存
     */
    void checkMulticastDeviceChanges();

    /**
     * @brief removeDevicePushStreamEntry 按序列号移除单台设备的推流缓存
     * @return 是否实际移除了记录
     */
    bool removeDevicePushStreamEntry(const QString &serialNumber);

    /**
     * @brief refreshPushStreamListUiAfterDeviceChange 设备列表变化后刷新推流下拉
     */
    void refreshPushStreamListUiAfterDeviceChange();

    /** 使用与窗口设置相同的 ZLM 有效流列表刷新客户端拉流地址下拉框 */
    void refreshClientPullUrlCombo(const StreamUi::StreamItemList &streamList);

    /** 更新独立的客户端拉流协议选择及确认按钮状态。 */
    void updateClientProtocolControls();
    bool currentClientStreamIsValid() const;
    QString clientStreamUrlForProtocol(const QString &protocol,
                                       const QString &sourceUrl = QString()) const;

    /** 退出/析构前：停定时器、断开 HTTP、清空 VideoSet 裸指针缓存 */
    void shutdownHttpResources();

    /** 在工作线程中调度 SSE 连接（避免主线程直接操作 HttpWorker） */
    void enqueueWorkerSetSseClient(const QString &host);

     /** SSE 连接 abort */
    void enqueueWorkerSseLinkabort();

protected slots:
    /**
     * @brief slotRTSP_pushButton
     * 点击“RTSP”按钮让自身按钮高亮，其他按钮恢复常态
     * @param void
     * @return void
     */
    void slotRTSP_pushButton();

    /**
     * @brief slotRTMP_pushButton
     * 点击“RTMP”按钮让自身按钮高亮，其他按钮恢复常态
     * @param void
     * @return void
     */
    void slotRTMP_pushButton();

    /**
     * @brief slotH264_pushButton
     * 点击“H264”按钮让自身按钮高亮，其他按钮恢复常态
     * @param void
     * @return void
     */
    void slotH264_pushButton();

    /**
     * @brief slotH265_pushButton
     * 点击“H265”按钮让自身按钮高亮，其他按钮恢复常态
     * @param void
     * @return void
     */
    void slotH265_pushButton();

    /**
     * @brief slotStreamSet_pushButton
     * 客户端拉流设置
     * @param void
     * @return void
     */
    void slotStreamSet_pushButton(bool bFlag);

    /**
     * @brief slotClientPullUrl_pushButton 客户端拉流地址设置
     */
    void slotClientPullUrl_pushButton();

    /**
     * @brief slotSendVideoSetInfo
     * 拉流设置http协议下发
     * @param VideoSetData bFlag
     * @return void
     */
    void slotSendVideoSetInfo(VideoSetData dData,bool bFlag);

    /**
     * @brief slotAddVideoSetItem
     * 添加视频Item子项
     * @param void
     * @return void
     */
    void slotAddVideoSetItem();

    /**
     * @brief slotDeleteItemVideoSetItem
     * 点击视频Item子项中的“删除”按钮，触发对子项的删除
     * @param VideoSetData（需要删除的子项详情信息）
     * @return void
     */
    void slotDeleteItemVideoSetItem(VideoSetData dData);

    /**
     * @brief slotIdentifyVideoSetItem
     * 点击视频Item子项中的“识别”按钮，触发对子项的识别功能
     * @param VideoSetData（需要识别的子项详情信息）
     * @return void
     */
    void slotIdentifyVideoSetItem(VideoSetData dData,bool bFlag);

    /**
     * @brief slotFinishedForPullStreamPara
     * 接收拉流设置http反馈（pendingContext / reqKind 与该次 HTTP 请求对应，支持多路并发下发）
     * @param strData 反馈消息内容
     * @param pendingContext 下发时的界面数据快照
     * @param reqKind 0=拉流开关 slotSendVideoSetInfo，1=仅识别 setIdentify
     */
    void slotFinishedForPullStreamPara(QString strData, VideoSetData pendingContext, int reqKind);

    /**
     * @brief slotFinishedForOCRSwitch
     * 接收OCR识别http反馈
     * @param strData（反馈消息内容）
     * @return void
     */
    void slotFinishedForOCRSwitch(QString strData);

    /**
     * @brief slotFinishedForOCRSwitch
     * 接收目标识别http反馈
     * @param strData（反馈消息内容）
     * @return void
     */
    void slotFinishedForRecognitionSwitch(QString strData);

    /**
     * @brief slotFinishedForPushStreamPara
     * 接收推流设置http反馈
     * @param strData（反馈消息内容）
     * @return void
     */
    void slotFinishedForPushStreamPara(QString strData);

    /**
     * @brief slotFinishedForSwitchBigScreenWork
     * 接收大屏切换http反馈
     * @param strData（反馈消息内容）
     * @return void
     */
    void slotFinishedForSwitchBigScreenWork(QString strData, VideoSetData pendingContext);


    /**
     * @brief slotFinishedForPushStreamPara
     * 接收MessageBox确认消息
     * @param strData（消息主题）
     * @return void
     */
    void slotConfirmBtnClicked(QString strData);

    /**
     * @brief slotIdentify
     * 接收菜单消息目标识别开关
     * @param nType（菜单ID） nStatus(识别开关 1 关 2开)
     * @return void
     */
    void slotIdentify(int nType, int nStatus);

    /**
     * @brief slotUpdateVideoSetDate 更新m_vVideoSetData数据
     * @param dData
     */
    void slotUpdateVideoSetDate(VideoSetData dData);


    /**
     * @brief slotFinishedForgetpullStreamList
     * 接收获取设备端正在拉流列表查询结果
     * @param strData（反馈消息内容）
     * @return void
     */
    void slotFinishedForgetpullStreamList(QString strData);
    /**
     * @brief slotFinishedForgetZLMpushList
     * 接收SSE推送的ZLM服务器推流列表
     * @param strData（上报消息内容）
     * @return void
     */
    void slotFinishedForgetZLMpushList(QString strEvent,QString strData);

    /**
     * @brief slotSwitchBigScreenSetItem 更新m_currentBigScreenData数据，并下发大屏切换指令
     * @param dData
     */
    void slotSwitchBigScreenSetItem(VideoSetData dData,bool);

    /**
     * @brief slotRefreshpushStreamList 更新VideoSetWidget中combox下拉列表zlm数据
     */
    void slotRefreshpushStreamList();

    /**
     * @brief slotRefreshDeviceNameList 按comboBox_product中选中的设备名称项刷新推流列表
     * @param index
     */
    void slotRefreshDeviceNameList(int index);

public slots:
    /**
     * @brief slotsendpullStreamList 发送获取拉流列表命令
     */
    void slotsendpullStreamList();

    /**
     * @brief slotReconnectSSE SSE重连
     */
    void slotReconnectSSE(QString address);

    /**
     * @brief slotUdpDataReceived  接收UDP组播（ZLM推流列表信息）
     */
    void slotUdpDataReceived(const QByteArray &arr, QHostAddress ip, quint16 port);

    /**
     * @brief slotproductNum 接收产品编号，用于设备列表连接的设备选中
     * @param productNum 产品编号
     */
    void slotproductNum(QString productNum);

    /** 从设备信息 HTTP 状态中同步当前设备名称，作为组播发现的备用数据源。 */
    void slotCurrentDeviceStatus(QString strData, int statusCode);

    /** 当前设备离线时，移除由 HTTP 状态补入的设备名称。 */
    void slotCurrentDeviceOnlineChanged(bool online);

signals:
    // 拉流消息下发（pendingContext、reqKind 随请求传入 HttpWorker，响应中原样带回）
    void signalPosPullStreamPara(QString, QJsonObject, VideoSetData, int reqKind);

    // 推流消息下发
    void signalPosPushStreamPara(QString, QJsonObject);

    // 协助Ocr界面下发Ocr识别区域
    void signalOcrPushStreamPara(QString, QJsonObject);

    // 协助网口设置界面下发网口设置参数
    void signalSendNetSetPara(QString, QJsonObject);

    // 发送消息至MessageBox插件
    void signalMessageInfoText(QString,QString,MessageType);

    // 发送带自定义按钮文字的确认消息至MessageBox插件
    void signalMessageText(QString,QString,QString,QString,QString);

    // 菜单消息的目标识别
    void signalRecognitionSwitch(QString, QJsonObject);

    // 菜单消息的ORC识别
    void signalOCRSwitch(QString, QJsonObject);

    // 反馈给菜单当前识别开启反馈
    void signalIdentifyRespons(int nType, int nStatus);

    // 反馈Ocr识别区域设置结果
    void signalFinishedForOcrPushStreamPara(QString);

    // 反馈网口设置结果
    void signalFinishedForNetWorkSet(QString);

    // 获取拉流列表下发
    void signalpullStreamList(QString);

    //更新VideoSetWidget中m_readListData读取本地的拉流数据
    void signalUpdateReadListData(StreamUi::StreamItemList);

    /**
     * @brief signalSwitchBigScreen  dds下发视频大屏切换指令--old
     * @param screenNum  小屏窗口号
     */
    void signalSwitchBigScreen(QJsonObject screenNum);

    /**
     * @brief signalPosSwitchBigScreen http下发视频大屏切换指令 --new
     */
    void signalPosSwitchBigScreen(QString, QJsonObject, VideoSetData);

    /**
     * @brief signalSendVideoSetResult 发送下发拉流，失败信号
     */
    void signalSendVideoSetResult();

    /**
     * @brief signalStreamPullAdress 发送客户端拉流参数
     * @param adresss 拉流地址
     */
    void signalStreamPullAdress(QString adresss);

    /**
     * @brief signalZLMPushStatus 发送推流状态
     * @param status 推流状态 0-推流完成 1-推流准备中 2-推流错误
     * @param reason 错误原因  推流错误情况下有值，其他情况下为空字符串
     */
    void signalZLMPushStatus(int status,QString reason);

    /**
     * @brief signalcurrentDeviceIp 发送当前选择设备ip
     * @param ip
     */
    void signalcurrentDeviceIp(QString ip);

protected:
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event);

private:
    Ui::ParaContentWidget *ui;
    IUiPluginBase *m_pPlugin = nullptr;
    QVector<VideoSetData> m_vVideoSetData; //用于存储视频参数设置列表详情信息
    QList<VideoSetWidget*> m_listVideoSetItem; //用于存储视频Item子项界面指针
    int m_nScrollOffset = 0; //视频item距离边框的距离
    HttpWorker *m_worker = nullptr; //http通信实例化指针
    QThread *m_workerThread = nullptr; //工作子线程
    bool m_shuttingDown = false;//退出程序 标志
    MulticastClient *m_pMulticastClient = nullptr;//UDP组播（ZLM推流列表）
    QString m_strUrl = "10.100.24.230:3000"; // http通信基础Url
    QString m_currentDeviceIP = "10.100.24.230"; // 当前连接设备ip
    QString m_PushStreamName = "/live/stream"; // 推流名称
    QString m_StreamPullAdress="";//客户端拉流地址
    QString m_clientProtocolSelection = QStringLiteral("rtsp");//下方独立选择的 RTSP/RTMP，仅协议确认时作用于地址
    StreamUi::StreamItemList m_pendingClientPullUrls{};//顶部地址编辑期间收到的最后一份待刷新列表
    bool m_clientPullRefreshPending = false;
    int m_strDeleteItem; // 待删除的Item窗口编号
    int m_nArrayFlag[MAX_VIDEOSET_NUM] = {0}; // 用于存储是否有空闲视频编号
    bool m_bPullSwitch = false; // 拉流开关
    bool m_bIdentifySwitch = false; // 识别开关
    bool m_bTagetIdentifyFlag = false; // 一级菜单识别开关
    StreamUi::StreamItemList m_pushstreamlist{};//拉流数据
    StreamUi::StreamItemList m_localPullStreamItems{};//读取本地的完整拉流数据项（含配置名称与地址）
    QStringList m_localpushstreamData{};//读取本地的拉流数据
    QHash<QString, QString> m_localStreamDisplayNameByUrl{};//communication.ini PullStream：真实地址 -> 配置名称
    mutable QReadWriteLock m_deviceDataLock; // 保护设备推流缓存，支持多读单写
    QHash<QString, DevicePushStreamData> m_devicePushStreamDataBySN; // 序列号 -> 完整设备推流数据
    QHash<QString, QString> m_serialNumberBydeviceId; // 设备编号 -> 序列号（用于反查同一条记录）
    VideoSetData m_currentVideoSetData;//当前操作的拉流设置
    VideoSetData m_currentBigScreenData;//当前操作的大屏信息
    QStringList m_curDevicepushstreamlist{};//当前连接设备上报的ZLM服务器推流列表
    QString m_currentDeviceId="";//当前连接的设备编号
    QString m_statusDeviceName{}; // 设备信息 HTTP 返回的当前设备名称（组播缓存为空时使用）
    QString m_statusDeviceId{};   // 设备信息 HTTP 返回的当前设备编号
    bool m_productComboRefreshPending = false; // 下拉展开期间设备列表变化，关闭后再安全重建

    DeviceListWidget *m_deviceListWidget = nullptr;//设备列表 界面类
    QTimer *m_pTimer = nullptr;//定时器 查询正在拉流列表
    QTimer *m_pMulticastDeviceWatchTimer = nullptr; // 组播设备活跃检测
    QHash<QString, qint64> m_multicastDeviceLastSeenMs; // 序列号 -> 最近一次组播上报时间(ms)
    qint64 m_lastAnyMulticastReceivedMs = 0; // 最近一次收到任意组播帧的时间(ms)
    qint64 m_lastDeviceListMismatchLogMs = 0; // 设备列表差异日志节流时间(ms)

    //组播IP 端口
    QString m_groupIp = "224.0.0.100";
    quint16 m_localPort = 50000;


};

#endif // PARACONTENTWIDGET_H
