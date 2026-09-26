/**
 * @file TestFrameDecoder.cpp
 * @brief 测试客户端字节流分帧器实现。
 * @author xubb
 */

#include "TestFrameDecoder.h"

#include <QtGlobal>

namespace edgeagent {

TestFrameDecoder::TestFrameDecoder() = default;

void TestFrameDecoder::configure(TestFrameKind kind,
                                 const QByteArray &delimiter,
                                 int fixedLength,
                                 int lengthBytes,
                                 bool littleEndian,
                                 int maxBytes)
{
    m_kind = kind;
    m_delimiter = delimiter;
    m_fixedLength = qMax(fixedLength, 1);
    m_lengthBytes = qBound(1, lengthBytes, 4);
    m_littleEndian = littleEndian;
    m_maxBytes = qMax(maxBytes, 1);
    reset();
}

bool TestFrameDecoder::feed(const QByteArray &data, QVector<QByteArray> *frames, QString *error)
{
    if (frames == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("分帧输出对象为空");
        }
        return false;
    }
    if (!data.isEmpty()) {
        m_buffer.append(data);
    }
    if (m_buffer.size() > m_maxBytes + m_lengthBytes) {
        m_buffer.clear();
        if (error != nullptr) {
            *error = QStringLiteral("接收缓存超过最大帧长度 %1 字节").arg(m_maxBytes);
        }
        return false;
    }

    if (m_kind == TestFrameKind::IdleTimeout) {
        return true;
    }
    if (m_kind == TestFrameKind::Delimiter) {
        if (m_delimiter.isEmpty()) {
            if (error != nullptr) {
                *error = QStringLiteral("分隔符不能为空");
            }
            return false;
        }
        while (true) {
            const int end = m_buffer.indexOf(m_delimiter); // 查找当前缓存中的完整分隔符。
            if (end < 0) {
                break;
            }
            if (end > m_maxBytes) {
                m_buffer.clear();
                if (error != nullptr) {
                    *error = QStringLiteral("分隔符帧超过最大长度 %1 字节").arg(m_maxBytes);
                }
                return false;
            }
            frames->append(m_buffer.left(end));
            m_buffer.remove(0, end + m_delimiter.size());
        }
        return true;
    }
    if (m_kind == TestFrameKind::FixedLength) {
        while (m_buffer.size() >= m_fixedLength) {
            frames->append(m_buffer.left(m_fixedLength));
            m_buffer.remove(0, m_fixedLength);
        }
        return true;
    }
    return decodeLengthPrefix(frames, error);
}

bool TestFrameDecoder::decodeLengthPrefix(QVector<QByteArray> *frames, QString *error)
{
    while (m_buffer.size() >= m_lengthBytes) {
        quint64 length = 0; // 当前帧声明的业务数据长度。
        if (m_littleEndian) {
            for (int index = m_lengthBytes - 1; index >= 0; --index) {
                length = (length << 8) | static_cast<quint8>(m_buffer.at(index));
            }
        } else {
            for (int index = 0; index < m_lengthBytes; ++index) {
                length = (length << 8) | static_cast<quint8>(m_buffer.at(index));
            }
        }
        if (length > static_cast<quint64>(m_maxBytes)) {
            m_buffer.clear();
            if (error != nullptr) {
                *error = QStringLiteral("长度前缀声明的帧超过最大长度 %1 字节").arg(m_maxBytes);
            }
            return false;
        }
        const int frameSize = m_lengthBytes + static_cast<int>(length);
        if (m_buffer.size() < frameSize) {
            break;
        }
        frames->append(m_buffer.mid(m_lengthBytes, static_cast<int>(length)));
        m_buffer.remove(0, frameSize);
    }
    return true;
}

bool TestFrameDecoder::hasBuffered() const
{
    return !m_buffer.isEmpty();
}

QByteArray TestFrameDecoder::takeBuffered()
{
    const QByteArray result = m_buffer; // 保存缓存内容，再清空内部状态。
    m_buffer.clear();
    return result;
}

void TestFrameDecoder::reset()
{
    m_buffer.clear();
}

} // namespace edgeagent
