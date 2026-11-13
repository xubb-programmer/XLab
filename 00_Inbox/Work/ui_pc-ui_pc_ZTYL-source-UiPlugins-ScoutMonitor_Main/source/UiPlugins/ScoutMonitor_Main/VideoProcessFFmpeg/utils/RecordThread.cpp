#include "RecordThread.h"
#include "QDebug"
#include <QDateTime>
#include <QApplication>
#include <QFileInfo>

extern int g_SegmentTime;
RecordThread::RecordThread(AVFormatContext *fmt,
                           SafeQueue<AVPacket> *recordQueue,
                           QObject *parent)
    :QThread(parent),m_inFmtCtx(fmt),
      m_recordPacketQueue(recordQueue),
      m_stopFlag(false)
{

}

void RecordThread::run()
{
    m_stopFlag = false;
    int fileIndex = 0;
    qint64 segmentStartTime  = QDateTime::currentMSecsSinceEpoch();
    if(!initOutput(fileIndex)){
        qDebug()<<" Failed to init output file.";
        return;
    }
    if(g_SegmentTime==0){
        g_SegmentTime=30;  // 默认30min
    }
    while(!m_stopFlag){
        if(m_recordPacketQueue->empty()){
            msleep(2);
            continue;
        }
        //判断是否需要进行分段（30min）
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        if(now-segmentStartTime >= g_SegmentTime*60*1000){
            closeOutput();
            fileIndex++;
            segmentStartTime = now;
            if(!initOutput(fileIndex)){
                qDebug()<<" Failed to init output file. fileIndex:"<<fileIndex;
                emit errorOccured(Error_Relay_InitOutput_Fail);
                return;
            }
        }
        if(!outFmtCtx) {
            // 无输出结构体 ，直接返回
            break;
        }
        AVPacket pkt = m_recordPacketQueue->pop();
        pkt.stream_index = 0;  // 只保存视频
        av_packet_rescale_ts(&pkt, inStream->time_base, outStream->time_base);
        pkt.pos = -1;
        int ret = -1;
#if 0
        // 按照帧间隔控制写入速度
        if(last_pts !=0 ){
            //计算当前帧与上一帧的实际间隔s
            double actual_interval = (double)(pkt.pts -last_pts) *av_q2d(inStream->time_base);
            //若实际间隔小于设定帧间隔，补延迟
            if(actual_interval < frame_interval - 0.001){
                QThread::usleep((frame_interval - actual_interval) * 1000000);
            }
        }
        last_pts = pkt.pts;//更新上一帧PTS
#endif
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
        frame_count++;
        if(ret < 0) {
            qDebug()<<"write frame failed"<<ret;
        }
        av_packet_unref(&pkt);
        //超过最大长度丢弃最旧数据；
        if(m_recordPacketQueue->size() > g_MAX_QUEUE_SIZE){
            AVPacket oldItem = m_recordPacketQueue->pop();
            av_packet_unref(&oldItem);
        }
    }
    closeOutput();
}

void RecordThread::stop()
{
    m_stopFlag = true;
    filename = "";
    // wait 超时须大于 WRITE_TIMEOUT_MS，让 IO 看门狗先中断阻塞的写操作，
    // 尽量避免走到 terminate()（强杀正在写文件的线程会损坏堆/文件）
    if (!wait(5000)) {
        qWarning() << "[RecordThread] timeout, force terminate";
        terminate();
        wait();
    }
}

int RecordThread::interruptCallback(void *opaque)
{
    RecordThread *self = static_cast<RecordThread*>(opaque);
    if (!self) return 0;
    if (self->m_writeWatchdog.isValid() &&
        self->m_writeWatchdog.elapsed() > WRITE_TIMEOUT_MS) {
        return 1; // 单次写 IO 超时，中断，防止无限阻塞
    }
    return 0;
}

void RecordThread::setFormatContext(AVFormatContext *fmtCtx)
{
    m_inFmtCtx = fmtCtx;
}

void RecordThread::setRecordUrl(QString url)
{
    filename = url;
}

void RecordThread::setInpurUrl(QString url)
{
    m_inputUrl = url;
}

