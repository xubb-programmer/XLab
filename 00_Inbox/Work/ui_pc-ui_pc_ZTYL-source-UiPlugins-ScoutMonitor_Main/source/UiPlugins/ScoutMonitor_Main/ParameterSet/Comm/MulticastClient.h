#ifndef MULTICASTCLIENT_H
#define MULTICASTCLIENT_H

#include <QObject>
#include <QThread>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QDateTime>

class MulticastWorker : public QObject
{
    Q_OBJECT
public:
    // 构造：初始化成员变量与定时器（定时器在 cpp 中创建并连接）
    explicit MulticastWorker(QObject *parent = 0);
    // 析构：确保 socket 资源被释放，离开组播组
    ~MulticastWorker();
signals:
    // 工作线程启动成功后发出
    void started();
    // 工作线程停止后发出
    void stopped();
    // 收到 UDP 数据报后发出（原始数据、发送方地址、发送方端口）
    void datagramReceived(QByteArray data, QHostAddress sender, quint16 port);
    // 出现错误时发出（用于上层日志和告警）
    void errorOccurred(QString err);
    // 连接状态变化通知（starting/running/reconnecting/stopped）
    void stateChanged(QString state);
public slots:
    // 配置参数：组播地址/端口、本地端口、网卡名、心跳周期、超时、重连退避参数
    void configure(QString groupIp,
                   quint16 groupPort,
                   quint16 localPort,
                   QString ifaceName,
                   int heartbeatMs,
                   int heartbeatTimeoutMs,
                   int reconnectBaseMs,
                   int reconnectMaxMs,
                   QString communicationIp);
    // 更新设备通信地址；运行中会离开旧接口，并按新地址重新选卡入组
    void setCommunicationAddress(QString communicationIp);
    // 启动收发：创建 socket、bind、join 组播、启动心跳与健康检查
    void startWork();
    // 停止收发：停止所有定时器、离组、关闭 socket
    void stopWork();
    // 发送业务数据（由 facade 层通过 queued invoke 投递到 worker 线程）
    void sendPayload(QByteArray payload);
private slots:
    // socket 可读回调：循环读取所有 pending datagram 并上抛信号
    void onReadyRead();
    // socket 错误回调：记录错误并触发重连流程
    void onSocketError();
    // 心跳定时器回调：按周期发送心跳报文
    void onHeartbeatTick();
    // 健康检查回调：检测最近接收时间，超时则触发重连
    void onHealthCheckTick();
    // 重连定时器回调：执行一次重连尝试
    void onReconnectTick();
private:
    // 打开 socket 并加入组播组，成功返回 true
    bool openSocketAndJoin();
    // 在当前 socket 上加入组播组：自动模式下所有可用网卡 + 系统默认网卡，显式指定网卡名时只在
    // 那块网卡；至少一处成功返回 true
    bool joinGroups();
    // 在当前 socket 上离开 joinGroups() 加入的组播组(幂等)
    void leaveGroups();
    // IPv4 组播：用本机网卡地址 localAddress 直接 setsockopt 入组/离组(不经 Qt，见 cpp 说明)
    bool setIpv4Membership(const QHostAddress &localAddress, bool join);
    // 关闭 socket 并离开组播组（幂等）
    void closeSocket();
    // 按退避策略安排下一次重连（并更新状态）
    void scheduleReconnect(QString reason);
    // 根据网卡名选择网卡；未指定时按设备通信地址的最长子网前缀匹配
    QNetworkInterface chooseInterface(const QString &ifaceName,
                                      const QHostAddress &communicationAddr) const;
    // 配置变化后立即重建组播成员关系
    void restartMembership(QString reason);
    // 判断地址是否为 IPv6 组播地址
    bool isIpv6Multicast(const QHostAddress &addr) const;
    // 判断地址是否为 IPv4 组播地址
    bool isIpv4Multicast(const QHostAddress &addr) const;
private:
    // UDP socket，仅在 worker 线程中创建/使用/销毁
    class QUdpSocket *m_socket;
    // 心跳发送定时器：按 m_heartbeatMs 周期发送心跳包
    class QTimer *m_heartbeatTimer;
    // 健康检查定时器：周期检查最近收包时间是否超时
    class QTimer *m_healthCheckTimer;
    // 重连定时器：单次触发，到点执行一次重连尝试
    class QTimer *m_reconnectTimer;
    // 当前加入的组播地址（IPv4/IPv6）
    QHostAddress m_groupAddr;
    // 目标组播端口（发送目的端口）
    quint16 m_groupPort;
    // 本地绑定端口（接收监听端口）
    quint16 m_localPort;
    // 组播发送接口（发心跳用）；显式指定网卡名时也是唯一的入组网卡
    QNetworkInterface m_iface;
    // 用户显式指定的网卡名；为空时启用按设备通信地址自动选卡
    QString m_ifaceName;
    // 当前设备通信地址，用于匹配同网段本机网卡
    QHostAddress m_communicationAddr;
    // 心跳发送周期（毫秒）
    int m_heartbeatMs;
    // 心跳超时时间（毫秒），超过该时间未收到数据则判定异常
    int m_heartbeatTimeoutMs;
    // 重连基础间隔（毫秒），用于指数退避起点
    int m_reconnectBaseMs;
    // 重连最大间隔（毫秒），指数退避上限
    int m_reconnectMaxMs;
    // 当前重连间隔（毫秒），每次失败后递增直到上限
    int m_currentReconnectMs;
    // 最近一次收到数据的时间戳（毫秒）
    qint64 m_lastRxMs;
    // 运行状态标记：startWork 后为 true，stopWork 后为 false
    bool m_running;
    // 入组状态标记：joinMulticastGroup 成功后为 true
    bool m_joined;
    // 已加入组播的网卡及入组时用的本机 IPv4 地址（用于安全离组；IPv6 组播时地址为空）
    struct JoinedInterface {
        QNetworkInterface iface;
        QHostAddress localAddress;
    };
    QList<JoinedInterface> m_joinedIfaces;
    // 是否还在系统默认网卡上入了组（离组时同样要离）
    bool m_joinedDefault;
    // 上一次报的入组网卡信息，内容没变就不重复报
    QString m_lastMembershipLog;
};

