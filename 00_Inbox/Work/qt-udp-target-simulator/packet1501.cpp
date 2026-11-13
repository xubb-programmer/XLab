#include "packet1501.h"

#include <QtEndian>
#include <cstring>
#include <limits>

namespace {

constexpr int HeaderLength = 26;
constexpr int ReconBodyLength = 138;
constexpr int BurstBodyLength = 52;

void writeDouble(uchar *destination, double value)
{
    static_assert(sizeof(double) == sizeof(quint64) && std::numeric_limits<double>::is_iec559,
                  "1501 requires IEEE 754 binary64");
    quint64 bits;
    std::memcpy(&bits, &value, sizeof(bits));
    qToLittleEndian(bits, destination);
}

void writeTime(uchar *destination, const QDateTime &reportedAt)
{
    const QDate date = reportedAt.date();
    const QTime time = reportedAt.time();
    qToLittleEndian<quint16>(date.year(), destination);
    destination[2] = date.month();
    destination[3] = date.day();
    destination[4] = time.hour();
    destination[5] = time.minute();
    destination[6] = time.second();
    qToLittleEndian<quint16>(time.msec(), destination + 7);
}

} // namespace

QByteArray encode1501(TargetKind kind, quint64 targetNumber, double longitude,
                      double latitude, double altitude, const QDateTime &reportedAt)
{
    const bool isBurst = kind == TargetKind::BurstPoint;
    const int bodyLength = isBurst ? BurstBodyLength : ReconBodyLength;
    QByteArray packet(HeaderLength + bodyLength, '\0');
    auto *header = reinterpret_cast<uchar *>(packet.data());
    header[0] = 0x90;
    header[1] = 0xeb;
    header[2] = 0x77;
    header[3] = 0x66;
    header[4] = 0x50;
    header[5] = isBurst ? 0x3c : 0x45;
    qToLittleEndian<quint16>(bodyLength, header + 9);
    header[14] = 0x08;
    header[15] = 0x01;
    header[19] = 0xb9;
    header[23] = 0x03;

    auto *body = header + HeaderLength;
    body[0] = isBurst ? 1 : 0;
    writeDouble(body + 1, static_cast<double>(targetNumber));
    writeDouble(body + 19, longitude);
    writeDouble(body + 27, latitude);

    if (isBurst) {
        body[9] = 1; // 炸点数量
        body[10] = 2; // 炸点类型
        writeDouble(body + 11, 1.0);
        qToLittleEndian<qint32>(static_cast<qint32>(altitude), body + 35);
        writeTime(body + 43, reportedAt);
    } else {
        body[9] = 1; // 有效标志
        writeTime(body + 10, reportedAt);
        writeDouble(body + 35, altitude);
        qToLittleEndian<quint16>(1, body + 91); // 地面目标
    }

    return packet;
}
