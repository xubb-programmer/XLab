#include "FFmpegWorker.h"
#include <QDebug>
#include <QDir>
#include <QSettings>
#include <QApplication>
#include <QTextCodec>
// 中断回调（FFmpeg IO 线程 / PullThread 内 av_read_frame 期间调用）
int AVInterruptCallBackFun(void *param)
{
    FFmpegWorker *pworker = (FFmpegWorker*)param;
    if(!pworker) {
        return 0;
    }
    if(pworker->m_stopFlag || pworker->m_interruptRequested.loadAcquire()) {
        return 1;
    }
    int64_t now = av_gettime_relative();
    // open_input/find_stream_info 共用一个总截止时间，UDP 没有数据时也能退出。
    if(pworker->m_initDeadlineUs > 0 && now >= pworker->m_initDeadlineUs) {
        qDebug()<<"[Interrupt] input initialization timeout";
        return 1;
    }
    // 拉流阶段无数据超时。
    if(pworker->m_watchdogEnabled) {
        int64_t elapsedMs = (now - pworker->m_lastFeedUs) / 1000;
        if(elapsedMs > pworker->m_watchdogTimerOut){
            qDebug()<<"[Interrupt] watchdog timeout"<<elapsedMs<<"ms";
            return 1;
        }
    }
    return 0;
}

void FFmpegWorker::feedWatchdog()
{
    m_lastFeedUs = av_gettime_relative();
}

void FFmpegWorker::requestInterrupt()
{
    m_interruptRequested.storeRelease(1);
}

FFmpegWorker::FFmpegWorker()
    :m_stopFlag(false),
      m_interruptRequested(0),
      m_fmtCtx(NULL),
      m_codecCtx(NULL),
      m_winId(NULL),
      m_streamIndex(-1),
      initFlag(false),
      m_currentStatus(Status_Idle)
{
#if LIBAVFORMAT_VERSION_MAJOR < 58
    av_register_all();
#endif
    avformat_network_init();
    m_packetQueue = new SafeQueue<AVPacket>();
    m_frameQueue = new SafeQueue<AVFrame*>();
    m_recordQueue=new SafeQueue<AVPacket>();
    m_relayQueue=new SafeQueue<AVPacket>();

    m_pullThread = new PullThread(m_fmtCtx,m_packetQueue,m_recordQueue,m_relayQueue,this);
    m_decodeThread = new DecodeThread(m_codecCtx,m_packetQueue,m_frameQueue,this);
    m_renderThread = RenderThread::create(m_frameQueue, this);
    m_recordThread = new RecordThread(m_fmtCtx,m_recordQueue,this);
    m_relayThread = new RelayThread(m_fmtCtx,m_relayQueue,this);

    // 连接错误信号
    connect(m_pullThread,&PullThread::errorOccured,this,&FFmpegWorker::sltSubThreadError);
    connect(m_decodeThread,&DecodeThread::errorOccured,this,&FFmpegWorker::sltSubThreadError);
    connect(m_renderThread,&RenderThread::errorOccured,this,&FFmpegWorker::sltSubThreadError);
    connect(m_recordThread,&RecordThread::errorOccured,this,&FFmpegWorker::sltSubThreadError);
    connect(m_relayThread,&RelayThread::errorOccured,this,&FFmpegWorker::sltSubThreadError);
    connect(m_pullThread,&PullThread::signalHideTip,this,&FFmpegWorker::signalHideTip);
    connect(m_pullThread,&PullThread::signalFeedWatchdog,this,&FFmpegWorker::sltFeedWatchDog);
    connect(m_pullThread,&PullThread::signalChangeVideoState,this,&FFmpegWorker::sltChangeVideoState);
    connect(m_renderThread,&RenderThread::signalShowSnapShot,this,&FFmpegWorker::signalShowSnapShot);
}

