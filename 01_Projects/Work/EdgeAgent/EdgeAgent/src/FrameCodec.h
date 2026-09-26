/**
 * @file FrameCodec.h
 * @brief TCP 字节流分帧器接口。
 * @author xubb
 */

#pragma once

#include "Config.h"

#include <QByteArray>
#include <QVector>

namespace EdgeAgent {

/**
 * @brief 将 TCP 字节流拆分为完整业务帧。
 *
 * TCP 本身没有消息边界，因此该类负责保存未完成数据，并按照配置提取完整帧。
 */
class FrameDecoder {
public:
    /**
     * @brief 创建分帧器。
     * @param config 分帧配置。
     */
    explicit FrameDecoder(const FrameConfig &config = {});

    /**
     * @brief 输入新收到的字节并提取完整帧。
     * @param data 新收到的 TCP 字节。
     * @param frames 输出的完整帧列表。
     * @param error 错误信息输出对象。
     * @return 解析成功返回 true，否则返回 false。
     */
    bool feed(const QByteArray &data, QVector<QByteArray> *frames, QString *error);

    /**
     * @brief 取出当前缓存中的数据。
     * @return 缓存数据，取出后缓存会被清空。
     */
    QByteArray takeBuffered();

    /**
     * @brief 判断是否存在未分帧数据。
     * @return 存在缓存返回 true，否则返回 false。
     */
    bool hasBuffered() const;

    /** @brief 清空当前分帧缓存。 */
    void reset();

private:
    /**
     * @brief 解析长度前缀类型的数据帧。
     * @param frames 输出的完整帧列表。
     * @param error 错误信息输出对象。
     * @return 解析成功返回 true，否则返回 false。
     */
    bool decodeLengthPrefix(QVector<QByteArray> *frames, QString *error);

    FrameConfig m_config; ///< 当前分帧配置。
    QByteArray m_buffer; ///< 尚未组成完整帧的字节缓存。
};

} // 命名空间 EdgeAgent
