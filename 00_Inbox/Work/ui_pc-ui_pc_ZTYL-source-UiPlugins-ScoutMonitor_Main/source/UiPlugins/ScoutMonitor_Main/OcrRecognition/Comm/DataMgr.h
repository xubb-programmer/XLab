#ifndef DATAMGR_H
#define DATAMGR_H
#include <QObject>
#include "IUiPluginBase.h"
#include "Comm/sseclient.h"

class DataMgr: public QObject
{
    Q_OBJECT
public:
    static DataMgr & getInstance();

    ~DataMgr();

    /**
     * @brief initData
     * 初始化数据转发类。
     * @param pPlugin 界面框架指针
     * @return 无返回值
     */
    void initData(IUiPluginBase *plugin);

    /**
     * @brief initCfg
     * 读取配置文件获取发送时的基础UI
     * @param void
     * @return 无返回值
     */
    void initCfg();

    /**
     * @brief connectServer SSE连接
     * @param void
     * @return 无返回值
     */
    void connectServer();

    /**
     * @brief reconnectServer 重新连接SSE
     * @param address
     */
    void reconnectServer(QString address);

protected slots:
    /**
     * @brief slotlSSELinkStatus
     * 接收SSE推流的心跳消息。
     * @param bStatus（true:已连接 false:连接断开）
     * @return 无返回值
     */
    void slotlSSELinkStatus(bool bStatus);

signals:
    void signalSendSSEInfo(QString, QString);

private:
    SSEClient m_sseClient; //创建SSE客户端实例
    IUiPluginBase *m_pPlugin = nullptr; //界面插件指针
    QString m_strUrl = "";
};

#endif // DATAMGR_H
