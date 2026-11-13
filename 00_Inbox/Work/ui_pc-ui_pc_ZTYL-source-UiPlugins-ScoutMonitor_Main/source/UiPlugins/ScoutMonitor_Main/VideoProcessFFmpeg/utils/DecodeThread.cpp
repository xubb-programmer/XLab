#include "DecodeThread.h"
#include <QDebug>

DecodeThread::DecodeThread(AVCodecContext *cdc,
                           SafeQueue<AVPacket> *pQueue,
                           SafeQueue<AVFrame *> *fQueue,
                           QObject *parent)
    :QThread(parent),
      m_codecCtx(cdc),
      m_packetQueue(pQueue),
      m_frameQueue(fQueue),
      m_stopFlag(false)
{

}

void DecodeThread::run()
{
    m_stopFlag = false;
    int got_picture = -1;
    int ret = -1;
    while(!m_stopFlag){
        AVPacket packet;
        av_init_packet(&packet);
        packet.data = NULL;
        packet.size = 0;

        if (!m_packetQueue->tryPop(packet, 20)) {
            continue;   // 超时或队列已 wakeAll，回头检查 m_stopFlag
        }
        if (m_stopFlag) {
            av_packet_unref(&packet);
            continue;
        }

        AVFrame *pFrame = av_frame_alloc();
        if(!pFrame){
           av_packet_unref(&packet);
           continue;
        }

        if (m_stopFlag) {
            av_frame_free(&pFrame);
            av_packet_unref(&packet);
            continue;
        }
        ret = avcodec_send_packet(m_codecCtx,&packet);
        if(ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
            av_frame_free(&pFrame);
            av_packet_unref(&packet);
            continue;
        }
        while(!m_stopFlag) {
            ret = avcodec_receive_frame(m_codecCtx,pFrame);
            if(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            }
            if (ret < 0) {
                break;
            }
            AVFrame *cloned = av_frame_clone(pFrame);
            if(cloned){
                if (m_frameQueue->size() >= g_MAX_QUEUE_SIZE) {
                    AVFrame *old = m_frameQueue->pop();
                    if (old) av_frame_free(&old);
                }
                m_frameQueue->push(cloned);
            }
        }
        av_frame_free(&pFrame);
        av_packet_unref(&packet);
    }
}

void DecodeThread::stop()
{
    m_stopFlag = true;
    if (m_packetQueue) m_packetQueue->wakeAll();

    if (!wait(5000)) {
        qWarning() << "[DecodeThread] wait timeout, force terminate";
        terminate();
        wait();
    }
}

void DecodeThread::setCodecContext(AVCodecContext *cdcCtx)
{
    m_codecCtx = cdcCtx;
}