FFmpegWorker::~FFmpegWorker()
{
    stopPlay();
    avformat_network_deinit();
    delete m_packetQueue;
    delete m_frameQueue;
    delete m_recordQueue;
    delete m_relayQueue;
    delete m_pullThread;
    delete m_decodeThread;
    delete m_renderThread;
    delete m_recordThread;
    delete m_relayThread;
}

void FFmpegWorker::switchPlay(const QString &url, const QVariant &winIdVar)
{
    qDebug() << "switchPlay:" << url;
    stopPlay();
    startPlay(url, winIdVar);
}

void FFmpegWorker::startPlay(const QString &url, const QVariant &winIdVar)
{
    qDebug()<<"startPlay:"<<url;
    // 如果已经在播了，先停止并释放
    if(initFlag) {
        stopPlay();
    }
    m_stopFlag = false;
    m_interruptRequested.storeRelease(0);

    m_winId = reinterpret_cast<void*>(winIdVar.value<qulonglong>());
    // 重置队列 abort 标志，允许消费线程正常等待
    m_packetQueue->resetAbort();
    m_frameQueue->resetAbort();
    m_recordQueue->resetAbort();
    m_relayQueue->resetAbort();

    // ── 状态1：正在拉流中 ──
    emitStatus(Status_Connecting);

    m_watchdogEnabled = false;
    if(!initFFmpeg(url)) {
        qDebug()<<"Faild to initFFmpeg ";
        const bool cancelledByNewRequest = m_interruptRequested.loadAcquire() != 0;
        //m_stopFlag = true;
        stopPlay();
        // 新的切流请求已经在队列中，旧请求不再上报失败或触发重连。
        if(cancelledByNewRequest) {
            qDebug()<<"input initialization cancelled by a newer request";
            return;
        }
        emitStatus(Status_Failed,
                   QString::fromLocal8Bit("无法连接到视频源，请检查地址和网络"));
        emit errorOccured(Error_Init_Fail);
        return;
    }
    m_pullThread->setEagainPara(m_eagainSleepTime);
    m_pullThread->setFormatContext(m_fmtCtx,m_streamIndex);
    m_decodeThread->setCodecContext(m_codecCtx);
    m_renderThread->init(reinterpret_cast<unsigned long>(m_winId),
                         m_codecCtx->width, m_codecCtx->height);
    m_recordThread->setFormatContext(m_fmtCtx);
    m_recordThread->setInpurUrl(url);
    m_relayThread->setInpurUrl(url);
    m_relayThread->setFormatContext(m_fmtCtx);

    feedWatchdog();
    m_watchdogEnabled = true; // PullThread 启动后启用看门狗

    ensureWorkerThreadsStopped();
    if(m_pullThread) {
        m_pullThread->start();
    }
    if(m_decodeThread) {
        m_decodeThread->start();
    }
    if(m_renderThread) {
        m_renderThread->start();
    }
    initFlag = true;
}

void FFmpegWorker::stopPlay()
{
    qDebug()<<__FUNCTION__<<"m_stopFlag:"<<m_stopFlag<<"initFlag:"<<initFlag;
    if(m_stopFlag && !initFlag) {
        return;
    }
    initFlag = false;
    m_watchdogEnabled = false;
    m_stopFlag = true;  // 中断回调立即返回，避免av_frame_read阻塞
    m_interruptRequested.storeRelease(1);
    m_initDeadlineUs = 0;

    stopRecordVideo();  // 内部已调用 m_recordThread->stop()
    stopRelayVideo();   // 内部已调用 m_relayThread->stop()

    // 先唤醒队列，再停子线程：Decode/Render 阻塞在 tryPop 时能立刻退出，
    // 避免 stop() wait 超时后 terminate 导致切流后无画面
    m_packetQueue->wakeAll();
    m_frameQueue->wakeAll();
    m_recordQueue->wakeAll();
    m_relayQueue->wakeAll();

    // 先停拉流（断包来源），再停解码/渲染
    if(m_pullThread)  {  m_pullThread->stop();  }
    if(m_decodeThread){  m_decodeThread->stop();}
    if(m_renderThread){  m_renderThread->stop();}

    drainPacketQueue(m_packetQueue);
    drainFrameQueue(m_frameQueue);
    drainPacketQueue(m_recordQueue);
    drainPacketQueue(m_relayQueue);
    releaseFFmpeg();
    if (m_currentStatus != Status_Reconnecting) {
        // emitStatus(Status_Stopped);
    }
    qDebug()<<"====================happy ending============";
}

