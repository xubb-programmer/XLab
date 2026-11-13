#include "VideoProcessWidget.h"
#include "ui_VideoProcessWidget.h"
#include <QPushButton>
#include <QDebug>
#include <QDateTime>
#include <QFileInfo>
#include <QDir>
#include "UiPluginFactory.h"
#include <QTextCodec>
#include <QImage>
#include <QTimer>
#include "FrameLogManager.h"
#include <QHostAddress>
#include <QScreen>
#include <QPixmap>
#include <QPalette>

namespace {
QString closeScreenDialogName()
{
    return QString::fromUtf8(u8"关闭投屏提示");
}
}

int g_SegmentTime = 30; // 300min
uint g_freeSpaceThreshold = 300; // 300M

VideoProcessWidget::VideoProcessWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::VideoProcessWidget),
    m_pFFmpegWorker(nullptr),
    m_workerThread(nullptr),
    m_pPlugin(nullptr),
    m_reconnectTimer(nullptr),
    m_workerState(State_Idle),
    m_currentStatus(Status_Idle)
{
    ui->setupUi(this);
    ui->widget_test->hide();
}

VideoProcessWidget::~VideoProcessWidget()
{
    // 1. 先停所有定时器，避免析构期间 timeout 触发重连/状态上报/恢复回调，
    //    访问处于半销毁状态的成员导致崩溃
    if(m_reconnectTimer)    m_reconnectTimer->stop();
    if(m_pVideoStatusTimer) m_pVideoStatusTimer->stop();
    if(m_recoverFunTimer)   m_recoverFunTimer->stop();

    // 2. 让 stopPlay 在 worker 线程上完整执行后线程再退出。
    //    - 用队列连接投递，主线程不阻塞（BlockingQueuedConnection 会与
    //      worker 回主线程的信号互等而卡死 UI）。
    //    - stopPlay 先于 quit 入队，事件循环 FIFO 处理完 stopPlay 才退出。
    //    - wait 超时必须大于 stopPlay 内部最长阻塞（看门狗 + 各子线程 join），
    //      否则会在线程仍运行时 delete，造成 use-after-free / 线程被强删崩溃。
    if(m_workerThread && m_workerThread->isRunning()) {
        if(m_pFFmpegWorker) {
            QMetaObject::invokeMethod(m_pFFmpegWorker, "stopPlay", Qt::QueuedConnection);
        }
        m_workerThread->quit();
        if(!m_workerThread->wait(8000)) {
            qWarning() << "[~VideoProcessWidget] workerThread wait timeout, terminate";
            m_workerThread->terminate();
            m_workerThread->wait();
        }
    }

    // 3. 停止屏幕推流
    if(m_pScreenPusher){
        m_pScreenPusher->stop();
    }
    DiskSpaceChecker::getInstance().stopChecking();

    // 4. 此时 worker 线程已退出，可安全销毁（worker 无 parent，需手动 delete）
    delete m_pFFmpegWorker;
    m_pFFmpegWorker = nullptr;
    delete m_workerThread;
    m_workerThread = nullptr;
    delete ui;
}

void VideoProcessWidget::initUi(IUiPluginBase *pPlugin)
{
    //获取参数
    m_pPlugin = pPlugin;
    m_pScreenPusher = new ScreenStreamPusher(this);
    connect(m_pScreenPusher, &ScreenStreamPusher::signalHintMessage,
            this, &VideoProcessWidget::signalHintMessage);
    connect(m_pScreenPusher, &ScreenStreamPusher::signalUpdateScreenPushStyle,
            this, &VideoProcessWidget::sltUpdateScreenPushStyle);

    m_pShowSnapWgt = new SnapShotShowWidget(this);
    m_pShowSnapWgt->hide();
}

