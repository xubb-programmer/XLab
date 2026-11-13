#include "PullThread.h"
#include "FFmpegWorker.h"
#include <QDebug>

PullThread::PullThread(AVFormatContext *fmt,
                       SafeQueue<AVPacket>* queue,
                       SafeQueue<AVPacket>* recordQueue,
                       SafeQueue<AVPacket>* relayQueue,
                       QObject *parent)
    :QThread(parent),m_fmtCtx(fmt),m_packetQueue(queue),
      m_recordPacketQueue(recordQueue),
      m_relayPacketQueue(relayQueue),
      m_stopFlag(false),
      m_recordFlag(false),
      m_relaydFlag(false),
      videoStreamIndex(-1),
      m_waitKeyFrame(true)   // 初始化为 true：启动 / 重连后必须等到 IDR 帧
{

}

void PullThread::run()
{
    m_stopFlag = false;
    m_waitKeyFrame = true;   // 每次启动都重新等待关键帧
    int eagainCount = 0;
    AVPacket pkt;
    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size=0;

    bool firstFlag = true; // 有流后发信号让拉流提示消失
    FFmpegWorker *worker = qobject_cast<FFmpegWorker*>(parent());
    if (!worker) {
        // 理论上 PullThread 总以 FFmpegWorker 为 parent；若取不到则无法喂狗，
        // 看门狗会误判超时导致持续断流，这里显式告警便于定位，而非静默降级。
        qWarning() << "[PullThread] parent is not FFmpegWorker, watchdog feeding disabled!";
    } else {
        worker->feedWatchdog();
    }
    while(!m_stopFlag){
        if(!m_fmtCtx){
            break;
        }
        int ret = av_read_frame(m_fmtCtx,&pkt);
        if(ret < 0 ){
            qDebug()<<"av_read_frame<0"<<ret<<av_err2str_cpp(ret)<<AVERROR(EAGAIN);
             if(ret == AVERROR_EXIT){
                 emit errorOccured(Error_End_Of_File);
                 break;
             }
            if(ret == AVERROR(EAGAIN)){
                // 非阻塞模式下，当前无可用数据，后续重试可能成功
                //eagainCount++;
                //if(eagainCount <= MAX_EAGAIN_RETRY) { // 尝试连接5次，避免因网络波动导致的异常，若5次失败后，测认为视设备离线，重新连接
                    msleep(EAGAIN_SLEEP_MS);
                    continue;
                //}
                //emit errorOccured(Error_End_Of_File);
                //break;
            }
            //其他错误
            emit errorOccured(Error_End_Of_File);
            break;
        } else {
            // 读帧成功
            eagainCount =0;//重置EAGIN次数
            if (worker) {
                worker->feedWatchdog(); // 直接喂狗，避免信号槽跨线程延迟在 ARM 上误触发超时
            }
            if(pkt.stream_index == videoStreamIndex) {
#if 1
                // ── ★ 关键帧等待逻辑 ──────────────────────────
                if (m_waitKeyFrame) {
                    if (!(pkt.flags & AV_PKT_FLAG_KEY)) {
                        // 非关键帧，丢弃
                        av_packet_unref(&pkt);
                        av_init_packet(&pkt);
                        pkt.data = NULL;
                        pkt.size = 0;
                        continue;
                    }
                    // 收到第一个关键帧，解除等待
                    m_waitKeyFrame = false;
                    qDebug() << "[PullThread] got first key frame, start decoding";
                    emit signalChangeVideoState(Status_Playing,QString::fromLocal8Bit("播放中"));
                }
#endif
                // ★ 必须在把 pkt 交给解码队列之前，先为录像/上报各取一份引用。
                //   m_packetQueue->push(pkt) 是浅拷贝并转移 pkt.buf 所有权，一旦推入，
                //   DecodeThread 可能立即 pop 并 av_packet_unref 释放底层 buffer，
                //   此后再对 &pkt 调用 av_packet_ref 就是操作已释放内存，
                //   触发 avutil 写访问冲突(0xC0000005)崩溃。
                if(m_recordFlag) {
                    AVPacket recordPkt;
                    av_init_packet(&recordPkt);
                    if(av_packet_ref(&recordPkt,&pkt) == 0){
                        m_recordPacketQueue->push(recordPkt);  // 进入录像队列
                    }
                }
                if(m_relaydFlag) {
                    AVPacket relayPkt;
                    av_init_packet(&relayPkt);
                    if(av_packet_ref(&relayPkt,&pkt) == 0){
                        m_relayPacketQueue->push(relayPkt);  // 进入上报队列
                    }
                }
                //超过最大长度丢弃最旧数据；
                if(m_packetQueue->size() > g_MAX_QUEUE_SIZE){
                    AVPacket oldItem = m_packetQueue->pop();
                    av_packet_unref(&oldItem);
                }
                m_packetQueue->push(pkt); // 最后把 pkt 所有权转移给解码队列
            } else {
                av_packet_unref(&pkt);
            }
            // 重置pkt，准备下次av_read_frame
            av_init_packet(&pkt);
            pkt.data=NULL;
            pkt.size=0;
        }
    }
    //确保循环退出时不持有悬空pkt
    av_packet_unref(&pkt);
}

void PullThread::stop()
{
    m_stopFlag = true;
    m_recordFlag = false;
    m_relaydFlag = false;
    if(!wait(5000)){
        qWarning() << "[PullThread] wait timeout, force terminate";
        terminate();
        wait();
    }
}

void PullThread::setFormatContext(AVFormatContext *fmtCtx,int videoindex)
{
    m_fmtCtx = fmtCtx;
    videoStreamIndex = videoindex;
}

void PullThread::setRecordFlag(bool flag)
{
    m_recordFlag = flag;
}

void PullThread::setRelayFlag(bool flag)
{
    m_relaydFlag = flag;
}

void PullThread::setEagainPara(int sleepTime)
{
    EAGAIN_SLEEP_MS = sleepTime;
}
