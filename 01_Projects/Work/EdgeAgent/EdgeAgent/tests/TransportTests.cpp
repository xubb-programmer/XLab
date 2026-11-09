#include "TransportCodec.h"
#include "FrameCodec.h"
#include "TcpChannel.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryFile>
#include <QThread>
#include <nlohmann/json.hpp>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace EdgeAgent;
using Json = nlohmann::json;
int checks = 0;
void require(bool condition, const char *message)
{
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
bool until(const std::function<bool()> &predicate, int timeout = 1000)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeout) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    return predicate();
}

void codecTests()
{
    SendRequest request;
    QString code, error;
    auto decode = [&](ChannelType type, const Json &json) {
        return decodeSendRequest(type, QByteArray::fromStdString(json.dump()), &request, &code, &error);
    };
    const QByteArray bytes = QByteArray::fromHex("0041ff0a");
    require(decodeSendRequest(ChannelType::Tcp, bytes, &request, &code, &error)
            && request.command.payload == bytes, "TCP binary body unchanged");
    require(!request.messageId.isEmpty(), "raw sends receive correlation ID");
    require(decodeSendRequest(ChannelType::Serial, "{\"speed\":10}", &request, &code, &error)
            && request.command.payload == "{\"speed\":10}", "business JSON body forwarded unchanged");
    Json command = {{"msg_id", "one"}, {"data", "48490A"}, {"connection_id", "server-1"}};
    require(decode(ChannelType::Tcp, command) && request.command.payload == "HI\n"
            && request.messageId == "one" && request.command.connectionId == "server-1", "HEX and connection routing");
    command["data"] = 123;
    require(!decode(ChannelType::Tcp, command) && code == "invalid_request", "typed JSON error never sent raw");
    for (const auto *text : {"A", "GG", "AA BB"}) {
        command["data"] = text;
        require(!decode(ChannelType::Serial, command), "invalid HEX rejected");
    }
    command = {{"operation", "exchange"}, {"data", "AA"}};
    require(!decode(ChannelType::Tcp, command), "unsupported operation rejected");
    command = {{"data", "AA"}, {"msg_id", 42}};
    require(!decode(ChannelType::Tcp, command), "invalid ID type rejected");
    require(!decodeSendRequest(ChannelType::Can, QByteArray(1024 * 1024 + 1, 'x'), &request, &code, &error)
            && code == "payload_too_large", "MQTT body size limit");
    require(!decodeSendRequest(ChannelType::Udp, "not-json", &request, &code, &error)
            && code == "invalid_json", "UDP requires JSON");

    command = {{"data", "A1B2"}, {"target_ip", "127.0.0.1"}, {"target_port", 19090}};
    require(decode(ChannelType::Udp, command) && request.command.payload.toHex() == "a1b2"
            && request.command.port == 19090, "UDP target and payload decoded");
    command["target_port"] = 1.5;
    require(!decode(ChannelType::Udp, command), "fractional port rejected");
    command["target_port"] = 65536;
    require(!decode(ChannelType::Udp, command), "out of range port rejected");
    command["target_port"] = 19090;
    command["target_ip"] = "invalid";
    require(!decode(ChannelType::Udp, command), "invalid destination rejected");

    command = {{"schema", "edge.can.v1"}, {"can_id", "0x1FFFFFFF"}, {"extended", true},
               {"rtr", false}, {"dlc", 2}, {"data", "A1B2"}};
    require(decode(ChannelType::Can, command) && request.command.canId == 0x1FFFFFFF, "extended CAN ID");
    command["extended"] = false;
    require(!decode(ChannelType::Can, command), "standard CAN ID bounds");
    command["can_id"] = "0x123";
    command["dlc"] = 3;
    require(!decode(ChannelType::Can, command), "CAN DLC must match bytes");
    command["rtr"] = true;
    require(!decode(ChannelType::Can, command), "RTR rejects data bytes");
    command["data"] = "";
    require(decode(ChannelType::Can, command) && request.command.remote && request.command.dlc == 3, "RTR carries requested DLC");
    command["extended"] = 1;
    require(!decode(ChannelType::Can, command), "CAN flag type checked");
}

