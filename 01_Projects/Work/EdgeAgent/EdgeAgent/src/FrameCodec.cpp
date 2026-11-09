/**
 * @file FrameCodec.cpp
 * @brief TCP 字节流分帧器实现。
 * @author xubb
 */

#include "FrameCodec.h"

#include <QtGlobal>

namespace EdgeAgent {

FrameDecoder::FrameDecoder(const FrameConfig &config)
    : m_config(config)
{
}

bool FrameDecoder::feed(const QByteArray &data, QVector<QByteArray> *frames, QString *error)
{
    // 调用者必须提供输出容器，否则无法返回解析出的完整帧。
    if (frames == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("分帧输出对象为空");
        }
        return false;
    }
    auto tooLarge = [this, error]() {
        m_buffer.clear();
        if (error) *error = QStringLiteral("分帧长度超过 max_bytes=%1").arg(m_config.maxBytes);
        return false;
    };
    // TCP 是连续字节流，先把本次数据追加到连接自己的缓存中。
    if (!data.isEmpty()) {
        m_buffer.append(data);
    }

    // 空闲超时模式暂不立即输出，等待定时器通知末尾数据。
    if (m_config.kind == FrameKind::IdleTimeout) {
        return m_buffer.size() <= m_config.maxBytes || tooLarge();
    }

    // 分隔符模式可以一次从缓存中提取多帧。
    if (m_config.kind == FrameKind::Delimiter) {
        if (m_config.delimiter.isEmpty()) return tooLarge();
        while (true) {
            const int end = m_buffer.indexOf(m_config.delimiter); // 分隔符结束位置。
            if (end < 0) {
                break;
            }
            const QByteArray frame = m_buffer.left(end); // 不包含分隔符的业务数据。
            if (frame.size() > m_config.maxBytes) {
                m_buffer.clear();
                if (error != nullptr) {
                    *error = QStringLiteral("分隔符帧长度超过 max_bytes=%1").arg(m_config.maxBytes);
                }
                return false;
            }
            frames->append(frame);
            m_buffer.remove(0, end + m_config.delimiter.size());
        }
        return m_buffer.size() <= m_config.maxBytes + m_config.delimiter.size() - 1 || tooLarge();
    }

    // 固定长度模式只在缓存足够时输出完整帧。
    if (m_config.kind == FrameKind::FixedLength) {
        if (m_config.fixedLength < 1 || m_config.fixedLength > m_config.maxBytes) return tooLarge();
        while (m_buffer.size() >= m_config.fixedLength) {
            frames->append(m_buffer.left(m_config.fixedLength));
            m_buffer.remove(0, m_config.fixedLength);
        }
        return true;
    }

    return decodeLengthPrefix(frames, error);
}

bool FrameDecoder::decodeLengthPrefix(QVector<QByteArray> *frames, QString *error)
{
    // 长度前缀模式先读取长度，再等待完整业务数据到达。
    while (m_buffer.size() >= m_config.lengthBytes) {
        quint64 length = 0; // 当前帧声明的业务数据长度。
        if (m_config.byteOrder == ByteOrder::BigEndian) {
            for (int i = 0; i < m_config.lengthBytes; ++i) { // 按大端顺序拼接长度值。
                length = (length << 8) | static_cast<quint8>(m_buffer.at(i));
            }
        } else {
            for (int i = m_config.lengthBytes - 1; i >= 0; --i) { // 按小端顺序拼接长度值。
                length = (length << 8) | static_cast<quint8>(m_buffer.at(i));
            }
        }
        if (length > static_cast<quint64>(m_config.maxBytes)) {
            m_buffer.clear();
            if (error != nullptr) {
                *error = QStringLiteral("长度前缀帧超过 max_bytes=%1").arg(m_config.maxBytes);
            }
            return false;
        }
        const int frameSize = m_config.lengthBytes + static_cast<int>(length); // 前缀和业务数据总长度。
        if (m_buffer.size() < frameSize) {
            break;
        }
        frames->append(m_buffer.mid(m_config.lengthBytes, static_cast<int>(length)));
        m_buffer.remove(0, frameSize);
    }
    return true;
}

QByteArray FrameDecoder::takeBuffered()
{
    QByteArray result = m_buffer; // 保存当前缓存，随后清空内部状态。
    m_buffer.clear();
    return result;
}

bool FrameDecoder::hasBuffered() const
{
    return !m_buffer.isEmpty();
}

void FrameDecoder::reset()
{
    m_buffer.clear();
}

} // 命名空间 EdgeAgent
