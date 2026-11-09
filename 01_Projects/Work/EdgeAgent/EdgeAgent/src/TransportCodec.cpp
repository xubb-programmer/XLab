#include "TransportCodec.h"
#include <nlohmann/json.hpp>
#include <QUuid>
#include <QDateTime>
#include <limits>
#include <stdexcept>

namespace EdgeAgent {
namespace {
using Json = nlohmann::json;

int number(const Json &object, const char *key, int minimum, int maximum)
{
    const auto &value = object.at(key);
    if (!value.is_number_integer()
        || (value.is_number_unsigned() && value.get<quint64>() > static_cast<quint64>(maximum)))
        throw std::runtime_error("integer field out of range");
    const qint64 result = value.get<qint64>();
    if (result < minimum || result > maximum) throw std::runtime_error("integer field out of range");
    return static_cast<int>(result);
}

bool decodeHex(const QString &text, QByteArray *bytes)
{
    if (text.size() % 2) return false;
    for (const QChar c : text) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return false;
    }
    *bytes = QByteArray::fromHex(text.toLatin1());
    return true;
}

void checkKeys(const Json &object, std::initializer_list<const char *> keys)
{
    if (!object.is_object()) throw std::runtime_error("expected object");
    for (auto it = object.cbegin(); it != object.cend(); ++it) {
        bool known = false;
        for (const auto *key : keys) if (it.key() == key) known = true;
        if (!known) throw std::runtime_error("unsupported field: " + it.key());
    }
}

QString nonEmptyString(const Json &value)
{
    const QString text = QString::fromStdString(value.get<std::string>());
    if (text.isEmpty()) throw std::runtime_error("empty string");
    return text;
}

void decodeUnified(ChannelType type, const Json &json, TransportCommand *command)
{
    checkKeys(json, {"id", "ts", "transport", "data", "meta"});
    if (json.contains("transport") && json.at("transport") != transportName(type).toStdString())
        throw std::runtime_error("transport does not match configured channel");
    if (json.contains("ts")) {
        const auto &ts = json.at("ts");
        if (!ts.is_number_integer()
            || (ts.is_number_unsigned() && ts.get<quint64>() > static_cast<quint64>(std::numeric_limits<qint64>::max()))
            || ts.get<qint64>() < 0) throw std::runtime_error("invalid timestamp");
    }
    const Json meta = json.value("meta", Json::object());
    checkKeys(meta, {"endpoint", "connection", "frame"});
    if (meta.contains("endpoint")) {
        const auto &endpoint = meta.at("endpoint");
        checkKeys(endpoint, {"address", "port"});
        command->endpoint = nonEmptyString(endpoint.at("address"));
        if (type == ChannelType::Udp || type == ChannelType::Tcp) {
            if (!command->address.setAddress(command->endpoint)) throw std::runtime_error("invalid endpoint IP");
            command->port = static_cast<quint16>(number(endpoint, "port", 1, 65535));
        } else if (endpoint.contains("port")) {
            throw std::runtime_error("port only applies to network endpoints");
        }
    }
    if (type == ChannelType::Udp && command->endpoint.isEmpty()) throw std::runtime_error("UDP endpoint required");
    if (meta.contains("connection")) {
        if (type != ChannelType::Tcp) throw std::runtime_error("connection only applies to TCP");
        command->connectionId = nonEmptyString(meta.at("connection"));
    }
    command->requireUniqueTarget = type == ChannelType::Tcp;
    if (type != ChannelType::Can) {
        if (meta.contains("frame")) throw std::runtime_error("frame attributes not supported by this transport");
        return;
    }
    const auto &frame = meta.at("frame");
    checkKeys(frame, {"id", "extended", "remote", "length"});
    command->extended = frame.value("extended", false);
    command->remote = frame.value("remote", false);
    command->dlc = frame.contains("length") ? number(frame, "length", 0, 8) : command->payload.size();
    if (command->remote && !frame.contains("length")) throw std::runtime_error("remote frame requires length");
    const QString id = nonEmptyString(frame.at("id"));
    bool ok = id.startsWith("0x", Qt::CaseInsensitive) && id.size() > 2;
    for (int i = 2; i < id.size(); ++i) {
        const auto c = id.at(i);
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) ok = false;
    }
    bool converted = false;
    const auto value = id.mid(2).toULongLong(&converted, 16);
    if (!ok || !converted || value > (command->extended ? 0x1FFFFFFFULL : 0x7FFULL))
        throw std::runtime_error("invalid frame ID");
    command->canId = static_cast<quint32>(value);
    if (command->dlc > 8 || (command->remote ? !command->payload.isEmpty() : command->payload.size() != command->dlc))
        throw std::runtime_error("frame length does not match data");
}