void unifiedMessageTests()
{
    SendRequest request;
    QString code, error;
    auto decode = [&](ChannelType type, const Json &json) {
        return decodeSendRequest(type, QByteArray::fromStdString(json.dump()), &request, &code, &error);
    };
    for (const auto type : {ChannelType::Udp, ChannelType::Tcp, ChannelType::Serial, ChannelType::Can}) {
        FrameEvent event;
        event.transport = transportName(type);
        event.timestampMs = 123456789;
        event.payload = QByteArray::fromHex("00a1ff");
        event.peerAddress = "127.0.0.1";
        event.peerPort = 19090;
        event.endpoint = type == ChannelType::Serial ? "/dev/ttyUSB0" : "can0";
        if (type == ChannelType::Tcp) event.connectionId = "server-42";
        event.canId = 0x123;
        event.canDlc = 3;
        const auto json = Json::parse(encodeReceivedFrame(event, MessageFormat::Unified).toStdString());
        require(json.size() == 5 && json["id"].is_string() && !json["id"].get<std::string>().empty()
                && json["ts"] == 123456789 && json["transport"] == event.transport.toStdString()
                && json["data"] == "00A1FF", "same envelope and HEX for all transports");
        const auto &meta = json.at("meta");
        require(meta.at("endpoint").at("address").is_string(), "all endpoints use address string");
        require(meta.at("endpoint").contains("port") == (type == ChannelType::Udp || type == ChannelType::Tcp), "port only for network endpoints");
        require(meta.contains("connection") == (type == ChannelType::Tcp), "connection only for connected transport");
        require(meta.contains("frame") == (type == ChannelType::Can), "frame identifier is not an endpoint");
        require(decode(type, json) && request.command.payload == event.payload
                && request.messageId.toStdString() == json["id"], "uplink envelope can be used as addressed reply");
        if (type == ChannelType::Can) {
            require(meta["frame"] == Json{{"id", "0x123"}} && request.command.dlc == 3, "CAN default attributes and length inferred");
            event.canRemote = true;
            event.canExtended = true;
            event.canId = 0x12345678;
            event.canDlc = 8;
            event.payload.clear();
            const auto remote = Json::parse(encodeReceivedFrame(event, MessageFormat::Unified).toStdString());
            require(remote["data"] == "" && decode(type, remote) && request.command.remote
                    && request.command.extended && request.command.dlc == 8, "CAN remote frame roundtrip");
        }
        if (type == ChannelType::Tcp || type == ChannelType::Serial)
            require(encodeReceivedFrame(event, MessageFormat::Legacy) == event.payload, "legacy raw uplink unchanged");
    }

    Json json = {{"id", "cmd-1"}, {"data", "A1B2"}};
    require(decode(ChannelType::Serial, json) && request.messageId == "cmd-1", "minimal serial command");
    require(decode(ChannelType::Tcp, json) && request.command.requireUniqueTarget, "minimal TCP command requires unique target");
    require(!decode(ChannelType::Udp, json), "UDP requires an explicit endpoint");
    require(!decode(ChannelType::Can, json), "CAN requires frame ID");
    json["meta"] = {{"endpoint", {{"address", "127.0.0.1"}, {"port", 9000}}}};
    require(decode(ChannelType::Udp, json) && request.command.port == 9000, "uniform UDP destination");
    json["transport"] = "serial";
    require(!decode(ChannelType::Udp, json), "transport mismatch rejected");
    json.erase("transport");
    json["msg_id"] = "mixed";
    require(!decode(ChannelType::Udp, json), "mixed old and new envelope rejected");
    json.erase("msg_id");
    for (const auto badMeta : {Json(), Json::array(), Json("bad"), Json{{"peer", "wrong field"}},
            Json{{"endpoint", "wrong type"}}, Json{{"endpoint", {{"address", "127.0.0.1"}, {"port", 1.5}}}},
            Json{{"endpoint", {{"address", "127.0.0.1"}}}}, Json{{"endpoint", {{"address", ""}, {"port", 9000}}}}}) {
        json["meta"] = badMeta;
        require(!decode(ChannelType::Udp, json) && request.messageId == "cmd-1", "malformed metadata rejected with original ID");
    }
    json = {{"id", "cmd-1"}, {"data", "AA"}, {"meta", {{"connection", "client"}}}};
    require(!decode(ChannelType::Serial, json), "connection cannot be silently ignored on serial");
    json["meta"] = {{"frame", {{"id", "0x123"}}}};
    require(!decode(ChannelType::Tcp, json), "frame attributes cannot be silently ignored on TCP");
    for (const auto badTs : {Json(-1), Json(1.5), Json("now"), Json(std::numeric_limits<quint64>::max())}) {
        json = {{"id", "cmd-1"}, {"data", "AA"}, {"ts", badTs}};
        require(!decode(ChannelType::Serial, json), "invalid timestamp rejected");
    }
    for (const auto badId : {Json(1), Json(""), Json()}) {
        json = {{"id", badId}, {"data", "AA"}};
        require(!decode(ChannelType::Serial, json), "invalid message ID rejected");
    }
    require(!decode(ChannelType::Tcp, Json{{"id", "missing-data"}, {"meta", Json::object()}}), "malformed unified envelope never sent raw");

    json = {{"id", "can-1"}, {"data", "AA"}, {"meta", {{"frame", {{"id", "0x123"}}}}}};
    require(decode(ChannelType::Can, json), "minimal CAN command");
    json["meta"]["frame"]["length"] = 2;
    require(!decode(ChannelType::Can, json), "declared frame length verified");
    json["meta"]["frame"].erase("length");
    json["meta"]["frame"]["remote"] = true;
    json["data"] = "";
    require(!decode(ChannelType::Can, json), "remote frame requires requested length");
    json["meta"]["frame"]["length"] = 8;
    require(decode(ChannelType::Can, json) && request.command.remote, "valid remote frame");
    json["meta"]["frame"]["extended"] = 1;
    require(!decode(ChannelType::Can, json), "frame flag type checked");
    json["meta"]["frame"]["extended"] = false;
    json["meta"]["frame"]["id"] = "0x+123";
    require(!decode(ChannelType::Can, json), "noncanonical frame ID rejected");

    ChannelConfig config;
    config.type = ChannelType::Serial;
    config.serial.portName = "COM3";
    TransportCommand command;
    command.endpoint = "COM3";
    require(matchesLocalEndpoint(config, command), "serial endpoint constraint matches config");
    command.endpoint = "COM4";
    require(!matchesLocalEndpoint(config, command), "cannot switch serial port through metadata");
    config.type = ChannelType::Can;
    config.can.interfaceName = "can0";
    command.endpoint = "can1";
    require(!matchesLocalEndpoint(config, command), "cannot switch CAN interface through metadata");

    request.messageId = "cmd-42";
    auto result = Json::parse(encodeSendResult("d", request, true, "ok", "sent", MessageFormat::Unified).toStdString());
    require(result == Json{{"id", "cmd-42"}, {"status", "sent"}}, "minimal success receipt echoes ID");
    result = Json::parse(encodeSendResult("d", request, false, "invalid_request", "bad metadata", MessageFormat::Unified).toStdString());
    require(result["id"] == "cmd-42" && result["status"] == "failed" && result["error"]["code"] == "invalid_request", "failure receipt with structured error");
}

