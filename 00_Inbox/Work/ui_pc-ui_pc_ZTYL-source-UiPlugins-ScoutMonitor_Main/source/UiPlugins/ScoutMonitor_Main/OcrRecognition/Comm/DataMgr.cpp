#include "DataMgr.h"
#include <QSettings>
#include <QTextCodec>
#include <QMessageBox>
#include "FrameLogManager.h"

DataMgr &DataMgr::getInstance()
{
    static DataMgr Instance;
    return Instance;
}

DataMgr::~DataMgr()
{
    m_sseClient.disconnectFromServer();
    qDebug() << "DataMgr destroyed";
    FrameLogManager::getInstance()->log("OcrRecognition","关闭SSE服务器URL:" ,IFrameLogManager::INFO_TYPE);
}

/**
 * @brief initData
 * 初始化数据转发类。
 * @param pPlugin 界面框架指针
 * @return 无返回值
 */
void DataMgr::initData(IUiPluginBase *plugin)
{
    m_pPlugin = plugin;

    initCfg();

    // 设置SSE服务器URL（替换为实际的SSE服务器地址）
    QString strUrl = "http://" + m_strUrl +"/proxy/target/stream";
    m_sseClient.setUrl(strUrl);

    // 设置重连间隔为3秒
    m_sseClient.setReconnectInterval(3000);

    // 连接信号槽（UniqueConnection 避免重复连接)
    QObject::connect(&m_sseClient, &SSEClient::eventReceived, this, &DataMgr::signalSendSSEInfo, Qt::UniqueConnection);
    QObject::connect(&m_sseClient, &SSEClient::connectionStatusChanged, this, &DataMgr::slotlSSELinkStatus, Qt::UniqueConnection);
    QObject::connect(&m_sseClient, &SSEClient::errorOccurred,
                     [](const QString &errorString) {
        qWarning() << "SSE error:" << errorString;
    });

    // 连接到SSE服务器
    m_sseClient.connectToServer();
}


/**
 * @brief initCfg
 * 读取配置文件获取发送时的基础UI
 * @param void
 * @return 无返回值
 */
void DataMgr::initCfg()
{
    QSettings settings(QApplication::applicationDirPath() + "/config/communication.ini" , QSettings::IniFormat);
    settings.setIniCodec(QTextCodec::codecForName("GBK"));
    settings.beginGroup("HTTP");
    m_strUrl = settings.value("url").toString();
    settings.endGroup();
}

/**
 * @brief connectServer SSE连接
 * @param void
 * @return 无返回值
 */
void DataMgr::connectServer()
{
    m_sseClient.connectToServer();
}

/**
 * @brief reconnectServer 重新连接SSE
 * @param address
 */
void DataMgr::reconnectServer(QString address)
{
    if(address==""/* || m_strUrl==address*/)
        return;

    //先断开之前SSE连接
    m_sseClient.disconnectFromServer();

    // 设置SSE服务器URL（替换为实际的SSE服务器地址）
    QString strUrl = "http://" + address +"/proxy/target/stream";
    m_sseClient.setUrl(strUrl);

    // 设置重连间隔为3秒
    m_sseClient.setReconnectInterval(3000);

    // 连接信号槽
//    QObject::connect(&m_sseClient, &SSEClient::eventReceived, this, &DataMgr::signalSendSSEInfo);
//    QObject::connect(&m_sseClient, &SSEClient::connectionStatusChanged, this, &DataMgr::slotlSSELinkStatus);
//    QObject::connect(&m_sseClient, &SSEClient::errorOccurred,
//                     [](const QString &errorString) {
//        qWarning() << "SSE error:" << errorString;
//    });

    // 连接到SSE服务器
    m_sseClient.connectToServer();

    m_strUrl=address;
}

/**
 * @brief slotlSSELinkStatus
 * 接收SSE推流的心跳消息。
 * @param bStatus（true:已连接 false:连接断开）
 * @return 无返回值
 */
void DataMgr::slotlSSELinkStatus(bool bStatus)
{
    if (bStatus) {
        qDebug() << "Connected to SSE server";
    } else {
        qDebug() << "Disconnected from SSE server";
    }
}
