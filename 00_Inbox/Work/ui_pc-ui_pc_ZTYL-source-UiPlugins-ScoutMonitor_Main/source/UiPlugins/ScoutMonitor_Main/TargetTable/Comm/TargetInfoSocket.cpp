#include "TargetInfoSocket.h"
#include <QSettings>
#include <QApplication>
#include <QDebug>
#include <QMessageBox>
#include <QNetworkAddressEntry>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QTextCodec>
#include <QJsonDocument>
#include <QJsonObject>
#include "FrameLogManager.h"

TargetInfoSocket::TargetInfoSocket(QObject *parent) : QUdpSocket(parent)
{
    m_udpSocket=new QUdpSocket(this);

    connect(this,&TargetInfoSocket::readyRead,this,[this](){
       while(hasPendingDatagrams()) {
           QByteArray ba;
           ba.resize(pendingDatagramSize());
           readDatagram(ba.data(),ba.size());
#if 0
           foreach (unsigned char v, ba) {
               qDebug()<<QString::number(v,16)<<ba.size();
           }
#endif
           emit dataArrive(ba);
       }
    });
}

TargetInfoSocket::~TargetInfoSocket()
{
    ;
}

bool TargetInfoSocket::init()
{
    return getConfig();
}

int TargetInfoSocket::sendGDTargetArray(QByteArray ba)
{
    //qDebug() << "kkksssssss11111111" << QString(ba.toHex());
    getConfig();
    return  QUdpSocket().writeDatagram(ba, m_czAddress, m_czPort);
}



bool TargetInfoSocket::getConfig()
{
    QString LocalIPv4=getPreferredLocalIPv4Address();
    QStringList ipv4List = getAllLocalIPv4Addresses();

    QString iniPath  = QApplication::applicationDirPath() + "/config/communication.ini";
    QSettings reader (iniPath, QSettings::IniFormat) ;
    m_czPort     =  reader.value("/Address/CzPort").toInt();
    m_czAddress =  reader.value("/Address/CzIp").toString();
    m_localPort     =  reader.value("/Address/LocalPort").toInt();
    m_localaAddress =  reader.value("/Address/LocalIp").toString();

    if(!ipv4List.contains(m_localaAddress.toString())){
        QSettings settings1(QApplication::applicationDirPath() + "/config/communication.ini" , QSettings::IniFormat);
        settings1.setIniCodec(QTextCodec::codecForName("GBK"));
        settings1.beginGroup("Address");
        settings1.setValue("LocalIp",LocalIPv4);
        settings1.endGroup();
        m_localaAddress=LocalIPv4;
    }



   if(m_udpSocket->state()==QAbstractSocket::BoundState){
       m_udpSocket->abort();
   }
   bool res = m_udpSocket->bind(m_localPort,QUdpSocket::DontShareAddress);

   if(!res) {
       QString tip = QString("fail to bind ip %1 port %2").arg(m_localaAddress.toString()).arg(QString::number(m_localPort));
       QMessageBox::critical(nullptr,"TargetInfoSocket error!",tip);
   }
   return res;
}


/**
 * @brief 获取本机所有IPv4地址
 * @return IPv4地址列表
 */
QStringList TargetInfoSocket::getAllLocalIPv4Addresses()
{
    QStringList ipList;

    // 获取所有网络接口
    QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();

    for (const QNetworkInterface &interface : interfaces) {
        // 跳过非活动接口和回环接口
        if (!interface.isValid() ||
            interface.flags().testFlag(QNetworkInterface::IsLoopBack) ||
            !interface.flags().testFlag(QNetworkInterface::IsUp) ||
            !interface.flags().testFlag(QNetworkInterface::IsRunning)) {
            continue;
        }

        // 获取接口的所有IP地址
        QList<QNetworkAddressEntry> entries = interface.addressEntries();
        for (const QNetworkAddressEntry &entry : entries) {
            QHostAddress ip = entry.ip();

            // 只保留IPv4地址，跳过IPv6
            if (ip.protocol() == QAbstractSocket::IPv4Protocol) {
                QString ipStr = ip.toString();

                // 跳过本地回环地址和自动配置地址
                if (!ipStr.startsWith("127.") &&
                    !ipStr.startsWith("169.254.") &&
                    !ipList.contains(ipStr)) {
                    ipList.append(ipStr);
                }
            }
        }
    }

    return ipList;
}

/**
 * @brief 获取首选本机IPv4地址
 * @return 首选IPv4地址，如果获取失败返回空字符串
 */
QString TargetInfoSocket::getPreferredLocalIPv4Address()
{
    QStringList ipList = getAllLocalIPv4Addresses();

    if (ipList.isEmpty()) {
        return QString();
    }

    // 优先选择以太网或无线网络地址
    // 1. 优先选择非192.168开头的地址（可能是公网或专用网络）
    for (const QString &ip : ipList) {
        if (!ip.startsWith("192.168.") &&
            !ip.startsWith("10.") &&
            !ip.startsWith("172.")) {
            return ip;
        }
    }

    // 2. 优先选择192.168开头的地址（常见家庭/办公室网络）
    for (const QString &ip : ipList) {
        if (ip.startsWith("192.168.")) {
            return ip;
        }
    }

    // 3. 选择10开头的地址（常见企业网络）
    for (const QString &ip : ipList) {
        if (ip.startsWith("10.")) {
            return ip;
        }
    }

    // 4. 选择172开头的地址（较大企业网络）
    for (const QString &ip : ipList) {
        if (ip.startsWith("172.")) {
            return ip;
        }
    }

    // 5. 返回第一个地址
    return ipList.first();
}
