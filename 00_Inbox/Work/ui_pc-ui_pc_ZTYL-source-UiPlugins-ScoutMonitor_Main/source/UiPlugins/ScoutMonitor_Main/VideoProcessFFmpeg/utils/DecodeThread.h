/*
* File：DecodeThread.h
* Author：huangjin
* Created：2025-08-08
* Last Modified：2025-08-12
* Description：该文件用于实现解码线程
*/
#ifndef DECODETHREAD_H
#define DECODETHREAD_H
#include <QThread>
#include "utils/FFmpegWorkerCommon.h"

class DecodeThread:public QThread
{
    Q_OBJECT
public:
    explicit DecodeThread(AVCodecContext* cdc,SafeQueue<AVPacket>*pQueue,
                 SafeQueue<AVFrame*>*fQueue,
                 QObject *parent=NULL);

    /**
    * @brief run 函数
    * 解码线程
    * @param
    * @return 空。
    * */
    void run();
    /**
    * @brief stop 函数
    * 停止解码
    * @param
    * @return 空。
    * */
    void stop();

    void setCodecContext(AVCodecContext *cdcCtx);

private:

    AVCodecContext *m_codecCtx = NULL;  // 视频编解码上下文

    SafeQueue<AVPacket>*m_packetQueue;// 未解码包队列

    SafeQueue<AVFrame*>*m_frameQueue; //解码帧队列

    bool m_stopFlag = false;// 停止标志位

signals:
    void errorOccured(ErrorType);
};

#endif // DECODETHREAD_H
