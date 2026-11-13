/*
* File：RecordThread.h
* Author：huangjin
* Created：2025-08-08
* Last Modified：2025-08-12
* Description：该文件用于实现上报线程
*/
#ifndef RELAYTHREAD_H
#define RELAYTHREAD_H
#include <QThread>
#include <QElapsedTimer>
#include "utils/FFmpegWorkerCommon.h"

class RelayThread:public QThread
{
      Q_OBJECT
public:
    explicit RelayThread(AVFormatContext* fmt,
                        SafeQueue<AVPacket>*relayQueue,
                        QObject *parent=NULL);

    void run();

    void stop();

    void setFormatContext(AVFormatContext *fmtCtx);
    /**
    * @brief setRelayUrl 函数
    * 上报接口操作
    * @param
    * QString url：上报的地址
    * @return 空。
    * */
    void setRelayUrl(QString url);

    /**
    * @brief setInpurUrl 函数
    * 设置输入的url,区分协议
    * @param
    * QString url：拉流的地址
    * @return 空。
    * */
    void setInpurUrl(QString url);


signals:
    void errorOccured(ErrorType);

private:
    /**
    * @brief initOutput 函数
    * 初始化转发视频操作
    * @param
    * @return bool true:成功，false:失败。
    * */
    bool initOutput();
    /**
    * @brief closeOutput 函数
    * 关闭上报文件操作
    * @param
    * @return。
    * */
    void closeOutput();

    /**
    * @brief interruptCallback 函数
    * FFmpeg 输出 IO 中断回调：单次写操作超过 WRITE_TIMEOUT_MS 即返回 1，
    * 让上报（RTMP/网络）写操作不会无限阻塞，避免 stop() 超时触发
    * terminate() 强杀正在写流的线程导致崩溃。
    * @param opaque：this 指针
    * @return 1:中断当前 IO  0:继续
    * */
    static int interruptCallback(void *opaque);

    SafeQueue<AVPacket> *m_relayPacketQueue=NULL;// 上报队列
    AVFormatContext *m_inFmtCtx = NULL;// 输入视频格式的上下文
    AVFormatContext *outFmtCtx = NULL;//输出文件的格式上下文
    AVStream *inStream = NULL;//输入流
    AVStream *outStream = NULL;//输出流
    int videoStreamIndex = 0;//视频流序号
    volatile bool m_stopFlag = false;//停止标志位（跨线程读写）
    QString m_url = "";//上传的地址
    int frame_count = 0;//帧计数
    QString m_inputUrl ="" ;
    int64_t last_dts = 0;

    QElapsedTimer m_writeWatchdog;            // 写 IO 看门狗计时器
    AVIOInterruptCB m_intCB = {};             // 输出 IO 中断回调结构
    static const int WRITE_TIMEOUT_MS = 5000; // 单次写 IO 超时(ms)，网络上报放宽到5s

};

#endif // RELAYTHREAD_H
