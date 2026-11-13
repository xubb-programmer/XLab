#ifndef TCPCOMM_H
#define TCPCOMM_H

#include <QObject>
#include <QHostAddress>
#include <QList>

class QTcpServer;
class QTcpSocket;

/**
 * @brief 通用 TCP 收发类
 *
 * 同一实例在任一时刻仅处于一种工作角色（服务端 / 客户端 / 未启动）。
 * 切换角色前须先调用 stop()，或通过 startServer / startClient 内部自动 stop。
 *
 * 使用示例：
 * @code
 * TcpComm *tcp = new TcpComm(this);
 * connect(tcp, &TcpComm::dataArrive, this, &MyClass::onData);
 * tcp->startServer(9000);           // 服务端
 * // 或
 * tcp->startClient("127.0.0.1", 9000);  // 客户端
 * tcp->sendData(QByteArray("hello"));
 * @endcode
 *
 * 注意：TCP 为字节流，本类在 readyRead 时 readAll()，不做粘包/拆包，应用层需自行组帧。
 */
class TcpComm : public QObject
{
    Q_OBJECT
public:
    /** @brief 当前 TCP 工作角色 */
    enum Role {
        RoleNone = 0,   ///< 未启动
        RoleServer,     ///< 服务端：监听并接受多个客户端连接
        RoleClient      ///< 客户端：主动连接远端服务器
    };
    Q_ENUM(Role)

    /**
     * @brief 构造
     * @param parent 父对象
     */
    explicit TcpComm(QObject *parent = nullptr);

    /**
     * @brief 析构，自动调用 stop() 释放连接
     */
    ~TcpComm();

    /**
     * @brief 以服务端模式监听指定地址和端口
     * @param port 监听端口
     * @param address 绑定地址，默认 QHostAddress::Any 表示所有网卡
     * @return 监听成功返回 true；失败时发出 errorOccurred 并返回 false
     */
    bool startServer(quint16 port, const QHostAddress &address = QHostAddress::Any);

    /**
     * @brief 以客户端模式连接远端
     * @param host 远端 IP 或主机名
     * @param port 远端端口
     * @return 始终返回 true（仅表示已发起连接）；实际连上后发出 connected，失败发出 errorOccurred
     */
    bool startClient(const QString &host, quint16 port);

    /**
     * @brief 停止当前 TCP 服务或连接
     * 关闭监听、断开所有客户端、释放套接字，角色重置为 RoleNone
     */
    void stop();

    /**
     * @brief 客户端模式：使用上次 startClient 的地址重新连接
     * 断线后会重建套接字再连接，避免复用已断开的 QTcpSocket 导致无法连上
     * @return 已发起连接返回 true；未处于客户端模式或缺少地址返回 false
     */
    bool reconnectClient();

    /**
     * @brief 设置客户端断线后是否自动重连
     * @param enable true 开启（默认），false 关闭
     */
    void setAutoReconnect(bool enable);

    /**
     * @brief 设置自动重连间隔
     * @param msec 间隔毫秒数，默认 3000
     */
    void setReconnectInterval(int msec);

    /**
     * @brief 获取当前工作角色
     * @return RoleNone / RoleServer / RoleClient
     */
    Role role() const { return m_role; }

    /**
     * @brief 客户端模式：是否已与服务器建立连接
     * @return 已连接返回 true
     */
    bool isClientConnected() const;

    /**
     * @brief 服务端模式：当前已连接的客户端数量
     * @return 客户端个数
     */
    int clientCount() const;

    /**
     * @brief 服务端模式下向指定客户端发送数据
     * @param socket 目标客户端套接字（须为 listen 后 accept 得到的连接）
     * @param data 待发送内容
     * @return 实际写入字节数；socket 无效或未连接返回 -1
     */
    qint64 sendDataTo(QTcpSocket *socket, const QByteArray &data);

public slots:
    /**
     * @brief 发送数据
     * @param data 待发送内容
     * @return 客户端：写入字节数；服务端：向所有已连接客户端广播后的总写入字节数；
     *         未连接或 data 为空（空返回 0）失败返回 -1
     */
    qint64 sendData(const QByteArray &data);

signals:
    /**
     * @brief 收到数据
     * 客户端模式：每次 readyRead 触发；
     * 服务端模式：仅当当前只有一个客户端时额外触发（多客户端请用 dataArriveFrom）
     * @param data 本次读到的全部字节（未做粘包处理）
     */
    void dataArrive(const QByteArray &data);

    /**
     * @brief 服务端模式：收到某客户端数据
     * @param socket 数据来源套接字，可用于 sendDataTo 回发
     * @param data 本次读到的全部字节
     */
    void dataArriveFrom(QTcpSocket *socket, const QByteArray &data);

    /**
     * @brief 客户端模式：已成功连接到服务器
     */
    void connected();

    /**
     * @brief 客户端模式：与服务器连接已断开
     */
    void disconnected();

    /**
     * @brief 服务端模式：新客户端接入
     * @param socket 新连接套接字，生命周期由本类管理，断开时 deleteLater
     */
    void clientConnected(QTcpSocket *socket);

    /**
     * @brief 服务端模式：客户端断开
     * @param socket 已断开的套接字（即将销毁，勿再使用）
     */
    void clientDisconnected(QTcpSocket *socket);

    /**
     * @brief 发生套接字错误（监听失败、连接失败、传输错误等）
     * @param error 错误描述字符串
     */
    void errorOccurred(const QString &error);

private slots:
    /** @brief 服务端：处理新连接请求 */
    void onNewConnection();

    /** @brief 套接字有数据可读 */
    void onSocketReadyRead();

    /** @brief 套接字连接断开 */
    void onSocketDisconnected();

    /** @brief 套接字错误 */
    void onSocketError(QAbstractSocket::SocketError socketError);

    /** @brief 客户端自动重连定时器超时 */
    void onReconnectTimeout();

private:
    /** @brief 关闭并清理服务端所有客户端连接 */
    void clearServerClients();

    /** @brief 销毁客户端套接字（立即释放，便于马上重建连接） */
    void destroyClientSocket();

    /**
     * @brief 创建新的客户端套接字并连接 m_clientHost:m_clientPort
     * @return 是否已发起连接
     */
    bool connectClient();

    /**
     * @brief 为套接字绑定统一的读、断开、错误信号
     * @param socket 待绑定的 TCP 套接字
     */
    void setupSocket(QTcpSocket *socket);

    /** @brief 断线后调度一次自动重连 */
    void scheduleClientReconnect();

    /**
     * @brief initConfig
     * 初始化配置
     */
    void getConfig();


private:
    Role m_role;                        ///< 当前工作角色
    QTcpServer *m_server;               ///< 服务端监听对象（仅 RoleServer 有效）
    QTcpSocket *m_clientSocket;         ///< 客户端连接套接字（仅 RoleClient 有效）
    QList<QTcpSocket *> m_serverClients; ///< 服务端已接受的客户端列表
    QString m_clientHost;               ///< 客户端目标主机
    quint16 m_clientPort;               ///< 客户端目标端口
    bool m_autoReconnect;                 ///< 客户端断线后是否自动重连
    int m_reconnectIntervalMs;          ///< 自动重连间隔（毫秒）
    class QTimer *m_reconnectTimer;     ///< 客户端自动重连定时器
};

#endif // TCPCOMM_H
