#pragma once

#include "Config.h"
#include "TransportCommand.h"
#include "FrameEvent.h"

namespace EdgeAgent {

/** MQTT 发送信封。仅描述字节和传输元数据，不解释设备业务。 */
struct SendRequest {
    QString messageId;
    QString operation = QStringLiteral("send");
    TransportCommand command;
};

/** TCP/串口兼容原始正文；结构化命令使用严格 HEX 和字段校验。 */
bool decodeSendRequest(ChannelType type, const QByteArray &payload, SendRequest *request,
                       QString *code, QString *error);

QString transportName(ChannelType type);
QByteArray encodeReceivedFrame(const FrameEvent &event, MessageFormat format);
QByteArray encodeSendResult(const QString &deviceId, const SendRequest &request, bool sent,
                           const QString &code, const QString &message, MessageFormat format);

/** 校验本地接口约束；不能通过消息切换串口或 CAN 物理接口。 */
bool matchesLocalEndpoint(const ChannelConfig &config, const TransportCommand &command);

} // namespace EdgeAgent