class MulticastClient : public QObject
{
    Q_OBJECT
public:
    // 构造：创建 worker，移动到独立线程，建立转发信号连接并启动线程
    explicit MulticastClient(QObject *parent = 0);
    // 析构：先停 worker，再退出线程并等待结束
    ~MulticastClient();
    // 线程安全配置接口：将参数异步投递到 worker 线程
    void configure(const QString &groupIp,
                   quint16 groupPort,
                   quint16 localPort,
                   const QString &ifaceName = QString(),
                   int heartbeatMs = 3000,
                   int heartbeatTimeoutMs = 10000,
                   int reconnectBaseMs = 1000,
                   int reconnectMaxMs = 15000,
                   const QString &communicationIp = QString());
    // 线程安全地更新设备通信地址，并在需要时切换组播网卡
    void setCommunicationAddress(const QString &communicationIp);
    // 线程安全启动接口：异步调用 worker->startWork()
    void start();
    // 线程安全停止接口：异步调用 worker->stopWork()
    void stop();
    // 线程安全发送接口：异步调用 worker->sendPayload()
    void send(const QByteArray &payload);
signals:
    // 向外透传收到的数据
    void datagramReceived(QByteArray data, QHostAddress sender, quint16 port);
    // 向外透传错误
    void errorOccurred(QString err);
    // 向外透传状态变化
    void stateChanged(QString state);
private:
    // worker 所在独立线程，承载全部 socket I/O 逻辑
    QThread m_thread;
    // 实际执行网络收发/心跳/重连逻辑的 worker 对象
    MulticastWorker *m_worker;
};
#endif // MULTICASTCLIENT_H