void FFmpegWorker::snapShot(const QString &filename)
{
    if(m_stopFlag || !initFlag) {
        emit errorOccured(Error_Snapshot_Invaild_Frame);
        return;
    }
    if(cheakDir(filename)){
        if(m_renderThread){
            m_renderThread->snapShot(filename);
        }
    } else {
        emit errorOccured(Error_Snapshot_Fail);
        qDebug()<<"Fail to create snapShot path.";
    }
}

void FFmpegWorker::startRecordVideo(const QString &filename)
{
    if(m_stopFlag || !initFlag) {
        emit errorOccured(Error_Record_InitOutput_Fail);
        return;
    }
    if(cheakDir(filename)) {
#if 0
        DiskSpaceChecker::getInstance().stopChecking();
        DiskSpaceChecker::getInstance().setRecordPath(QFileInfo(filename).absolutePath());
        qint64 freeSpace = DiskSpaceChecker::getInstance().checkNow();
        qint64 threshold = static_cast<qint64>(g_freeSpaceThreshold * 1024LL * 1024LL);
        if(freeSpace < threshold){
            emit errorOccured(Error_Low_Disk_Space);
            return;
        }
#endif
        if(m_pullThread){
            m_recordThread->setRecordUrl(filename);
            m_recordThread->start();
            m_pullThread->setRecordFlag(true); // 先启动在开标志位，避免丢帧
        }
    }
    else {
        qDebug()<<"Fail to create snapShot path.";
        emit errorOccured(Error_Record_Invaild_Path);
    }

}

void FFmpegWorker::stopRecordVideo()
{
    if(m_pullThread){
        m_pullThread->setRecordFlag(false);
        m_recordThread->stop();
    }
}

void FFmpegWorker::startRelayVideo(const QString &url)
{
    if(m_stopFlag || !initFlag) {
        emit errorOccured(Error_Relay_InitOutput_Fail);
        return;
    }
    if(url.isEmpty()){
        return;// 无效地址
    }
    if(m_pullThread) {
        m_relayThread->setRelayUrl(url);
        m_relayThread->start();
        m_pullThread->setRelayFlag(true);
    }
}

void FFmpegWorker::stopRelayVideo()
{
    if(m_pullThread) {
        m_pullThread->setRelayFlag(false);
        m_relayThread->stop();
    }
}

void FFmpegWorker::sltFeedWatchDog()
{
    feedWatchdog();
}

void FFmpegWorker::sltSubThreadError(ErrorType err)
{
    // 透传给上层（VideoProcessWidget 仍能收到 errorOccured）
    emit errorOccured(err);
    switch (err) {
    case Error_End_Of_File:
        emitStatus(Status_Reconnecting,
                   QString::fromLocal8Bit("视频流中断，正在尝试重新连接"));
        break;
    case Error_Read_Timeout:
        emitStatus(Status_Reconnecting,
                   QString::fromLocal8Bit("网络超时，正在尝试重新连接"));
        break;
    default:
        break;
    }
}

void FFmpegWorker::sltChangeVideoState(VideoStatus status, const QString &extra)
{
    emitStatus(status,extra);
}

void FFmpegWorker::sltLowDiskSpace()
{
    emit errorOccured(Error_Low_Disk_Space);
    stopRecordVideo();
}

