/**
 * @file TestFrameDecoder.h
 * @brief 测试客户端使用的字节流分帧器。
 * @author xubb
 */

#pragma once

#include <QByteArray>
#include <QVector>

namespace edgeagent {

/** @brief 测试端支持的字节流分帧类型。 */
enum class TestFrameKind {
    Delimiter, ///< 使用固定分隔符分帧。
    FixedLength, ///< 使用固定长度分帧。
    LengthPrefix, ///< 使用长度前缀分帧。
    IdleTimeout ///< 空闲超时后输出缓存数据。
};

/**
 * @brief 为 TCP 或串口接收缓冲区提取完整业务帧。
 *
 * UDP 每个数据报天然具有边界，不经过此类；TCP 和串口按照连续字节流处理。
 */
class TestFrameDecoder final {
public:
    /** @brief 使用默认配置创建分帧器。 */
    TestFrameDecoder();

    /**
     * @brief 设置分帧参数。
     * @param kind 分帧类型。
     * @param delimiter 分隔符内容。
     * @param fixedLength 固定帧长度。
     * @param lengthBytes 长度前缀字节数。
     * @param littleEndian 是否使用小端长度。
     * @param maxBytes 单帧最大长度。
     */
    void configure(TestFrameKind kind,
                   const QByteArray &delimiter,
                   int fixedLength,
                   int lengthBytes,
                   bool littleEndian,
                   int maxBytes);

    /**
     * @brief 输入新字节并提取完整帧。
     * @param data 新收到的数据。
     * @param frames 输出的完整帧列表。
     * @param error 输出的错误说明。
     * @return 分帧成功返回 true。
     */
    bool feed(const QByteArray &data, QVector<QByteArray> *frames, QString *error);

    /** @brief 判断当前是否存在未完成帧。 */
    bool hasBuffered() const;

    /** @brief 取出并清空当前缓存。 */
    QByteArray takeBuffered();

    /** @brief 清空当前缓存。 */
    void reset();

private:
    /** @brief 解析长度前缀模式。 */
    bool decodeLengthPrefix(QVector<QByteArray> *frames, QString *error);

    TestFrameKind m_kind = TestFrameKind::Delimiter; ///< 当前分帧类型。
    QByteArray m_delimiter = QByteArrayLiteral("\n"); ///< 当前分隔符。
    int m_fixedLength = 1; ///< 当前固定帧长度。
    int m_lengthBytes = 2; ///< 当前长度前缀字节数。
    bool m_littleEndian = false; ///< 当前长度前缀是否为小端。
    int m_maxBytes = 65536; ///< 当前允许的最大帧长度。
    QByteArray m_buffer; ///< 尚未组成完整帧的字节缓存。
};

} // namespace edgeagent