bool RecordThread::initOutput(int fileIndex)
{
    // 防御性空指针检查
    if (!m_inFmtCtx) {
        qWarning() << "[RecordThread] m_inFmtCtx is NULL";
        return false;
    }
    int ret = -1;
    frame_count = 0;
    QString url = "";
    QString suffix = QFileInfo(filename).suffix();
    if(fileIndex == 0){  // 如果是录制第一个视频，就不进行后缀添加，
        url = filename;
    } else {
        QString name = QFileInfo(filename).absolutePath() +"/"+QFileInfo(filename).baseName()+ QString("_%1") .arg(fileIndex);
        url = name + "."+ suffix;
    }
    last_dts = 0;
    frame_interval = 0;
    qDebug()<<"url:"<<url;
    //ret = avformat_alloc_output_context2(&outFmtCtx,NULL,"mpegts",url.toStdString().c_str());
    ret = avformat_alloc_output_context2(&outFmtCtx,NULL,NULL,url.toStdString().c_str());

    if(!outFmtCtx) {
        qDebug()<<"Failed to alloc output context2"<<av_err2str_cpp(ret);
        return false;
    }

    // 安装输出 IO 中断回调（兼容 FFmpeg 3.2.12 及 ARM 版本的老 API）
    m_intCB.callback = &RecordThread::interruptCallback;
    m_intCB.opaque   = this;
    outFmtCtx->interrupt_callback = m_intCB;

    bool foundVideo = false;
    for(unsigned int i=0;i<m_inFmtCtx->nb_streams;i++) {
        if(m_inFmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO){
            inStream = m_inFmtCtx->streams[i];
            outStream = avformat_new_stream(outFmtCtx,NULL);
            videoStreamIndex = i;
            if(!outStream) {
                qDebug()<<"Failed to create new stream.";
                avformat_free_context(outFmtCtx);
                outFmtCtx = nullptr;
                return false;
            }
            avcodec_parameters_copy(outStream->codecpar,inStream->codecpar);
            //重置codec_tag防止MP4容器不兼容某些标签
            outStream->codecpar->codec_tag=0;
            outStream->time_base = inStream->time_base;
            foundVideo = true;
            break;
        }
    }
    if (!foundVideo) {
        qWarning() << "[RecordThread] no video stream found in input context";
        avformat_free_context(outFmtCtx);
        outFmtCtx = nullptr;
        return false;
    }
    av_dump_format(outFmtCtx, 0, url.toStdString().c_str(), 1);

    // 设置时间基
    //videoStreamIndex = outStream->index;
    //inStream = m_inFmtCtx->streams[videoStreamIndex];
    if(!(outFmtCtx->oformat->flags & AVFMT_NOFILE)){
        m_writeWatchdog.restart(); // 打开文件前启动看门狗，防止 avio_open2 阻塞
        if(avio_open2(&outFmtCtx->pb,url.toStdString().c_str(),AVIO_FLAG_WRITE,&m_intCB,NULL) < 0){
            qDebug()<<"Could not open output file";
            avformat_free_context(outFmtCtx);
            outFmtCtx = nullptr;
            return false;
        }
    }
    m_writeWatchdog.restart(); // 写文件头前重置看门狗
    if(suffix == QString("mp4")){
        AVDictionary *opts = nullptr;
        av_dict_set(&opts,"movflags","faststart",0);
        ret = avformat_write_header(outFmtCtx,&opts);
        //释放设置参数
        if (opts != NULL)  {
            av_dict_free(&opts);
        }
    }
    else {
        ret = avformat_write_header(outFmtCtx,NULL);
    }
    if(ret < 0) {
        qDebug()<<"Failed to write header"<<av_err2str_cpp(ret);
        if(!(outFmtCtx->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&outFmtCtx->pb);
        }
        avformat_free_context(outFmtCtx);
        outFmtCtx = NULL;
        return false;
    }

    return true;
}

void RecordThread::closeOutput()
{
    if(!outFmtCtx) {
        return;
    }
    m_writeWatchdog.restart(); // 写文件尾前重置看门狗，防止 av_write_trailer 阻塞
    av_write_trailer(outFmtCtx);
    if(!(outFmtCtx->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&outFmtCtx->pb);
    }
    avformat_free_context(outFmtCtx);
    outFmtCtx = NULL;
}