bool FFmpegWorker::initFFmpeg(QString url)
{
    AVDictionary *opts = NULL;
    bool isRtsp = (strncmp(url.toLatin1().data(),"rtsp://",7)==0);
    bool isRtmp = (strncmp(url.toLatin1().data(),"rtmp://",7)==0);
    QSettings settings(QApplication::applicationDirPath() + "/config/VideoProcess.ini", QSettings::IniFormat);
    settings.setIniCodec(QTextCodec::codecForName("UTF-8"));
    QString name_key =  "Video/rtspTransport";
    QString rtspTansport = "udp";
    rtspTansport = settings.value(name_key).toString();
    if(rtspTansport.isEmpty()){
        rtspTansport = QString("udp");
    }
    m_watchdogTimerOut = settings.value("Video/watchDog").toInt();
    m_watchdogTimerOut = m_watchdogTimerOut*1000;
    if (m_watchdogTimerOut == 0){
        m_watchdogTimerOut = 6000;
    }
    m_eagainSleepTime = settings.value("Video/eagainSleepTime").toInt();
    if(m_eagainSleepTime == 0){
        m_eagainSleepTime = 10;
    }
    int initTimeoutMs = settings.value("Video/connectTimeoutMs", 8000).toInt();
    if(initTimeoutMs < 1000) {
        initTimeoutMs = 8000;
    }
    m_fmtCtx = avformat_alloc_context();
    m_fmtCtx->interrupt_callback.callback = AVInterruptCallBackFun;
    m_fmtCtx->interrupt_callback.opaque = this;
    m_fmtCtx->use_wallclock_as_timestamps = 1;

    if(isRtsp){
        av_dict_set(&opts,"fflags",         "nobuffer", 0);  // 禁用输入缓冲
        av_dict_set(&opts,"flags",          "low_delay",0);  // 禁用输出缓冲
        av_dict_set(&opts, "rtsp_transport", rtspTansport.toLatin1().data(), 0);//
        av_dict_set(&opts,"avioflags",      "direct",   0);  // 减少缓冲
        av_dict_set(&opts,"probesize",      "32768",    0);  // 降低初始探测大小（快速启动）
        av_dict_set(&opts,"analyzeduration","0",        0); //
        av_dict_set(&opts, "threads",       "4",        0); //自动开启线程数
        av_dict_set(&opts, "stimeout",      "3000000",  0); //设置超时3s, 防止阻塞
        av_dict_set(&opts, "rw_timeout",    "5000000",   0); // 读写超时 5s
        //av_dict_set(&opts,"wait_for_keyframe", "1",   0); //强制等待关键帧，编码解码非I帧
        //av_dict_set(&opts,"reconnect",      "1",        0); //
        //av_dict_set(&opts,"reconnect_streamed","1",     0);
        //av_dict_set(&opts,"reconnect_delay_max","5",    0);
    } else if(isRtmp){
        av_dict_set(&opts,"fflags",         "nobuffer", 0); // 禁用输入缓冲
        av_dict_set(&opts,"flags",          "low_delay",0); // 强制低延迟
        av_dict_set(&opts, "threads",       "4",        0); //自动开启线程数
        av_dict_set(&opts, "rw_timeout",    "5000000",  0); //设置超时3s, 防止阻塞
        av_dict_set(&opts,"probesize",      "4096",      0); // 降低初始探测大小（快速启动）
        av_dict_set(&opts,"analyzeduration","500000",        0); //减少流分析时间微秒
        av_dict_set(&opts,"maxdelay",       "500000",   0); //设置最大解封装延迟 0.5秒
        av_dict_set(&opts,"rtmp_live",       "live",    0); //设置最大解封装延迟 0.5秒
        //av_dict_set(&opts,"flvflags",       "no_sequence_end",0); //
        //av_dict_set(&opts,"flash_packets",   "1",       0); //
    } else if(url.startsWith("udp://", Qt::CaseInsensitive)) {
        av_dict_set(&opts,"fflags",         "nobuffer",  0);
        av_dict_set(&opts,"flags",          "low_delay", 0);
        av_dict_set(&opts,"probesize",      "32768",     0);
        av_dict_set(&opts,"analyzeduration","1000000",   0);
        av_dict_set(&opts,"rw_timeout",     "5000000",   0);
    }
    m_initDeadlineUs = av_gettime_relative() + static_cast<int64_t>(initTimeoutMs) * 1000;
    int ret = avformat_open_input(&m_fmtCtx,url.toUtf8().data(),NULL,&opts);
    //释放设置参数
    if (opts != NULL)  {
        av_dict_free(&opts);
    }
    if(ret < 0 || !m_fmtCtx) {
        m_initDeadlineUs = 0;
        QString reason = av_err2str_cpp(ret);
        qDebug() << "avformat_open_input failed:" << reason;
        // 区分连接拒绝、地址错误、超时三种情况，给出更精确的中文提示
        QString cnReason;
        if (ret == AVERROR(ECONNREFUSED))
            cnReason = QString::fromLocal8Bit("连接被拒绝，请确认服务端正在运行");
        else if (ret == AVERROR(ETIMEDOUT) || ret == AVERROR_EXIT)
            cnReason = QString::fromLocal8Bit("连接超时，请检查网络或地址");
        else
            cnReason = QString::fromLocal8Bit("无法打开地址（") + reason + ")";
        emitStatus(Status_Failed, cnReason);
        m_fmtCtx = nullptr;
        return false;
    }
    ret = avformat_find_stream_info(m_fmtCtx,NULL);
    if(ret < 0){
        m_initDeadlineUs = 0;
        qDebug()<<"failedavformat_find_stream_info"<<av_err2str_cpp(ret);
        emitStatus(Status_Failed,
                   QString::fromLocal8Bit("无法获取流信息，服务端可能尚未推流"));
        return false;
    }
    m_initDeadlineUs = 0;
    m_fmtCtx->flags |= AVFMT_FLAG_NONBLOCK;  // 设置为非阻塞模式

    m_streamIndex  = av_find_best_stream(m_fmtCtx,AVMEDIA_TYPE_VIDEO,-1,-1,NULL,0);

    if (m_streamIndex == -1){
        qDebug ()<<"No video stream found ";
        emit errorOccured(Error_Init_NoVideoStream);
        emitStatus(Status_Failed, errorTypeToString(Error_Init_NoVideoStream));
        return false;
    }
    AVCodecParameters *par = m_fmtCtx->streams[m_streamIndex]->codecpar;
    //前置校验：RTMP H.264/HEVC 必须有 extradata，否则说明
    //   find_stream_info 没有读到 AVCDecoderConfigurationRecord，
    //   此时继续会导致解码器初始化不完整，第一帧必然花屏或 mismatch。
    //   直接返回 false 触发上层重连，等服务端推送完整的 codec config。
    if(!par){
        emit errorOccured(Error_Init_NoExtradata);
        emitStatus(Status_Failed, errorTypeToString(Error_Init_NoExtradata));
        return false;
    }
    if (isRtmp) {
        if (par->codec_id == AV_CODEC_ID_H264 || par->codec_id == AV_CODEC_ID_HEVC) {
            if (!par->extradata || par->extradata_size < 4) {
                qDebug() << "[initFFmpeg] RTMP extradata missing"
                         << "codec_id=" << par->codec_id
                         << "extradata_size=" << (par ? par->extradata_size : -1);
                emit errorOccured(Error_Init_NoExtradata);
                emitStatus(Status_Failed, errorTypeToString(Error_Init_NoExtradata));
                return false;
            }
        }
    }
    const AVCodec *codec = avcodec_find_decoder(par->codec_id);
    if( !codec ) {
        qDebug()<<"Unsupported codec"<<par->codec_id;
        emit errorOccured(Error_Init_CodecNotFound);
        emitStatus(Status_Failed, errorTypeToString(Error_Init_CodecNotFound));
        return false;
    }
    m_codecCtx =  avcodec_alloc_context3(codec);
    if(!m_codecCtx) {
        qDebug ()<<"avcodec_alloc_context3 failed ";
        emitStatus(Status_Failed, QString::fromLocal8Bit("解码器上下文内存分配失败"));
        return false;
    }
    ret = avcodec_parameters_to_context(m_codecCtx,par);
    if(ret < 0){
        qDebug ()<<"avcodec_parameters_to_context failed "<<av_err2str_cpp(ret);
        emitStatus(Status_Failed, QString::fromLocal8Bit("解码器参数拷贝失败"));
        return false;
    }
    //设置加速解码
    m_codecCtx->lowres = codec->max_lowres;
    m_codecCtx->thread_count = 2;
    m_codecCtx->thread_type = FF_THREAD_SLICE; //1 frame: 2: slice
    if(avcodec_open2(m_codecCtx,codec,NULL) < 0) {
        qDebug()<<"failed to open codec";
        emit errorOccured(Error_Init_CodecOpenFail);
        emitStatus(Status_Failed, errorTypeToString(Error_Init_CodecOpenFail));
        return false;
    }
    int width = m_codecCtx->width;
    int height = m_codecCtx->height;
    if(width ==0 || height == 0){
        qDebug()<<"Invalid width/height";
        emit errorOccured(Error_Init_InvalidResolution);
        emitStatus(Status_Failed, errorTypeToString(Error_Init_InvalidResolution));
        return false;
    }
    //Output Info-----------------------------
    printf("--------------- File Information ----------------\n");
    av_dump_format(m_fmtCtx,0,url.toUtf8().data(),0);
    printf("-------------------------------------------------\n");
    return true;
}

