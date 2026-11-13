#include "packet1501.h"

#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QSpinBox>
#include <QTimer>
#include <QUdpSocket>
#include <QVBoxLayout>
#include <limits>

namespace {

struct Position {
    double longitude;
    double latitude;
    double altitude;
};

struct Destination {
    QHostAddress address;
    quint16 port;
    quint64 targetNumber;
    TargetKind kind;
};

QDoubleSpinBox *coordinateInput(double minimum, double maximum, double value, int decimals = 7)
{
    auto *input = new QDoubleSpinBox;
    input->setDecimals(decimals);
    input->setRange(minimum, maximum);
    input->setSingleStep(decimals == 7 ? 0.001 : 1.0);
    input->setValue(value);
    return input;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QWidget window;
    window.setWindowTitle(QStringLiteral("033（1501）UDP 目标模拟器"));
    window.resize(580, 710);

    auto *layout = new QVBoxLayout(&window);
    auto *connection = new QGroupBox(QStringLiteral("EdgeAgent UDP 接收地址"));
    auto *connectionForm = new QFormLayout(connection);
    auto *host = new QLineEdit(QStringLiteral("127.0.0.1"));
    auto *port = new QSpinBox;
    port->setRange(1, 65535);
    port->setValue(18080);
    connectionForm->addRow(QStringLiteral("目标 IP"), host);
    connectionForm->addRow(QStringLiteral("目标端口"), port);
    layout->addWidget(connection);

    auto *report = new QGroupBox(QStringLiteral("033（1501）测试数据"));
    auto *reportForm = new QFormLayout(report);
    auto *kind = new QComboBox;
    kind->addItem(QStringLiteral("侦察目标 · 0x45 · 164 字节"));
    kind->addItem(QStringLiteral("炸点 · 0x3C · 78 字节"));

    auto *number = new QLineEdit(QStringLiteral("1001"));
    number->setMaxLength(10);
    number->setValidator(new QRegularExpressionValidator(QRegularExpression("[0-9]{1,10}"), number));

    const auto minAltitude = std::numeric_limits<qint32>::min();
    const auto maxAltitude = std::numeric_limits<qint32>::max();
    auto *longitude = coordinateInput(-180.0, 180.0, 118.52);
    auto *latitude = coordinateInput(-90.0, 90.0, 24.52);
    auto *altitude = coordinateInput(minAltitude, maxAltitude, 50.0, 3);

    reportForm->addRow(QStringLiteral("报文类型"), kind);
    reportForm->addRow(QStringLiteral("编号"), number);
    reportForm->addRow(QStringLiteral("起点经度（°）"), longitude);
    reportForm->addRow(QStringLiteral("起点纬度（°）"), latitude);
    reportForm->addRow(QStringLiteral("起点高程（米）"), altitude);
    layout->addWidget(report);

    auto *trajectory = new QGroupBox(QStringLiteral("连续航迹"));
    auto *trajectoryForm = new QFormLayout(trajectory);
    auto *endLongitude = coordinateInput(-180.0, 180.0, 118.56);
    auto *endLatitude = coordinateInput(-90.0, 90.0, 24.55);
    auto *endAltitude = coordinateInput(minAltitude, maxAltitude, 50.0, 3);
    auto *pointCount = new QSpinBox;
    pointCount->setRange(2, 10000);
    pointCount->setValue(20);
    auto *interval = new QSpinBox;
    interval->setRange(100, 60000);
    interval->setValue(1000);
    trajectoryForm->addRow(QStringLiteral("终点经度（°）"), endLongitude);
    trajectoryForm->addRow(QStringLiteral("终点纬度（°）"), endLatitude);
    trajectoryForm->addRow(QStringLiteral("终点高程（米）"), endAltitude);
    trajectoryForm->addRow(QStringLiteral("点数（含起终点）"), pointCount);
    trajectoryForm->addRow(QStringLiteral("发送间隔（毫秒）"), interval);
    layout->addWidget(trajectory);

    auto *send = new QPushButton(QStringLiteral("发送单点"));
    auto *sendTrajectory = new QPushButton(QStringLiteral("发送连续航迹"));
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(send);
    buttons->addWidget(sendTrajectory);
    auto *status = new QLabel(QStringLiteral("就绪"));
    status->setWordWrap(true);
    auto *hex = new QPlainTextEdit;
    hex->setReadOnly(true);
    hex->setPlaceholderText(QStringLiteral("发送后显示报文的十六进制内容"));
    layout->addLayout(buttons);
    layout->addWidget(status);
    layout->addWidget(hex, 1);

    QUdpSocket socket;
    QTimer trajectoryTimer;
    trajectoryTimer.setTimerType(Qt::PreciseTimer);
    Destination routeDestination{};
    Position routeStart{};
    Position routeEnd{};
    int routePointCount = 0;
    int nextPoint = 0;

    const auto readDestination = [&](Destination &destination) {
        if (!destination.address.setAddress(host->text().trimmed())) {
            status->setText(QStringLiteral("请输入有效的目标 IP 地址。"));
            return false;
        }
        const auto targetNumber = number->text().toULongLong();
        if (!number->hasAcceptableInput() || targetNumber == 0) {
            status->setText(QStringLiteral("请输入 1 至 10 位数字的正整数编号。"));
            return false;
        }
        destination.port = static_cast<quint16>(port->value());
        destination.targetNumber = targetNumber;
        destination.kind = kind->currentIndex() == 0 ? TargetKind::Reconnaissance : TargetKind::BurstPoint;
        return true;
    };

    const auto sendPosition = [&](const Destination &destination, const Position &position) {
        const auto packet = encode1501(destination.kind, destination.targetNumber, position.longitude,
                                       position.latitude, position.altitude, QDateTime::currentDateTime());
        const auto sent = socket.writeDatagram(packet, destination.address, destination.port);
        if (sent != packet.size()) {
            status->setText(QStringLiteral("发送失败：%1").arg(socket.errorString()));
            return false;
        }
        hex->setPlainText(QString::fromLatin1(packet.toHex(' ').toUpper()));
        return true;
    };

    const auto setSending = [&](bool sending) {
        connection->setEnabled(!sending);
        report->setEnabled(!sending);
        trajectory->setEnabled(!sending);
        send->setEnabled(!sending);
        sendTrajectory->setText(sending ? QStringLiteral("停止发送") : QStringLiteral("发送连续航迹"));
        if (!sending) trajectoryTimer.stop();
    };

    const auto sendNextPoint = [&] {
        const double fraction = static_cast<double>(nextPoint) / (routePointCount - 1);
        const auto interpolate = [fraction](double start, double end) {
            return start + fraction * (end - start);
        };
        const Position position{
            interpolate(routeStart.longitude, routeEnd.longitude),
            interpolate(routeStart.latitude, routeEnd.latitude),
            interpolate(routeStart.altitude, routeEnd.altitude)
        };
        if (!sendPosition(routeDestination, position)) {
            setSending(false);
            return;
        }
        ++nextPoint;
        if (nextPoint == routePointCount) {
            setSending(false);
            status->setText(QStringLiteral("航迹发送完成，共 %1 点").arg(nextPoint));
        } else {
            status->setText(QStringLiteral("正在发送航迹：%1 / %2 点").arg(nextPoint).arg(routePointCount));
        }
    };
    QObject::connect(&trajectoryTimer, &QTimer::timeout, &window, sendNextPoint);

    QObject::connect(send, &QPushButton::clicked, &window, [&] {
        Destination destination{};
        const Position position{longitude->value(), latitude->value(), altitude->value()};
        if (readDestination(destination) && sendPosition(destination, position)) {
            status->setText(QStringLiteral("已向 %1:%2 发送单点")
                            .arg(destination.address.toString()).arg(destination.port));
        }
    });

    QObject::connect(sendTrajectory, &QPushButton::clicked, &window, [&] {
        if (trajectoryTimer.isActive()) {
            setSending(false);
            status->setText(QStringLiteral("已停止，共发送 %1 点").arg(nextPoint));
            return;
        }
        if (!readDestination(routeDestination)) return;
        routeStart = {longitude->value(), latitude->value(), altitude->value()};
        routeEnd = {endLongitude->value(), endLatitude->value(), endAltitude->value()};
        const bool sameAltitude = routeDestination.kind == TargetKind::BurstPoint
            ? static_cast<qint32>(routeStart.altitude) == static_cast<qint32>(routeEnd.altitude)
            : routeStart.altitude == routeEnd.altitude;
        if (routeStart.longitude == routeEnd.longitude && routeStart.latitude == routeEnd.latitude && sameAltitude) {
            status->setText(QStringLiteral("起点和终点需要使用不同的坐标。"));
            return;
        }
        routePointCount = pointCount->value();
        nextPoint = 0;
        setSending(true);
        trajectoryTimer.start(interval->value());
        sendNextPoint();
    });

    window.show();
    return app.exec();
}
