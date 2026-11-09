#pragma once

#include <QByteArray>
#include <QHostAddress>
#include <QString>

namespace EdgeAgent {

/** 传输发送描述，不包含设备业务操作或 MQTT 信息。 */
struct TransportCommand {
    QByteArray payload;
    QString endpoint; ///< 可选端点约束：IP、串口名或 CAN 接口名。
    QHostAddress address;
    quint16 port = 0;
    QString connectionId;
    quint32 canId = 0;
    bool extended = false;
    bool remote = false;
    int dlc = 0;
    bool requireUniqueTarget = false; ///< 统一 JSON 发送不允许从多个 TCP 对端中任意选择。
};

} // namespace EdgeAgent
