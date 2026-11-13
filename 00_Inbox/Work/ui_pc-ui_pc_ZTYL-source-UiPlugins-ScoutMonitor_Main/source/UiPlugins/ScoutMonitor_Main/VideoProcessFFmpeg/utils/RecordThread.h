/*
* File：RecordThread.h
* Author：huangjin
* Created：2025-08-08
* Last Modified：2025-08-12
* Description：该文件用于实现录像线程
*/
#ifndef RECORDTHREAD_H
#define RECORDTHREAD_H
#include <QThread>
#include <QElapsedTimer>
#include "utils/FFmpegWorkerCommon.h"

class RecordThread:public QThread
{
    Q_OBJECT
public:
    explicit RecordThread(AVFormatContext* fmt,
                        SafeQueue<AVPacket>*recordQueue,
                        QObject *parent=NULL);

    void run();

    void stop();


    void setFormatContext(AVFormatContext *fmtCtx);
    /**
    * @brief setRecordUrl 函数
    * 录像操作
    * @param
    * QString url：录像文件绝对路径
    * @return 空。
    * */
    void setRecordUrl(QString url);

    /**
    * @brief setInpurUrl 函数
    * 设置输入的url,区分协议
    * @param
    * QString url：拉流的地址
    * @return 空。
    * */
    void setInpurUrl(QString url);

private:

    /**
    * @brief initOutput 函数
    * 初始化录像文件操作
    * @param
    * fileIndex：分段的序号 30min
    * @return bool true:成功，false:失败。
    * */
    bool initOutput(int fileIndex);

    /**
    * @brief closeOutput 函数
    * 关闭录像文件操作
    * @param
    * @return。
    * */
    void closeOutput();

    /**
    * @brief interruptCallback 函数
    * FFmpeg 输出 IO 中断回调：单次写操作超过 WRITE_TIMEOUT_MS 即返回 1，
    * 让 av_interleaved_write_frame / avio_open2 / av_write_trailer 不会在
    * 磁盘满、U盘拔出、网络盘卡顿时无限阻塞，从而避免 stop() 超时触发
    * terminate() 强杀正在写文件的线程导致的堆损坏/崩溃。
    * 注意：仅依赖看门狗超时，不读 m_stopFlag，保证正常停止时文件尾能写完整。
    * @param opaque：this 指针
    * @return 1:中断当前 IO  0:继续
    * */
    static int interruptCallback(void *opaque);

signals:
    void errorOccured(ErrorType);

private:
    SafeQueue<AVPacket> *m_recordPacketQueue=NULL; // 录像队列
    AVFormatContext *m_inFmtCtx = NULL; // 输入视频格式的上下文
    AVFormatContext *outFmtCtx = NULL; //输出文件的格式上下文
    AVStream *inStream = NULL;//输入流
    AVStream *outStream = NULL;//输出流
    int videoStreamIndex = 0;//视频流序号
    volatile bool m_stopFlag = false;//停止标志位（跨线程读写）
    QString filename = "";//录像文件名称
    int frame_count = 0;//帧计数
    QString m_inputUrl = ""; //输入的视频源
    int64_t last_dts = 0; //记录上一帧pts,用于计算时间间隔
    double frame_interval = 0 ;//帧间隔(s)

    QElapsedTimer m_writeWatchdog;          // 写 IO 看门狗计时器
    AVIOInterruptCB m_intCB = {};           // 输出 IO 中断回调结构
    static const int WRITE_TIMEOUT_MS = 3000; // 单次写 IO 超时(ms)，须小于 stop() 的 wait 超时



};

#endif // RECORDTHREAD_H