void frameTests()
{
    FrameConfig config;
    config.kind = FrameKind::FixedLength;
    config.fixedLength = 2;
    config.maxBytes = 4;
    FrameDecoder decoder(config);
    QVector<QByteArray> frames;
    QString error;
    require(decoder.feed("abcdefg", &frames, &error) && frames.size() == 3 && decoder.hasBuffered(), "coalesced fixed frames and tail");
    frames.clear();
    require(decoder.feed("h", &frames, &error) && frames == QVector<QByteArray>{"gh"}, "fixed partial frame completes");

    config.kind = FrameKind::Delimiter;
    config.delimiter = "\r\n";
    decoder = FrameDecoder(config);
    frames.clear();
    require(decoder.feed("abcd\r", &frames, &error) && frames.isEmpty(), "delimiter spans reads at maximum body size");
    require(decoder.feed("\nx\r\n", &frames, &error) && frames == QVector<QByteArray>({"abcd", "x"}), "multiple delimiter frames");
    require(!decoder.feed("abcde\r\n", &frames, &error), "oversized delimiter frame rejected");

    for (const auto order : {ByteOrder::BigEndian, ByteOrder::LittleEndian}) {
        config.kind = FrameKind::LengthPrefix;
        config.lengthBytes = 2;
        config.byteOrder = order;
        decoder = FrameDecoder(config);
        frames.clear();
        const auto prefix = QByteArray::fromHex(order == ByteOrder::BigEndian ? "0002" : "0200");
        require(decoder.feed(prefix.left(1), &frames, &error) && frames.isEmpty(), "partial length header buffered");
        require(decoder.feed(prefix.mid(1) + "ab" + prefix + "cd", &frames, &error)
                && frames == QVector<QByteArray>({"ab", "cd"}), "length prefix endian and coalesced frames");
    }
    config.kind = FrameKind::IdleTimeout;
    decoder = FrameDecoder(config);
    frames.clear();
    require(decoder.feed("ab", &frames, &error) && frames.isEmpty() && decoder.takeBuffered() == "ab", "idle data flushed explicitly");
    require(!decoder.feed("abcde", &frames, &error) && !decoder.hasBuffered(), "idle buffer limit");
}

