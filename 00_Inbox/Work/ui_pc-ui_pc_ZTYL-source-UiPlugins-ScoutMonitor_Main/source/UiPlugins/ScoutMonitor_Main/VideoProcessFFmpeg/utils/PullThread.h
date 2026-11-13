/*
* File：PullThread.h
* Author：huangjin
* Created：2025-08-08
* Last Modified：2025-08-12
* Description：该文件用于实现拉流线程
*/
#ifndef PULLTHREAD_H
#define PULLTHREAD_H
#include <QThread>
#include "utils/FFmpegWorkerCommon.h"

class PullThread:public QThread
{
    Q_OBJECT
public:
    explicit PullThread(AVFormatContext* fmt,
                        SafeQueue<AVPacket>*queue,
                        SafeQueue<AVPacket>*recordQueue,
                        SafeQueue<AVPacket>*relayQueue,
                        QObject *parent=NULL);

    /**
    * @brief run 函数
    * 拉流线程
    * @param
    * @return 空。
    * */
    void run();

    /**
    * @brief stop 函数
    * 停止拉流
    * @param
    * @return 空。
    * */
    void stop();

    void setFormatContext(AVFormatContext *fmtCtx,int videoindex);

    /**
    * @brief setRecordFlag 函数
    * 设置视频录像标志位
    * @param
    * bool flag ：true:将packet塞入录像队列 false：停止packet塞入录像队列
    * @return 空。
    * */
    void setRecordFlag(bool flag);

    /**
    * @brief setRelayFlag 函数
    * 设置视频上报标志位
    * @param
    * bool flag ：true:将packet塞入上报队列 false：停止packet塞入上报队列
    * @return 空。
    * */
    void setRelayFlag(bool flag);

    void setEagainPara(int sleepTime);
    int EAGAIN_SLEEP_MS = 10; // 每次重试等待时间 10ms

    //static const int MAX_EAGAIN_RETRY=5;   // 最大重试次数
    //static const int EAGAIN_SLEEP_MS = 10; // 每次重试等待时间 10ms

signals:
    void errorOccured(ErrorType);
    void signalHideTip();
    void signalFeedWatchdog();
    void signalChangeVideoState(VideoStatus status, const QString &extra);

private:

    AVFormatContext *m_fmtCtx = NULL; // 视频格式上下文结构体
    int videoStreamIndex = -1;  // 视频流序号
    SafeQueue<AVPacket> *m_packetQueue;// 未解码包队列
    SafeQueue<AVPacket> *m_recordPacketQueue;// 录像队列
    SafeQueue<AVPacket> *m_relayPacketQueue;//上报队列
    bool m_stopFlag = false;   // 停止标志位
    bool m_recordFlag = false; // 录像标志位
    bool m_relaydFlag = false; // 上报标志位
    bool m_waitKeyFrame = true;
};

#endif // PULLTHREAD_H