void VideoProcessWidget::initData()
{
    QPixmap pixmap_bg;
    QStringList formats = {"png","jpg","bmp"};
    for (const QString &format : formats){
        QString filePath = QApplication::applicationDirPath()+"/config/VideoOffLine."+format;
        bool flag = pixmap_bg.load(filePath);
        if(flag){
            break;
        }
    }
    if(pixmap_bg.isNull()){
        ui->label->setStyleSheet("border-image: url(:/resource/VideoOffLine.png);");
    } else {
        pixmap_bg = pixmap_bg.scaled(ui->label->size(),Qt::IgnoreAspectRatio);
        ui->label->setPixmap(pixmap_bg);
    }

    m_workerThread = new QThread(this);
    m_pFFmpegWorker = new FFmpegWorker();
    m_pFFmpegWorker->moveToThread(m_workerThread);
    connect(this,&VideoProcessWidget::signalStartPlay,m_pFFmpegWorker,&FFmpegWorker::startPlay);
    connect(this,&VideoProcessWidget::signalStopPlay,m_pFFmpegWorker,&FFmpegWorker::stopPlay);
    qRegisterMetaType<QVariant>("QVariant");
    connect(m_pFFmpegWorker,&FFmpegWorker::signalShowSnapShot,m_pShowSnapWgt,&SnapShotShowWidget::sltShowSnapShot);

    qRegisterMetaType<ErrorType>("ErrorType");
    qRegisterMetaType<VideoStatus>("VideoStatus");

    connect(m_pFFmpegWorker,&FFmpegWorker::errorOccured,this,&VideoProcessWidget::sltErrorOccured);
    connect(m_pFFmpegWorker,&FFmpegWorker::signalHideTip,this,&VideoProcessWidget::sltHideTip);
    connect(m_pFFmpegWorker, &FFmpegWorker::signalStatusChanged,
            this, &VideoProcessWidget::sltVideoStatusChanged);

    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer,&QTimer::timeout,this,&VideoProcessWidget::sltDoReconect);

    m_workerThread->start();

    connect(ui->startBtn,&QPushButton::clicked,this,&VideoProcessWidget::sltStartPlayClicked);
    connect(ui->stopBtn,&QPushButton::clicked,this,&VideoProcessWidget::sltStopPlayClicked);
    connect(ui->snapshotBtn,&QPushButton::clicked,this,&VideoProcessWidget::sltSnapshotClicked);
    connect(ui->recordBtn,&QPushButton::clicked,this,&VideoProcessWidget::sltRecordVideoClicked);
    connect(ui->relayBtn,&QPushButton::clicked,this,&VideoProcessWidget::sltRelayVideoClicked);
    connect(ui->ScreenPushBtn,&QPushButton::clicked,this,&VideoProcessWidget::sltScreenPushBtnClicked);
    connect(ui->changeBtn,&QPushButton::clicked,this,&VideoProcessWidget::sltChangeBtnClicked);
    sltStartPlayClicked();

    m_pVideoStatusTimer = new QTimer(this);
    m_pVideoStatusTimer->setInterval(3000);
    connect(m_pVideoStatusTimer,&QTimer::timeout,this,&VideoProcessWidget::sltVideoStatusTimerout);
    m_pVideoStatusTimer->start();

    DiskSpaceChecker::getInstance().startChecking();
    connect(&DiskSpaceChecker::getInstance(),&DiskSpaceChecker::signalLowDiskSpace,
            this,&VideoProcessWidget::sltLowDiskSpace);

    m_recoverFunTimer = new QTimer(this);
    m_recoverFunTimer->setSingleShot(true);
    m_recoverFunTimer->setInterval(1000);
    connect(m_recoverFunTimer,&QTimer::timeout,this,&VideoProcessWidget::sltRecoverFunc);
}

void VideoProcessWidget::resetQss(const QString &strStyleName)
{

}

