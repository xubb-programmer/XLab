#pragma once
#ifndef SAFEQUEUE_H
#define SAFEQUEUE_H

#include <queue>
#include <QMutex>
#include <QWaitCondition>
#include "FFmpegWorkerCommon.h"

typedef struct AVPacket AVPacket;

// ─────────────────────────────────────────────────────────────
// SafeQueue<T> —— 线程安全队列
//
// 注意点：
// 1. pop() 原实现无超时保护：若生产者已停止，消费线程永久阻塞在 cond.wait
//    → DecodeThread / RenderThread 的 stop() 调用 wait() 永远无法返回 → 死锁
//
// 2. tryPop(T&, int ms)：带超时的非阻塞弹出，线程停止时可安全退出循环
//
// 3. wakeAll()：stop 流程中唤醒所有阻塞在 pop 的消费者
//
// 4. 模板类名补全（原代码 class SafeQueue 缺少 <T>）
// ─────────────────────────────────────────────────────────────
template <typename T>
class SafeQueue
{
public:
    SafeQueue() : m_abort(false) {}

    // 入队，唤醒一个等待者
    void push(const T &item)
    {
        QMutexLocker locker(&m_mutex);
        m_queue.push(item);
        m_cond.wakeOne();
    }

    // 阻塞弹出（永久等待）——仅在确认不会死锁时使用
    T pop()
    {
        QMutexLocker locker(&m_mutex);
        while (m_queue.empty() && !m_abort)
            m_cond.wait(&m_mutex);
        if (m_abort && m_queue.empty()) return T();   // 队列被终止，返回默认值
        T item = m_queue.front();
        m_queue.pop();
        return item;
    }

    // 带超时弹出：推荐在工作线程的 run() 循环中使用
    //   返回 true 表示成功取到元素；false 表示超时或队列已终止
    bool tryPop(T &out, unsigned long timeoutMs = 20)
    {
        QMutexLocker locker(&m_mutex);
        if (m_queue.empty()) {
            if (!m_cond.wait(&m_mutex, timeoutMs)) return false;  // 超时
        }
        if (m_abort && m_queue.empty()) return false;             // 被 wakeAll 唤醒但无数据
        if (m_queue.empty()) return false;
        out = m_queue.front();
        m_queue.pop();
        return true;
    }

    bool empty() const
    {
        QMutexLocker locker(&m_mutex);
        return m_queue.empty();
    }

    int size() const
    {
        QMutexLocker locker(&m_mutex);
        return static_cast<int>(m_queue.size());
    }

    // 清空队列（不释放 FFmpeg 资源，由调用方负责）
    void clear()
    {
        QMutexLocker locker(&m_mutex);
        std::queue<T> empty;
        std::swap(m_queue, empty);
    }

    // 终止：唤醒所有阻塞的消费者，使其从 pop/tryPop 返回
    //   在 stopPlay 清空队列前调用，避免消费线程永久阻塞
    void wakeAll()
    {
        QMutexLocker locker(&m_mutex);
        m_abort = true;
        m_cond.wakeAll();
    }

    // 重置终止标志（下次 startPlay 前调用）
    void resetAbort()
    {
        QMutexLocker locker(&m_mutex);
        m_abort = false;
    }

private:
    std::queue<T>  m_queue;
    mutable QMutex m_mutex;
    QWaitCondition m_cond;
    bool           m_abort;
};

#endif // SAFEQUEUE_H
