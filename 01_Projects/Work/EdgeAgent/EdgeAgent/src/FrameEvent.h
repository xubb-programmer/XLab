/**
 * @file FrameEvent.h
 * @brief UDP、TCP、串口和 CAN 数据统一事件定义。
 * @author xubb
 */

#pragma once

#include <QByteArray>
#include <QMetaType>
#include <QString>
#include <QtGlobal>

namespace EdgeAgent {

/**
 * @brief 传输层收到一帧完整数据后的统一事件。
 *
 * UDP、TCP、串口和 CAN 通道都转换为此结构，再由 EdgeAgent 负责日志记录和 MQTT 上报。
 */
struct FrameEvent {
    QString channelId; ///< 产生事件的通道标识。
    QString deviceId; ///< 关联设备标识。
    QString transport; ///< 传输类型：udp、tcp、serial 或 can。
    QByteArray payload; ///< 收到的原始数据。
    QString peerAddress; ///< 对端 IP 地址。
    quint16 peerPort = 0; ///< 对端端口。
    QString connectionId; ///< TCP 连接标识，UDP、串口和 CAN 为空。
    QString endpoint; ///< 串口设备名或 CAN 接口名；网络通道为空。
    quint32 canId = 0; ///< CAN 帧 ID。
    bool canExtended = false; ///< CAN 是否为扩展帧。
    bool canRemote = false; ///< CAN 是否为远程请求帧。
    quint8 canDlc = 0; ///< CAN 数据长度码，经典 CAN 范围为 0 到 8。
    qint64 timestampMs = 0; ///< 收包时间戳，单位为毫秒。
};

} // 命名空间 EdgeAgent

Q_DECLARE_METATYPE(EdgeAgent::FrameEvent)