QByteArray serialize(const Json &value) { return QByteArray::fromStdString(value.dump()); }
}

bool decodeSendRequest(ChannelType type, const QByteArray &payload, SendRequest *request,
                       QString *code, QString *error)
{
    auto fail = [code, error](const QString &reason, const QString &message) {
        if (code) *code = reason;
        if (error) *error = message;
        return false;
    };
    if (!request) return fail("invalid_request", "发送请求输出对象为空");
    *request = SendRequest{};
    request->messageId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (code) code->clear();
    if (error) error->clear();
    if (payload.size() > 1024 * 1024) return fail("payload_too_large", "命令超过 1MiB");

    const bool rawAllowed = type == ChannelType::Tcp || type == ChannelType::Serial;
    const Json json = Json::parse(payload.constData(), payload.constData() + payload.size(), nullptr, false);
    const bool unified = json.is_object() && (json.contains("id") || json.contains("ts")
        || json.contains("transport") || json.contains("meta"));
    if (rawAllowed && (!json.is_object() || (!unified && !json.contains("data") && !json.contains("operation")))) {
        request->command.payload = payload;
        return true;
    }
    if (!json.is_object()) return fail("invalid_json", "命令必须是 JSON 对象");
    try {
        const bool legacy = !unified && (json.contains("msg_id") || json.contains("operation") || json.contains("schema")
            || json.contains("target_ip") || json.contains("target_port") || json.contains("connection_id") || json.contains("can_id"));
        if (!legacy) {
            if (json.contains("id")) request->messageId = nonEmptyString(json.at("id"));
            if (!decodeHex(QString::fromStdString(json.at("data").get<std::string>()), &request->command.payload))
                return fail("invalid_request", "data 必须是偶数个十六进制字符且不能包含空格");
            decodeUnified(type, json, &request->command);
            return true;
        }
        const QString id = QString::fromStdString(json.value("msg_id", request->messageId.toStdString()));
        if (!id.isEmpty()) request->messageId = id;
        request->operation = QString::fromStdString(json.value("operation", std::string("send")));
        if (request->operation != "send") return fail("invalid_request", "仅支持 send 发送操作");
        auto &command = request->command;
        if (!decodeHex(QString::fromStdString(json.at("data").get<std::string>()), &command.payload))
            return fail("invalid_request", "data 必须是偶数个十六进制字符且不能包含空格");
        command.connectionId = QString::fromStdString(json.value("connection_id", std::string()));
        if (type == ChannelType::Udp) {
            if (!command.address.setAddress(QString::fromStdString(json.at("target_ip").get<std::string>())))
                return fail("invalid_request", "target_ip 无效");
            command.port = static_cast<quint16>(number(json, "target_port", 1, 65535));
        } else if (type == ChannelType::Can) {
            if (json.at("schema") != "edge.can.v1") return fail("invalid_request", "CAN schema 必须是 edge.can.v1");
            command.extended = json.at("extended").get<bool>();
            command.remote = json.at("rtr").get<bool>();
            command.dlc = number(json, "dlc", 0, 8);
            const QString idText = QString::fromStdString(json.at("can_id").get<std::string>());
            bool ok = false;
            const qulonglong idValue = idText.mid(2).toULongLong(&ok, 16);
            if (!idText.startsWith("0x", Qt::CaseInsensitive) || !ok
                || idValue > (command.extended ? 0x1FFFFFFFULL : 0x7FFULL))
                return fail("invalid_request", "CAN ID 超出范围");
            command.canId = static_cast<quint32>(idValue);
            if ((command.remote && !command.payload.isEmpty())
                || (!command.remote && command.payload.size() != command.dlc))
                return fail("invalid_request", "CAN 数据与 DLC 不匹配");
        }
        return true;
    } catch (const std::exception &) {
        return fail("invalid_request", "发送命令字段缺失、类型错误或超出范围");
    }
}