void VideoProcessWidget::registerProtocols()
{
    qRegisterMetaType<MessageType>("MessageType");
    std::vector<IUiPluginBase *> vPlugins =
            UiPluginFactory::getInstance()->getPlugins("SignalTransit");
    foreach (IUiPluginBase * plugin, vPlugins) {
        connect(plugin->getWidget(),SIGNAL(signalPluginMsg(pluginmsg)),this,SLOT(sltsignalPluginMsg(pluginmsg)));//接收
        connect(this,SIGNAL(signalPluginMsg(pluginmsg)),plugin->getWidget(),SIGNAL(signalPluginMsg(pluginmsg))); //发送
    }

    vPlugins  = UiPluginFactory::getInstance()->getPlugins("MessageBox");
    foreach (IUiPluginBase * plugin, vPlugins) {
        connect(this,SIGNAL(signalHintMessage(QString,QString,MessageType)),plugin->getWidget(),SLOT(onMessageText(QString,QString,MessageType)));
        connect(this,SIGNAL(signalMessageText(QString,QString,QString,QString,QString)),plugin->getWidget(),SLOT(onMessageText(QString,QString,QString,QString,QString)));
        connect(plugin->getWidget(),SIGNAL(signalConfirmBtnClicked(QString)),this,SLOT(sltConfirmClicked(QString)));
    }
    std::vector<IUiPluginBase *> vPlugins3  = UiPluginFactory::getInstance()->getPlugins("ParameterSet");
    foreach (IUiPluginBase * plugin, vPlugins3) {
        if(plugin->getWidget()){
            connect(plugin->getWidget(),SIGNAL(signalToVideoProcessFFmpeg(QString)),this,SLOT(slotSetPStr(QString)));
        }
    }
}

void VideoProcessWidget::slotSetPStr(QString url)
{
    sltReSetPullStream(url);
}

void VideoProcessWidget::sltReSetPullStream(QString &url)
{
    FrameLogManager::getInstance()->log(QString("VideoProcessFFmpeg"),
                                        QString::fromLocal8Bit("收到拉流地址:%1").arg(url),
                                        IFrameLogManager::DEBUG_TYPE);
    if(url.isEmpty()){
        QString errLog = QString::fromUtf8(u8"地址为空，拉流设置失败!");
        emit signalHintMessage(QString::fromUtf8(u8"拉流设置"),errLog,MessageType_Err);
        return;
    }
    if(m_workerState == State_Stopping){
        QString errLog = QString::fromUtf8(u8"正在停止中，请稍后再操作!");
        emit signalHintMessage(QString::fromUtf8(u8"拉流设置"),errLog,MessageType_Warn);
        return;
    }
    // ★ 新增：取消正在等待的重连，避免重连流程与本次切流冲突
    if(m_reconnectTimer && m_reconnectTimer->isActive()){
        m_reconnectTimer->stop();
    }
    m_workerState = State_Stopping;  // 标记，防止 sltDoReconect 再触发
    if(!m_pFFmpegWorker) {
        m_workerState = State_Idle;
        return;
    }
    void* winId = (void*)ui->label->winId();
    QVariant winIdVar = QVariant::fromValue(reinterpret_cast<qulonglong>(winId));
    m_workerState = State_Pulling;
    // 必须直接置位原子取消标志：worker 若正阻塞在 FFmpeg 初始化中，
    // 排队调用无法被处理。中断回调退出后，下面的 switchPlay 才能执行。
    m_pFFmpegWorker->requestInterrupt();
    // 在 worker 线程上原子 stop+start，避免 stop/start 分开发送导致
    // 残留 stopPlay 杀死新流，或 DecodeThread terminate 后 start() 无效
    QMetaObject::invokeMethod(m_pFFmpegWorker, "switchPlay", Qt::QueuedConnection,
                              Q_ARG(QString, url), Q_ARG(QVariant, winIdVar));
    // emit signalHintMessage(QString::fromUtf8(u8"拉流设置"),
    //                        QString::fromUtf8(u8"拉流设置成功!"),
    //                        MessageType_Success);
    if(m_recoverFunTimer->isActive()){
        m_recoverFunTimer->stop();
    }
    m_recoverFunTimer->start();
}