void FFmpegWorker::ensureWorkerThreadsStopped()
{
    auto waitThread = [](QThread *t, const char *name) {
        if (!t || !t->isRunning()) {
            return;
        }
        qWarning() << name << "still running before restart, waiting...";
        if (!t->wait(5000)) {
            qWarning() << name << "force terminate before restart";
            t->terminate();
            t->wait();
        }
    };
    waitThread(m_pullThread, "PullThread");
    waitThread(m_decodeThread, "DecodeThread");
    waitThread(m_renderThread, "RenderThread");
}

void FFmpegWorker::releaseFFmpeg()
{
    if(m_codecCtx) {
        //avcodec_close(m_codecCtx); // 关闭解码器
        avcodec_flush_buffers(m_codecCtx);
        avcodec_free_context(&m_codecCtx);//释放上下文 内部已经调用avcodec_close
        m_codecCtx = NULL;
    }
    if(m_fmtCtx) {
        //#ifdef Q_OS_LINUX
        avformat_close_input(&m_fmtCtx);// 暂时取消释放，会偶发崩溃
        m_fmtCtx = NULL;
        //#endif
    }
    m_streamIndex = -1;
}

bool FFmpegWorker::cheakDir(QString path)
{
    //检查路径
    QDir dir = QFileInfo(path).absolutePath();
    bool exist = dir.exists();
    if (!exist) {
        if( !dir.mkpath(QFileInfo(path).absolutePath())) {
            return false;
        }
    }
    return true;
}

void FFmpegWorker::drainPacketQueue(SafeQueue<AVPacket> *q)
{
    if(!q)return;
    while(!q->empty()) {
        AVPacket pkt = q->pop();
        av_packet_unref(&pkt);
    }
}

void FFmpegWorker::drainFrameQueue(SafeQueue<AVFrame*> *q)
{
    if(!q)return;
    while(!q->empty()) {
        AVFrame *frame = q->pop();
        av_frame_free(&frame);
    }
}

void FFmpegWorker::emitStatus(VideoStatus status, const QString &extra)
{
    // 允许Reconnecting同状态重复上报 ,其余状态去重
    if (/*status != Status_Reconnecting &&*/ status == m_currentStatus) return;
    m_currentStatus = status;
    QString msg = videoStatusToString(status);
    if (!extra.isEmpty()) {
        msg += QString::fromLocal8Bit("：") + extra;
    }
    qDebug() << "[FFmpegWorker] status ->" << msg;
    emit signalStatusChanged(status, msg);
}
