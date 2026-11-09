/**
 * @file Channel.h
 * @brief UDP、TCP 和串口通道的公共抽象接口。
 * @author xubb
 */

#pragma once

#include "Config.h"
#include "FrameEvent.h"
#include "TransportCommand.h"

#include <QObject>

namespace EdgeAgent {

/**
 * @brief 所有传输通道的公共基类。
 *
 * 基类统一保存配置，并通过 Qt 信号向 EdgeAgent 输出 UDP、TCP、串口、CAN 数据、状态和故障。
 */
class Channel : public QObject {
    Q_OBJECT

public:
    /**
     * @brief 创建通道对象。
     * @param config 通道配置。
     * @param parent Qt 父对象。
     */
    explicit Channel(const ChannelConfig &config, QObject *parent = nullptr)
        : QObject(parent)
        , m_config(config)
    {
    }
    /** @brief 销毁通道对象。 */
    ~Channel() override = default;

    /** @brief 启动通道。 */
    virtual void start() = 0;

    /** @brief 停止通道并释放通信资源。 */
    virtual void stop() = 0;

    /** 提交传输帧；成功仅表示本地发送接口接受数据。 */
    virtual bool send(const TransportCommand &command) = 0;

    /**
     * @brief 获取通道配置。
     * @return 当前通道配置的只读引用。
     */
    const ChannelConfig &config() const { return m_config; }

signals:
    /**
     * @brief 通道收到完整数据帧时发出。
     * @param event 已经完成传输层分帧的数据事件。
     */
    void frameReceived(const FrameEvent &event);

    /**
     * @brief 通道状态变化时发出。
     * @param channelId 发生变化的通道标识。
     * @param state 新状态，例如 ready、stopped 或 error。
     */
    void stateChanged(const QString &channelId, const QString &state);

    /**
     * @brief 通道发生故障时发出。
     * @param channelId 发生故障的通道标识。
     * @param code 稳定的机器可读错误码。
     * @param message 面向开发和运维人员的中文错误说明。
     */
    void fault(const QString &channelId, const QString &code, const QString &message);

protected:
    /**
     * @brief 发布当前通道状态。
     * @param state 要发布的状态名称。
     */
    void setState(const QString &state) { emit stateChanged(m_config.channelId, state); }

    /**
     * @brief 发布当前通道故障。
     * @param code 稳定的机器可读错误码。
     * @param message 面向开发和运维人员的中文错误说明。
     */
    void reportFault(const QString &code, const QString &message) { emit fault(m_config.channelId, code, message); }

    ChannelConfig m_config; ///< 当前通道配置。
};

} // 命名空间 EdgeAgent