void VideoProcessWidget::sltsignalPluginMsg(pluginmsg pMsg)
{
    if( pMsg.id < 0){
        return;
    }
    if( pMsg.pluginid == m_pPlugin->getId()){
        return;
    }
    if(Command_Snapshot == pMsg.id) //截图
    {
        sltSnapshotClicked();
    }
    if(Command_Record == pMsg.id) //录像控制
    {
        pluginmsg msg;
        memset(&msg,0,sizeof(msg));
        msg.pluginid = m_pPlugin->getId();
        msg.id = Command_Record;
        if(pMsg.state == 1){
            if(m_pFFmpegWorker) {
                msg.state = 2;
                emit signalPluginMsg(msg);
                QString path=getParaValue("Record","path").toString();
                g_SegmentTime=getParaValue("Record","segmentTime").toInt();
                if(path.isEmpty()){
                    path = QApplication::applicationDirPath()+"/Video/";
                }
                QString videoName = path+ QDateTime::currentDateTime().toString("yyyy-MM-dd-hh-mm-ss-zzz")+".ts";
                QMetaObject::invokeMethod(m_pFFmpegWorker,"startRecordVideo",Qt::QueuedConnection,Q_ARG(QString,videoName));
                m_recordHandFlag = true;
            }
        }
        else {
            if(m_pFFmpegWorker) {
                msg.state = 1;//1 关 2开
                emit signalPluginMsg(msg);
                QMetaObject::invokeMethod(m_pFFmpegWorker,"stopRecordVideo",Qt::QueuedConnection);
                m_recordHandFlag = false;
            }
        }
    }
    if(Command_Relay == pMsg.id)//上报控制
    {
        pluginmsg msg;
        memset(&msg,0,sizeof(msg));
        msg.pluginid = m_pPlugin->getId();
        msg.id = Command_Relay;
        if(pMsg.state == 1){
            if(m_pFFmpegWorker) {
                QString path = getParaValue("Video","outputUrl").toString();
                if (path.isEmpty()) {
                    sltErrorOccured(Error_Relay_Invaild_URL);
                    return;
                }
                msg.state = 2;//1 关 2开
                emit signalPluginMsg(msg);
                QMetaObject::invokeMethod(m_pFFmpegWorker,"startRelayVideo",Qt::QueuedConnection,Q_ARG(QString,path));
                m_relayHandFlag = true;
            }
        } else {
            if(m_pFFmpegWorker) {
                msg.state = 1;//1 关 2开
                emit signalPluginMsg(msg);
                QMetaObject::invokeMethod(m_pFFmpegWorker,"stopRelayVideo",Qt::QueuedConnection);
                m_relayHandFlag = false;
            }
        }
    }
    if(Command_ScreenPush == pMsg.id)//屏幕采集上报控制
    {
        pluginmsg msg;
        memset(&msg,0,sizeof(msg));
        msg.pluginid = m_pPlugin->getId();
        msg.id = Command_ScreenPush;
        if(pMsg.state == 1){
            int ret = m_pScreenPusher->start();
            if(ret == 0){
                msg.state = 2;//1 关 2开
                emit signalPluginMsg(msg);
            }
        } else {
            // 关闭投屏前复用软件退出时的自定义确认弹窗；确认前保持投屏和按钮状态。
            // 使用宽字符 Unicode 转义，避免英文 Docker/非中文代码页把文本编译成问号。
            emit signalMessageText(closeScreenDialogName(),
                                   QString::fromUtf8(u8"确定关闭投屏？"),
                                   QString::fromUtf8(u8"提示"),
                                   QString::fromUtf8(u8"关闭投屏"),
                                   QString::fromUtf8(u8"继续投屏"));
        }
    }
}

void VideoProcessWidget::sltConfirmClicked(QString text)
{
    if (text != closeScreenDialogName())
        return;

    m_pScreenPusher->stop();
    pluginmsg msg;
    memset(&msg, 0, sizeof(msg));
    msg.pluginid = m_pPlugin->getId();
    msg.id = Command_ScreenPush;
    msg.state = 1; //1 关 2开
    emit signalPluginMsg(msg);
}

