#include "RelayThread.h"
#include <QDebug>

RelayThread::RelayThread(AVFormatContext *fmt,
                         SafeQueue<AVPacket> *relayQueue,
                         QObject *parent)
    :QThread(parent),m_inFmtCtx(fmt),
      m_relayPacketQueue(relayQueue),
      m_stopFlag(false)
{
}

void RelayThread::run()
{
    m_stopFlag = false;
    if(!initOutput()) {
        emit errorOccured(Error_Relay_InitOutput_Fail);
        qDebug()<<"Faid to init relay Output";
        return;
    }

    while(!m_stopFlag){
        if(m_relayPacketQueue->empty()){
            msleep(2);
            continue;
        }
        if(!outFmtCtx) {
        // 无输出结构体
            break;
        }
        int ret = -1;
        AVPacket pkt = m_relayPacketQueue->pop();
        pkt.stream_index = 0;
        //av_packet_rescale_ts(&pkt, inStream->time_base, outStream->time_base);
        //pkt.pos = -1;

        //重新计算dts
         if(pkt.dts != AV_NOPTS_VALUE && last_dts != AV_NOPTS_VALUE  && pkt.dts <= last_dts ){
             pkt.dts = last_dts +1;
             if(pkt.pts < pkt.dts) {
                 pkt.pts = pkt.dts;
             }
         }
         last_dts = pkt.pts;//更新上一帧PTS

         m_writeWatchdog.restart(); // 写帧前重置看门狗，超时由中断回调中断写操作
         ret = av_interleaved_write_frame(outFmtCtx,&pkt);
         //frame_count++;
         if(ret < 0) {
             qDebug()<<"write frame failed"<<ret;
         }
        av_packet_unref(&pkt);
        //超过最大长度丢弃最旧数据；
        if(m_relayPacketQueue->size() > g_MAX_QUEUE_SIZE){
            AVPacket oldItem = m_relayPacketQueue->pop();
            av_packet_unref(&oldItem);
        }

    }
    closeOutput();
}

void RelayThread::stop()
{
    m_stopFlag =true;
    // wait 超时须大于 WRITE_TIMEOUT_MS，让 IO 看门狗先中断阻塞的网络写，
    // 尽量避免走到 terminate()
    if (!wait(5000)) {
        qWarning() << "[RelayThread] timeout, force terminate";
        terminate();
        wait();
    }
}

int RelayThread::interruptCallback(void *opaque)
{
    RelayThread *self = static_cast<RelayThread*>(opaque);
    if (!self) return 0;
    if (self->m_writeWatchdog.isValid() &&
        self->m_writeWatchdog.elapsed() > WRITE_TIMEOUT_MS) {
        return 1; // 单次写 IO 超时，中断，防止网络阻塞导致无限等待
    }
    return 0;
}

void RelayThread::setFormatContext(AVFormatContext *fmtCtx)
{
    m_inFmtCtx = fmtCtx;
}

void RelayThread::setRelayUrl(QString url)
{
    m_url = url;
}

void RelayThread::setInpurUrl(QString url)
{
    m_inputUrl = url;
}

bool RelayThread::initOutput()
{
    if (!m_inFmtCtx) {
        qWarning() << "[RelayThread] m_inFmtCtx is NULL";
        return false;
    }
    last_dts = 0;
    int ret = -1;
    ret = avformat_alloc_output_context2(&outFmtCtx,NULL,"mpegts",m_url.toStdString().c_str());
    if(!outFmtCtx) {
        qDebug()<<"Failed to alloc output context2"<<av_err2str_cpp(ret);
        return false;
    }

    // 安装输出 IO 中断回调（兼容 FFmpeg 3.2.12 及 ARM 版本的老 API）
    m_intCB.callback = &RelayThread::interruptCallback;
    m_intCB.opaque   = this;
    outFmtCtx->interrupt_callback = m_intCB;

    bool foundVideo = false;
    for(unsigned int i=0;i<m_inFmtCtx->nb_streams;i++) {
        if(m_inFmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO){
            inStream = m_inFmtCtx->streams[i];
            outStream = avformat_new_stream(outFmtCtx,NULL);
            if(!outStream) {
                qDebug()<<"Failed to create new stream.";
                avformat_free_context(outFmtCtx);
                outFmtCtx = nullptr;
                return false;
            }
            avcodec_parameters_copy(outStream->codecpar,inStream->codecpar);
            outStream->codecpar->codec_tag = 0;
            outStream->time_base = inStream->time_base;
            foundVideo = true;
            break;
        }
    }
    if (!foundVideo) {
        qWarning() << "[RelayThread] no video stream found in input context";
        avformat_free_context(outFmtCtx);
        outFmtCtx = nullptr;
        return false;
    }
    av_dump_format(outFmtCtx, 0, m_url.toStdString().c_str(), 1);

    if(!(outFmtCtx->oformat->flags & AVFMT_NOFILE)){
        m_writeWatchdog.restart(); // 连接/打开前启动看门狗，防止 avio_open2 阻塞
        if(avio_open2(&outFmtCtx->pb,m_url.toStdString().c_str(),AVIO_FLAG_WRITE,&m_intCB,NULL) < 0){
            qDebug()<<"Could not open output file";
            avformat_free_context(outFmtCtx);
            outFmtCtx = nullptr;
            return false;
        }
    }
    m_writeWatchdog.restart(); // 写文件头前重置看门狗
    ret = avformat_write_header(outFmtCtx,NULL);
    if(ret < 0) {
        qDebug()<<"Failed to write header"<<av_err2str_cpp(ret);
        if(!(outFmtCtx->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&outFmtCtx->pb);
        }
        avformat_free_context(outFmtCtx);
        outFmtCtx = nullptr;
        return false;
    }
    return true;
}

void RelayThread::closeOutput()
{
    if(!outFmtCtx) {
        return;
    }
    m_writeWatchdog.restart(); // 写文件尾前重置看门狗，防止 av_write_trailer 阻塞
    av_write_trailer(outFmtCtx);// 写文件尾
    if(!(outFmtCtx->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&outFmtCtx->pb);
    }
    avformat_free_context(outFmtCtx);
    outFmtCtx = NULL;
}
