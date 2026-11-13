#ifndef TARGETINFOSOCKET_H
#define TARGETINFOSOCKET_H

#include <QObject>
#include <QUdpSocket>
class TargetInfoSocket : public QUdpSocket
{
    Q_OBJECT
public:
    TargetInfoSocket(QObject *parent = nullptr);
    ~TargetInfoSocket();

    bool init();

    bool getConfig(); //读取配置文件(获取与车长席通信IP和端口)
    /**
     * @brief 获取本机所有IPv4地址
     * @return IPv4地址列表
     */
    QStringList getAllLocalIPv4Addresses();

    /**
     * @brief 获取首选本机IPv4地址
     * @return 首选IPv4地址，如果获取失败返回空字符串
     */
    QString getPreferredLocalIPv4Address();

public slots:
    int sendGDTargetArray(QByteArray ba);

signals:
    void dataArrive(QByteArray);

private:
    QHostAddress m_localaAddress;
    qint64 m_localPort;
    QHostAddress m_czAddress; //车长席ip地址
    qint64 m_czPort; //车长席端口   
    QUdpSocket *m_udpSocket;
};

#endif // TARGETINFOSOCKET_H