bool loadJson(const Json &json, AgentConfig *config)
{
    QTemporaryFile file;
    if (!file.open()) throw std::runtime_error("temporary config");
    file.write(QByteArray::fromStdString(json.dump()));
    file.flush();
    QString error;
    return loadConfig(file.fileName(), config, &error);
}
void configTests()
{
    AgentConfig config;
    Json channel = {{"channel_id", "link"}, {"type", "tcp"}, {"role", "client"}, {"remote_host", "127.0.0.1"},
        {"remote_port", 19090}, {"frame", {{"kind", "delimiter"}, {"delimiter", "\n"}}}};
    Json device = {{"device_id", "a"}, {"channel_id", "link"}, {"qos", 1}};
    Json root = {{"config_version", 2}, {"node_id", "test"}, {"channels", Json::array({channel})}, {"devices", Json::array({device})}};
    require(loadJson(root, &config) && config.devices[0].qos == 1, "v2 transparent binding without protocol selection");
    require(config.devices[0].messageFormat == MessageFormat::Unified, "unified JSON is default");
    root["devices"][0]["message_format"] = "legacy";
    require(loadJson(root, &config) && config.devices[0].messageFormat == MessageFormat::Legacy, "explicit legacy format");
    root["devices"][0]["message_format"] = "wrong";
    require(!loadJson(root, &config), "unknown output format rejected");
    root["devices"][0].erase("message_format");
    root["devices"][0]["protocol"] = "transparent";
    require(loadJson(root, &config), "existing transparent declaration accepted");
    root["devices"][0]["protocol"] = "other";
    require(!loadJson(root, &config), "unsupported device mode rejected");
    root["devices"][0].erase("protocol");
    root["devices"][0]["unexpected"] = 1;
    require(!loadJson(root, &config), "unsupported device fields rejected");
    root["devices"][0].erase("unexpected");
    device["device_id"] = "b";
    root["devices"].push_back(device);
    require(!loadJson(root, &config), "duplicate channel binding rejected");
    root["devices"].erase(1);
    root["devices"][0]["channel_id"] = "missing";
    require(!loadJson(root, &config), "unknown channel rejected");
    root["devices"][0]["channel_id"] = "link";
    root["devices"][0]["qos"] = 2;
    require(!loadJson(root, &config), "unsupported QoS rejected");
    root = {{"node_id", "old"}, {"channels", Json::array({{{"channel_id", "s"}, {"device_id", "old_device"},
        {"type", "serial"}, {"port_name", "/dev/ttyS0"}, {"frame", {{"kind", "idle_timeout"}, {"idle_ms", 20}}}}})}};
    require(loadJson(root, &config) && config.devices[0].channelId == "s", "v1 binding preserved");
    root["channels"][0]["frame"]["kind"] = "unknown";
    require(!loadJson(root, &config), "unsupported framing rejected");

    root = {{"config_version", 2}, {"node_id", "switches"}, {"channels", Json::array({channel})},
            {"devices", Json::array({{{"device_id", "a"}, {"channel_id", "link"}}})}};
    root["channels"][0]["enabled"] = false;
    require(loadJson(root, &config) && config.channels.isEmpty() && config.devices.isEmpty(), "disabled v2 channel and Topic binding excluded");
    root["channels"][0]["enabled"] = "false";
    require(!loadJson(root, &config), "channel switch requires boolean");
    root["channels"][0]["enabled"] = true;
    require(loadJson(root, &config) && config.channels.size() == 1 && config.devices.size() == 1, "enabled channel and binding restored");
    root["channels"][0]["enabled"] = false;
    root["channels"][0]["frame"]["kind"] = "invalid";
    require(!loadJson(root, &config), "disabled templates still validated");
    root["channels"][0] = channel;
    root["channels"][0]["enabled"] = false;
    root["channels"][0]["device_id"] = "old_device";
    root["config_version"] = 1;
    root.erase("devices");
    require(loadJson(root, &config) && config.channels.isEmpty() && config.devices.isEmpty(), "disabled v1 channel and implicit binding excluded");
}

