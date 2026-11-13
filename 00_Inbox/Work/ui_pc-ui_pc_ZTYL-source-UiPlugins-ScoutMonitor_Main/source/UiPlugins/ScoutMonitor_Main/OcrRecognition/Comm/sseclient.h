#ifndef SSECLIENT_H
#define SSECLIENT_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QString>
#include <QSslError>

class SSEClient : public QObject
{
    Q_OBJECT
public:
    explicit SSEClient(QObject *parent = nullptr);
    
    // 设置SSE服务器URL
    void setUrl(const QString &url);
    
    // 连接到SSE服务器
    void connectToServer();
    
    // 断开连接
    void disconnectFromServer();
    
    // 设置重连间隔（毫秒）
    void setReconnectInterval(int msec);

    /** 已建立流后无数据的空闲超时（毫秒），0 表示禁用；默认 30s */
    void setStreamIdleTimeout(int msec);

signals:
    // 接收到事件时触发
    void eventReceived(const QString &event, const QString &data);
    
    // 连接状态变化时触发
    void connectionStatusChanged(bool connected);
    
    // 发生错误时触发
    void errorOccurred(const QString &errorString);

private slots:
    void onReadyRead();
    void onFinished();
    void onSslErrors(const QList<QSslError> &errors);
    void onError(QNetworkReply::NetworkError code);
    void reconnect();
    /** 长时间无响应（含 TCP 挂起）时中止当前请求，走 finished 进入重连 */
    void onConnectTimeout();
    /** 流建立后长时间无数据（服务端/代理半开）时中止并进入重连 */
    void onStreamIdleTimeout();

private:
    void processBuffer();
    void resetConnection();
    /** 启动重连定时器（受 m_explicitDisconnect / URL / 间隔约束） */
    void scheduleReconnect();
    /** 中止当前 reply、清空缓冲；若曾视为已连接则发出断开信号。不触发重连定时器。 */
    void tearDownReply();
    /** 在旧 SSE reply 已 teardown 后刷新 NAM 连接池，避免断网重连复用僵死连接 */
    void flushTransportLayer();
    void stopStreamIdleTimer();
    void bumpStreamIdleTimer();

    QNetworkAccessManager *m_manager;
    QNetworkReply *m_reply;
    QString m_url;
    QString m_buffer;
    QTimer m_reconnectTimer;
    /** 单次连接无首包/无 finished 时的看门狗，避免网络断开后挂死不再重连 */
    QTimer m_connectTimeoutTimer;
    /** 流已建立后无 readyRead 时的看门狗，覆盖服务端进程退出但 TCP 半开 */
    QTimer m_streamIdleTimer;
    int m_reconnectInterval;
    int m_streamIdleTimeoutMs;
    bool m_connected;
    /** 为 true 时表示用户主动 disconnect，连接结束时不应再自动重连 */
    bool m_explicitDisconnect;
    /** connectToServer 执行中，避免陈旧 finished 误补 scheduleReconnect */
    bool m_isConnecting;
    /** 防止 onError 与 onFinished 重复 resetConnection */
    bool m_handlingReplyEnd;
};

#endif // SSECLIENT_H