QString transportName(ChannelType type)
{
    switch (type) {
    case ChannelType::Udp: return QStringLiteral("udp");
    case ChannelType::Tcp: return QStringLiteral("tcp");
    case ChannelType::Serial: return QStringLiteral("serial");
    case ChannelType::Can: return QStringLiteral("can");
    }
    return {};
}

bool matchesLocalEndpoint(const ChannelConfig &config, const TransportCommand &command)
{
    if (command.endpoint.isEmpty()) return true;
    if (config.type == ChannelType::Serial) return command.endpoint == config.serial.portName;
    if (config.type == ChannelType::Can) return command.endpoint == config.can.interfaceName;
    return true;
}

QByteArray encodeReceivedFrame(const FrameEvent &event, MessageFormat format)
{
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    const auto data = event.payload.toHex().toUpper().toStdString();
    const auto frameId = QStringLiteral("0x%1").arg(event.canId, event.canExtended ? 8 : 3, 16, QLatin1Char('0')).toStdString();
    if (format == MessageFormat::Legacy) {
        if (event.transport == "udp")
            return serialize({{"schema", "edge.udp.v1"}, {"msg_id", id}, {"src_ip", event.peerAddress.toStdString()},
                {"src_port", event.peerPort}, {"ts", event.timestampMs}, {"replay", false}, {"len", event.payload.size()}, {"data", data}});
        if (event.transport == "can")
            return serialize({{"schema", "edge.can.v1"}, {"msg_id", id}, {"can_id", frameId}, {"extended", event.canExtended},
                {"rtr", event.canRemote}, {"dlc", event.canDlc}, {"ts", event.timestampMs}, {"replay", false},
                {"data", event.canRemote ? std::string() : data}});
        return event.payload;
    }
    const bool network = event.transport == "udp" || event.transport == "tcp";
    Json endpoint = {{"address", (network ? event.peerAddress : event.endpoint).toStdString()}};
    if (network) endpoint["port"] = event.peerPort;
    Json meta = {{"endpoint", endpoint}};
    if (!event.connectionId.isEmpty()) meta["connection"] = event.connectionId.toStdString();
    if (event.transport == "can") {
        Json frame = {{"id", frameId}};
        if (event.canExtended) frame["extended"] = true;
        if (event.canRemote) {
            frame["remote"] = true;
            frame["length"] = event.canDlc;
        }
        meta["frame"] = frame;
    }
    return serialize({{"id", id}, {"ts", event.timestampMs}, {"transport", event.transport.toStdString()},
                      {"data", event.transport == "can" && event.canRemote ? std::string() : data}, {"meta", meta}});
}

QByteArray encodeSendResult(const QString &deviceId, const SendRequest &request, bool sent,
                           const QString &code, const QString &message, MessageFormat format)
{
    if (format == MessageFormat::Unified) {
        Json result = {{"id", request.messageId.toStdString()}, {"status", sent ? "sent" : "failed"}};
        if (!sent) result["error"] = {{"code", code.toStdString()}, {"message", message.toStdString()}};
        return serialize(result);
    }
    Json info = Json::object();
    if (!request.command.connectionId.isEmpty()) info["connection_id"] = request.command.connectionId.toStdString();
    Json result = {{"schema", "edge.command-result.v1"}, {"device_id", deviceId.toStdString()},
        {"msg_id", request.messageId.toStdString()}, {"operation", request.operation.toStdString()},
        {"stage", sent ? "sent" : "failed"}, {"code", code.toStdString()}, {"message", message.toStdString()},
        {"ts", QDateTime::currentMSecsSinceEpoch()}, {"result", info}};
    if (info.contains("connection_id")) result["connection_id"] = info.at("connection_id");
    return serialize(result);
}

} // namespace EdgeAgent
