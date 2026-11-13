/*
* File：FFmpegWorker.h
* Author：huangjin
* Created：2025-08-08
* Last Modified：2025-08-12
* Description：该文件用于ffmpeg控制，管理子线程和状态
*/
#ifndef FFMPEGWORKER_H
#define FFMPEGWORKER_H
#pragma once
#include <QObject>
#include <QImage>
#include <QThread>
#include <QString>
#include <QWidget>
#include <QAtomicInt>
#include "utils/PullThread.h"
#include "utils/DecodeThread.h"
#include "utils/RenderThread.h"
#include "utils/SafeQueue.h"
#include "utils/RecordThread.h"
#include "utils/RelayThread.h"

class FFmpegWorker:public QObject
{
    Q_OBJECT
public:
    FFmpegWorker();

    ~FFmpegWorker();

    //QThread thread;

    static const int INTERRUPT_TIMEOUT_MS = 5000; // 5s无数据则中断
    volatile bool m_stopFlag = false; // 停止标志位
    volatile bool m_watchdogEnabled = false; // 拉流阶段看门狗
    volatile int64_t m_lastFeedUs = 0;       // 上次喂狗时间(us)，PullThread 直接写入，中断回调读取
    volatile int64_t m_initDeadlineUs = 0;   // open/probe 阶段的绝对截止时间(us)
    QAtomicInt m_interruptRequested;         // UI 可直接置位，用于打断阻塞中的 FFmpeg IO
    int m_watchdogTimerOut = 6000;

    /** PullThread 读帧成功后直接调用，避免信号槽跨线程延迟导致 ARM 误触发超时 */
    void feedWatchdog();

    /**
     * 可从 UI 线程直接调用。不能使用排队槽，否则 worker 卡在 FFmpeg IO 时
     * 取消请求本身也会排在事件队列中，无法解除阻塞。
     */
    void requestInterrupt();

public slots:

    /**
    * @brief startPlay 函数
    * 启动播放
    * @param
    * const QString &url:接收UI线程传递的拉流地址
    * const QVariant &winIdVar:接收UI线程传递的窗口句柄
    * @return 空。
    * */
    void startPlay(const QString &url,const QVariant &winIdVar);

    /**
    * @brief switchPlay 函数
    * 在 worker 线程上原子执行 stop + start，避免 UI 侧分开发送
    * stopPlay/startPlay 时序错乱或残留 stop 杀死新流。
    * */
    void switchPlay(const QString &url,const QVariant &winIdVar);

    /**
    * @brief stopPlay 函数
    * 停止播放
    * @param
    * @return 空。
    * */
    void stopPlay();

    /**
    * @brief snapShot 函数
    * 截图操作
    * @param
    * const QString &filename:接收UI线程传递的截图绝对路径
    * @return 空。
    * */
    void snapShot(const QString &filename);

    /**
    * @brief startRecordVideo 函数
    * 开启录像操作
    * @param
    * const QString &filename:接收UI线程传递的录像绝对路径
    * @return 空。
    * */
    void startRecordVideo(const QString &filename);

    /**
    * @brief stopRecordVideo 函数
    * 停止录像操作
    * @param
    * @return 空。
    * */
    void stopRecordVideo();

    /**
    * @brief startRelayVideo 函数
    * 开启视频上报操作
    * @param
    * const QString &filename:接收UI线程传递的录像绝对路径
    * @return 空。
    * */
    void startRelayVideo(const QString &url);

    /**
    * @brief stopRelayVideo 函数
    * 停止视频上报操作
    * @param
    * @return 空。
    * */
    void stopRelayVideo();

private slots:
    /**
    * @brief sltFeedWatchDog 函数
    * 重置超时定时器，由拉流线程触发
    * @param
    * @return 空。
    * */
    void sltFeedWatchDog();

    // 将子线程的 errorOccured 转换为状态变化后再向上转发
    void sltSubThreadError(ErrorType err);

    void sltChangeVideoState(VideoStatus status, const QString &extra);

    void sltLowDiskSpace();

signals:
    void errorOccured(ErrorType);
    void signalHideTip();

    // status  : 当前状态枚举
    // message : 对应的中文描述
    // 触发时机：
    //   Status_Connecting   → startPlay 开始执行时
    //   Status_Playing      → initFFmpeg 成功、线程全部启动后
    //   Status_Reconnecting → 收到断流错误、准备重连时
    //   Status_Failed       → initFFmpeg 失败且不再重连时
    //   Status_Stopped      → stopPlay 主动调用完成后
    void signalStatusChanged(VideoStatus status, const QString &message);
    void signalShowSnapShot(QString imgPath);


private:

    bool initFFmpeg(QString url);

    void releaseFFmpeg();
    /**
    * @brief cheakDir 函数
    * 检查路径是否存在
    * @param
    * QString path：检查的路径
    * @return 空。
    * */
    bool cheakDir(QString path);

    void drainPacketQueue(SafeQueue<AVPacket>*q);

    void drainFrameQueue(SafeQueue<AVFrame*>*q);

    // 发射状态信号的内部统一入口，同时记录当前状态
    void emitStatus(VideoStatus status, const QString &extra = QString());

    /** 确保 Pull/Decode/Render 子线程已退出后再 start()，避免 QThread::start 对运行中线程无效 */
    void ensureWorkerThreadsStopped();

    AVFormatContext *m_fmtCtx = NULL;  // 视频格式上下文结构体

    AVCodecContext *m_codecCtx = NULL;// 视频编解码上下文结构体

    void* m_winId;  // 显示窗口句柄

    PullThread* m_pullThread= NULL;     // 拉流线程（生产者packet包）

    DecodeThread* m_decodeThread= NULL;  // 解码线程（中间层：消费packet包，生产frame）

    RenderThread* m_renderThread= NULL;  // 显示线程（消费者frame）

    RecordThread* m_recordThread= NULL;  // 录像线程（消费者packet包）

    RelayThread* m_relayThread= NULL;   // 上报线程（消费者packet包）

    SafeQueue<AVPacket>* m_packetQueue;   // 未解码包队列

    SafeQueue<AVFrame*>* m_frameQueue;    // 解码队列

    SafeQueue<AVPacket>* m_recordQueue;   // 录像队列

    SafeQueue<AVPacket>* m_relayQueue;    //上报队列

    int m_streamIndex  = -1;

    bool initFlag = false;   // 视频初始化标志位；

    VideoStatus  m_currentStatus;   // 当前状态，防止重复发射相同状态

    int m_eagainSleepTime=10;// ms

};

#endif // FFMPEGWORKER_H