void VideoProcessWidget::sltErrorOccured(ErrorType value)
{
    // 将错误码转为中文描述
    QString errLog = errorTypeToString(value);
    pluginmsg pMsg;
    switch (value) {
    case Error_Init_Fail:
        //scheduleReconnect(2000);
        return;
    case Error_End_Of_File:
        // scheduleReconnect(2000); // 2s后重连
        return;
    case Error_Init_NoExtradata:
        return;
    case Error_Init_InvalidResolution:
        return;
    case Error_Snapshot_Invaild_Frame:
        errLog = QString::fromUtf8(u8"截图失败，无视频数据");
        break;
    case Error_Snapshot_Fail:
        errLog = QString::fromUtf8(u8"截图失败，截图路径创建失败");
        break;
    case Error_Record_InitOutput_Fail:
        errLog = QString::fromUtf8(u8"录像失败，录像初始化失败");
        memset(&pMsg,0,sizeof(pluginmsg));
        pMsg.pluginid = m_pPlugin->getId();
        pMsg.id = 5;
        pMsg.state = 1;//1 关 2开
        emit signalPluginMsg(pMsg);
        m_recordHandFlag = false;
        break;
    case Error_Record_Invaild_Path:
        errLog = QString::fromUtf8(u8"录像失败，录像地址无效");
        memset(&pMsg,0,sizeof(pluginmsg));
        pMsg.pluginid = m_pPlugin->getId();
        pMsg.id = 5;
        pMsg.state = 1;//1 关 2开
        emit signalPluginMsg(pMsg);
        m_recordHandFlag = false;
        break;
    case Error_Low_Disk_Space:
        errLog = QString::fromUtf8(u8"录像失败，磁盘空间不足%1MB").arg(g_freeSpaceThreshold);
        memset(&pMsg,0,sizeof(pluginmsg));
        pMsg.pluginid = m_pPlugin->getId();
        pMsg.id = 5;
        pMsg.state = 1;//1 关 2开
        emit signalPluginMsg(pMsg);
        m_recordHandFlag = false;
        break;
    case Error_Relay_InitOutput_Fail:
        errLog = QString::fromUtf8(u8"上报失败，上报初始化失败");
        memset(&pMsg,0,sizeof(pluginmsg));
        pMsg.pluginid = m_pPlugin->getId();
        pMsg.id = 3;
        pMsg.state = 1;//1 关 2开
        emit signalPluginMsg(pMsg);
        m_relayHandFlag = false;
        break;
    case Error_Relay_Invaild_URL:
        errLog = QString::fromUtf8(u8"上报失败，上报地址无效");
        memset(&pMsg,0,sizeof(pMsg));
        pMsg.pluginid = m_pPlugin->getId();
        pMsg.id = 3;
        pMsg.state = 1;//1 关 2开
        emit signalPluginMsg(pMsg);
        m_relayHandFlag = false;
        break;
    default:
        break;
    }
    FrameLogManager::getInstance()->log(QString("VideoProcessFFmpeg"),
                                        QString("%1").arg(errLog),
                                        IFrameLogManager::DEBUG_TYPE);
    emit signalHintMessage(QString::fromUtf8(u8"错误"),errLog,MessageType_Err);
}

void VideoProcessWidget::sltHideTip()
{
}

void VideoProcessWidget::sltStartPlayClicked()
{
    if(m_pFFmpegWorker) {
        //使用队列连接确保在子线程执行
        //       QMetaObject::invokeMethod(m_pFFmpegWorker,"stopPlay",Qt::QueuedConnection);
        QString url=getParaValue("Video","inputUrl").toString();
        if(url.isEmpty()){
            m_workerState = State_Idle;
            return;
        }
        void* winId = (void*)ui->label->winId();
        // 将void* 转换为qulonglong 再包装成QVariant
        QVariant winIdVar = QVariant::fromValue(reinterpret_cast<qulonglong>(winId));
        m_workerState  = State_Pulling;
        m_pFFmpegWorker->requestInterrupt();
        emit signalStartPlay(url,winIdVar);
        FrameLogManager::getInstance()->log(QString("VideoProcessFFmpeg"),
                                            QString::fromLocal8Bit("开始拉流地址:%1").arg(url),
                                            IFrameLogManager::DEBUG_TYPE);
    }
}