void tcpReconnectTest()
{
    QTcpServer server;
    require(server.listen(QHostAddress::LocalHost, 0), "reserve loopback port");
    const auto port = server.serverPort();
    server.close();
    ChannelConfig config;
    config.channelId = "tcp";
    config.type = ChannelType::Tcp;
    config.frame.kind = FrameKind::Delimiter;
    config.frame.delimiter = "\n";
    config.tcp.role = "client";
    config.tcp.remoteHost = "127.0.0.1";
    config.tcp.remotePort = port;
    config.tcp.reconnectInitialMs = 100;
    config.tcp.reconnectMaxMs = 200;
    TcpChannel client(config);
    bool ready = false;
    QVector<FrameEvent> received;
    QObject::connect(&client, &Channel::stateChanged, &client, [&](const QString &, const QString &s) { if (s == "ready") ready = true; });
    QObject::connect(&client, &Channel::frameReceived, &client, [&](const FrameEvent &event) { received.append(event); });
    client.start();
    until([] { return false; }, 30);
    require(server.listen(QHostAddress::LocalHost, port), "start server after refused connection");
    require(until([&] { return ready && server.hasPendingConnections(); }, 1500), "TCP reconnect ready");
    auto *socket = server.nextPendingConnection();
    TransportCommand command;
    command.payload = "hello";
    require(client.send(command), "channel accepts transparent send");
    require(until([&] { return socket->bytesAvailable() == 5; }) && socket->readAll() == "hello", "outgoing TCP bytes unchanged");
    socket->write("abc\ndef\n");
    require(until([&] { return received.size() == 2; }) && received[0].payload == "abc"
            && received[1].payload == "def", "incoming TCP frames unchanged");
    client.stop();
    socket->deleteLater();
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    try {
        codecTests();
        unifiedMessageTests();
        frameTests();
        configTests();
        tcpReconnectTest();
        if (argc > 1) {
            AgentConfig config;
            QString error;
            const auto path = QString::fromLocal8Bit(argv[1]) + "/config.json";
            require(loadConfig(path, &config, &error), (path + ": " + error).toStdString().c_str());
            require(config.channels.size() == 2 && config.devices.size() == 2, "single config starts only UDP and TCP server by default");
            QFile file(path);
            require(file.open(QIODevice::ReadOnly), "read unified configuration");
            Json json = Json::parse(file.readAll().toStdString());
            for (auto &channel : json["channels"]) channel["enabled"] = true;
            require(loadJson(json, &config) && config.channels.size() == 6 && config.devices.size() == 6, "all six templates load together without editing another file");
        }
        std::cout << "PASS " << checks << " checks\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