void VideoProcessWidget::sltStopPlayClicked()
{
    if(m_reconnectTimer){
        m_reconnectTimer->stop();
    }
    if(m_pFFmpegWorker) {
        m_pFFmpegWorker->requestInterrupt();
    }
    emit signalStopPlay();
    update();
}

void VideoProcessWidget::sltSnapshotClicked()
{
    QString path=getParaValue("SnapShot","path").toString();
    if(path.isEmpty()){
        path = QApplication::applicationDirPath()+"/Picture/";
    }
    QString picName = path + QDateTime::currentDateTime().toString("yyyy-MM-dd-hh-mm-ss-zzz")+".jpg";
    if(m_pFFmpegWorker) {
        QMetaObject::invokeMethod(m_pFFmpegWorker,"snapShot",Qt::QueuedConnection,Q_ARG(QString,picName));
    }
}

void VideoProcessWidget::sltRecordVideoClicked()
{
    if(ui->recordBtn->isChecked()){
        if(m_pFFmpegWorker) {
            QString path = getParaValue("Record","path").toString();
            g_SegmentTime = getParaValue("Record","segmentTime").toInt();
            g_freeSpaceThreshold = getParaValue("Storage","freeSpaceThreshold").toUInt();
            if(path.isEmpty()){
                path = QApplication::applicationDirPath()+"/Video/";
            }
            QString videoName = path+ QDateTime::currentDateTime().toString("yyyy-MM-dd-hh-mm-ss-zzz")+".ts";
            QMetaObject::invokeMethod(m_pFFmpegWorker,"startRecordVideo",Qt::QueuedConnection,Q_ARG(QString,videoName));
        }
    }else {
        if(m_pFFmpegWorker) {
            QMetaObject::invokeMethod(m_pFFmpegWorker,"stopRecordVideo",Qt::QueuedConnection);
        }
    }
}

void VideoProcessWidget::sltRelayVideoClicked()
{
    if(ui->relayBtn->isChecked()){
        if(m_pFFmpegWorker) {
            QString path = getParaValue("Video","outputUrl").toString();
            if (path.isEmpty()) {
                return;
            }
            QMetaObject::invokeMethod(m_pFFmpegWorker,"startRelayVideo",Qt::QueuedConnection,Q_ARG(QString,path));
        }
    }else {
        if(m_pFFmpegWorker) {
            QMetaObject::invokeMethod(m_pFFmpegWorker,"stopRelayVideo",Qt::QueuedConnection);
        }
    }
}

void VideoProcessWidget::sltScreenPushBtnClicked()
{
    if(ui->ScreenPushBtn->isChecked()){
        m_pScreenPusher->start();
    }else {
        m_pScreenPusher->stop();
    }
}

void VideoProcessWidget::sltChangeBtnClicked()
{
    QString str1 = QString("rtsp://192.168.0.107:554/live/stream");
    QString str2 = QString("rtmp://192.168.0.107:1935/live/stream");
    QStringList strList;
    strList.append(str1);
    strList.append(str2);
    if(testIndex>1){
        testIndex =0;
    }
    qDebug()<<"strList[n]:"<<strList[testIndex];
    sltReSetPullStream(strList[testIndex]);
    testIndex++;
}

void VideoProcessWidget::sltDoReconect()
{
    if(m_workerState !=State_Reconnecting){
        return;
    }
    sltStartPlayClicked();
    //恢复视频上报以及推流功能
    if(m_recoverFunTimer->isActive()){
        m_recoverFunTimer->stop();
    }
    m_recoverFunTimer->start();
}

void VideoProcessWidget::sltVideoStatusChanged(VideoStatus status, const QString &message)
{
    // 记录日志
    FrameLogManager::getInstance()->log(
                QString("VideoProcessFFmpeg"),
                QString("status[%1]: %2").arg(status).arg(message),
                IFrameLogManager::DEBUG_TYPE);
    // 向参数设置发送状态
    m_currentStatus = status;
    m_extraMsg = message;
    emit signalPullVideoStatus(status, message);

    switch (status) {
    case Status_Connecting:
        // 正在拉流
        break;
    case Status_Playing:
        // 播放成功：取消重连定时器
        if (m_reconnectTimer) m_reconnectTimer->stop();
        m_workerState = State_Playing;
        ui->label->setUpdatesEnabled(false);
        break;
    case Status_Reconnecting:
        // 断线：启动重连倒计时
        ui->label->setUpdatesEnabled(true);
        scheduleReconnect(2000);
        break;
    case Status_Failed:
        // 初始化失败：稍后重连（服务端可能还未推流）
        ui->label->setUpdatesEnabled(true);
        scheduleReconnect(2000);
        break;
    case Status_Stopped:
        ui->label->setUpdatesEnabled(true);
        m_workerState = State_Idle;
        break;
    default:
        break;
    }
}

void VideoProcessWidget::sltVideoStatusTimerout()
{
    emit signalPullVideoStatus(m_currentStatus,m_extraMsg); // 定时发送视频状态
}

void VideoProcessWidget::sltLowDiskSpace()
{
    QString log = QString::fromUtf8(u8"存储剩余空间不足%1MB，请及时清理").arg(g_freeSpaceThreshold);
    emit signalHintMessage(QString::fromUtf8(u8"内存告警"),log,MessageType_Warn);
}

void VideoProcessWidget::sltRecoverFunc()
{   // 确定拉到流状态后再恢复录像以及上报等功能
    if(m_currentStatus == Status_Playing){
        //恢复视频上报以及推流功能
        if(m_recordHandFlag){
            ui->recordBtn->setChecked(true);
            sltRecordVideoClicked();
        }
        if(m_relayHandFlag){
            QString path = getParaValue("Video","outputUrl").toString();
            if (path.isEmpty()) {
                return;
            }
            QMetaObject::invokeMethod(m_pFFmpegWorker,"startRelayVideo",Qt::QueuedConnection,Q_ARG(QString,path));
        }
    } else {
        m_recoverFunTimer->start();
    }
}

void VideoProcessWidget::sltUpdateScreenPushStyle(int state)
{
    pluginmsg pmsg;
    memset(&pmsg, 0, sizeof(pmsg));
    pmsg.pluginid =  m_pPlugin->getId();
    pmsg.id       = Command_ScreenPush;
    if (state == Status_Reconnecting) {
        pmsg.state = 2; //1 关 2开
    } else if (state == Status_Playing) {
        pmsg.state = 2; //1 关 2开
    } else {
        pmsg.state = 1; //1 关 2开
    }
    emit signalPluginMsg(pmsg);
}

void VideoProcessWidget::scheduleReconnect(int delayMs)
{
    if(m_workerState == State_Reconnecting){
        m_reconnectTimer->start(delayMs);
        return;
    }
    m_workerState = State_Reconnecting;
    // 不在此处 emit stopPlay：若与 switchPlay 并发，队列里残留的 stop 会在
    // 新流 start 之后执行并杀死新会话。重连由 sltDoReconect → startPlay
    // 内部 if(initFlag) stopPlay() 完成清理即可。
    m_reconnectTimer->start(delayMs);
}

QVariant VideoProcessWidget::getParaValue(QString groupName, QString paraKey)
{
    m_pSettings.reset(new QSettings(QCoreApplication::applicationDirPath() + "/config/VideoProcess.ini", QSettings::IniFormat));
    m_pSettings->setIniCodec(QTextCodec::codecForName("UTF-8"));
    QString name_key = groupName + "/" + paraKey;
    if(!m_pSettings->contains(name_key)){
        return QVariant("Null");
    }
    return m_pSettings->value(name_key);
}

void VideoProcessWidget::recoverFunc()
{
}
