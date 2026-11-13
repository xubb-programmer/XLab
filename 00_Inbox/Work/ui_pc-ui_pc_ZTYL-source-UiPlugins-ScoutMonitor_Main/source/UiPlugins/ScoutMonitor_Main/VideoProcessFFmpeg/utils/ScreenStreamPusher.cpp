/*
* File：ScreenStreamPusher.cpp
* Description：屏幕采集推流控制类实现
*/
#include "ScreenStreamPusher.h"
#include "ScreenPushInstance.h"
#ifdef MY_PLUGIN
#include "FrameLogManager.h"
#endif
#include <QApplication>
#include <QScreen>
#include <QHostAddress>
#include <QSettings>
#include <QTextCodec>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#ifdef Q_OS_UNIX
#include <signal.h>
#else
#include <Windows.h>
#endif

static const struct Rule { const char *kw; QString msg; } s_rules[] = {
{"406 Not Acceptable",          QString::fromUtf8(u8"服务器拒绝推流参数\n 可能原因:名称冲突")},
{"Server error: Already publishing",QString::fromUtf8(u8"服务器拒绝推流参数\n 可能原因:名称冲突")},
{"401 Unauthorized",            QString::fromUtf8(u8"推流认证失败，请确定用户名和密码")},
{"404 Not Found",               QString::fromUtf8(u8"请确认推流URL是否正确")},
{"method ANNOUNCE failed",      QString::fromUtf8(u8"服务器拒绝建立推流会话，可能原因:服务器不存在")},
{"Error number -138 occurred",  QString::fromUtf8(u8"无法连接到服务器，请检查网络")},
{"Error number -10053 occurred",QString::fromUtf8(u8"无法连接到服务器，请检查网络")},
{"Connection refused",          QString::fromUtf8(u8"服务器拒绝连接，请检查网络或服务状态")},
{"Connection timed out",        QString::fromUtf8(u8"无法连接到服务器，请检查网络")},
{"Network is unreachable",      QString::fromUtf8(u8"无法连接到服务器，请检查网络")},
{"Connection reset",            QString::fromUtf8(u8"无法连接到服务器，请检查网络")},
{"Broken pipe",                 QString::fromUtf8(u8"无法连接到服务器，请检查网络")},
{"Conversion failed",           QString::fromUtf8(u8"无法连接到服务器，请检查网络")},
{"Unknown encoder",             QString::fromUtf8(u8"未知的编码器")},
{"Input/output error",          QString::fromUtf8(u8"采集输入/输出错误")},
{"Couldn't get cursor info",    QString::fromUtf8(u8"无法获取光标信息")},
{"RTMP send error",             QString::fromUtf8(u8"无法连接到服务器，请检查网络")},
{"failed to connect socket",    QString::fromUtf8(u8"无法连接到服务器，请检查网络")},
{"Unknown error occurred",      QString::fromUtf8(u8"无法连接到服务器，请检查网络")},
{"Server returned 4XX Client Error",QString::fromUtf8(u8"服务器拒绝推流参数\n 可能原因:名称冲突")},
{"failed to read RTMP packet header",QString::fromUtf8(u8"服务器拒绝推流参数\n 可能原因:名称冲突")}
};

ScreenStreamPusher::ScreenStreamPusher(QObject *parent)
    : QObject(parent)
{
    m_sysVer = QSysInfo::windowsVersion();
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &ScreenStreamPusher::onReadyReadOutput);
    connect(m_process, &QProcess::errorOccurred,
            this, &ScreenStreamPusher::onProcessError);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &ScreenStreamPusher::onProcessFinished);

    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    m_reconnectTimer->setInterval(5000);
    connect(m_reconnectTimer, &QTimer::timeout,
            this, &ScreenStreamPusher::onReconnectTimeout);
}

ScreenStreamPusher::~ScreenStreamPusher()
{
    stop();
}

bool ScreenStreamPusher::isActive() const
{
    return m_active;
}

void ScreenStreamPusher::setActive(bool active)
{
    m_active = active;
}

QString ScreenStreamPusher::getOutputUrl()const
{
    return m_outputUrl;
}

int ScreenStreamPusher::start()
{
    int ret = -1;
    m_stopping = false;

    QSettings screenParas(QApplication::applicationDirPath() + "/config/communication.ini",
                          QSettings::IniFormat);
    screenParas.setIniCodec(QTextCodec::codecForName("UTF-8"));

    QString outputIP = ScreenPushInstance::pushTargetIp(screenParas.value("ScreenPush/targetDevice").toString());
    QString outputProtl = screenParas.value("ScreenPush/protocol").toString();
    QString streamName  = screenParas.value("ScreenPush/streamName").toString();
    if (streamName.isEmpty())
        streamName = QString("stream");

    QString streamFormat = QString("mpegts");

    if (outputIP.isEmpty()) {
        QString errLog = QString::fromUtf8(u8"未获取到设备IP，请在桌面推流设置里选择目标设备，或在其他设置里配置设备地址");
#ifdef MY_PLUGIN
        FrameLogManager::getInstance()->log(QString("VideoProcessFFmpeg"),
                                            QString("error:%1").arg(errLog),
                                            IFrameLogManager::DEBUG_TYPE);
#endif
        emit signalHintMessage(QString::fromUtf8(u8"投屏错误"), errLog, MessageType_Err);
        emit signalUpdateScreenPushStyle(Status_Failed);
        m_active = false;
        return ret;
    }

    if (outputProtl == "rtsp") {
        m_outputUrl    = "rtsp://" + outputIP + ":554/screen/" + streamName;
        streamFormat = QString("rtsp");
    } else if (outputProtl == "rtmp") {
        m_outputUrl    = "rtmp://" + outputIP + ":1935/screen/" + streamName;
        streamFormat = QString("flv");
    } else {
        QString errLog = QString::fromUtf8(u8"不支持的推流协议%1").arg(outputProtl);
#ifdef MY_PLUGIN
        FrameLogManager::getInstance()->log(QString("VideoProcessFFmpeg"),
                                            QString("error:%1").arg(errLog),
                                            IFrameLogManager::DEBUG_TYPE);
#endif
        emit signalHintMessage(QString::fromUtf8(u8"投屏错误"), errLog, MessageType_Err);
        emit signalUpdateScreenPushStyle(Status_Failed);
        m_active = false;
        return ret;
    }

    const CaptureRegion region = ScreenPushInstance::captureRegion(
                ScreenPushInstance::captureScreens(), ScreenPushInstance::readCaptureConfig(screenParas));
    QString captureWidth  = QString::number(region.width);
    QString captureHeight = QString::number(region.height);
    QString captureFps    = screenParas.value("ScreenPush/captureFps").toString();
    QString captureX      = QString::number(region.x);
    QString captureY      = QString::number(region.y);

    int clampedFps = qBound(15, captureFps.toInt(), 100);
    captureFps = QString::number(clampedFps);

    if (m_process->state() == QProcess::Running)
        stop();
    m_stopping = false;

#ifdef Q_OS_WIN32
    static HANDLE hJob = CreateJobObject(NULL, NULL);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = {0};
    jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
    QString args = QApplication::applicationDirPath() + "/config/ScreenPush.bat";
#else
    QString args = QApplication::applicationDirPath() + "/config/ScreenPush.sh";
#endif

    QStringList arguments;
    arguments << QApplication::applicationDirPath();
    arguments << captureWidth;
    arguments << captureHeight;
    arguments << captureX;
    arguments << captureY;
    arguments << captureFps;
    arguments << streamFormat;
    arguments << m_outputUrl;

#ifdef Q_OS_WIN
    m_process->start(args, arguments);
#else
    m_process->start("setsid", QStringList() << args << arguments);
#endif

    if (!m_process->waitForStarted()) {
#ifdef MY_PLUGIN
        FrameLogManager::getInstance()->log(QString("VideoProcessFFmpeg"),
                                            QString("error:%1").arg(m_process->errorString()),
                                            IFrameLogManager::DEBUG_TYPE);
#endif
        QString errLog = QString::fromUtf8(u8"采集屏幕推流失败");
        emit signalHintMessage(QString::fromUtf8(u8"投屏错误"), errLog, MessageType_Err);
        emit signalUpdateScreenPushStyle(Status_Failed);
        m_active = false;
        ret = -1;
    } else {
        ret = 0;
        m_active = true;
        emit signalUpdateScreenPushStyle(Status_Playing);
#ifdef Q_OS_WIN32
        AssignProcessToJobObject(hJob,
                                 OpenProcess(PROCESS_ALL_ACCESS, FALSE, m_process->processId()));
#endif
    }
    return ret;
}

int ScreenStreamPusher::stop()
{
    m_stopping = true;
    if (m_reconnectTimer)
        m_reconnectTimer->stop();
    m_outputUrl = "";
    m_active = false;
#ifdef Q_OS_WIN
    if (m_process->state() == QProcess::Running) {
        m_process->write("q\n");
        m_process->close();
    }
#else
    if (m_process && m_process->state() == QProcess::Running) {
        qint64 pid = m_process->processId();
        if (pid > 0)
            kill(-pid, SIGINT);
        m_process->kill();
        m_process->waitForFinished(2000);
    }
#endif
    return 0;
}

void ScreenStreamPusher::onReadyReadOutput()
{
    QByteArray out = m_process->readAllStandardOutput();
    qDebug()<<"out:"<<out;
    int ruleCounts = static_cast<int>(sizeof(s_rules) / sizeof(s_rules[0]));
    for (int i = 0; i < ruleCounts; ++i) {
        if (QString(out).contains(QString(s_rules[i].kw), Qt::CaseInsensitive)) {
            processScreenStreamError(s_rules[i].kw, s_rules[i].msg);
            return;
        }
    }

    if (!checkFFmpegError(out))
        return;

    processScreenStreamError("unknown error", QString(out).right(64));
}

bool ScreenStreamPusher::checkFFmpegError(const QByteArray &out)
{
    QString outStr = QString::fromLocal8Bit(out).toLower();
    foreach (const QString &kw, ERROR_KEYWORDS) {
        if (outStr.contains(kw, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

bool ScreenStreamPusher::isNetworkError(const QString &kw, const QString &msg) const
{
    static const QStringList networkKeywords = {
        "error number -138 occurred",
        "error number -10053 occurred",
        "rtmp send error",
        "failed to connect socket",
        "connection refused",
        "connection timed out",
        "network is unreachable",
        "connection reset",
        "broken pipe",
        "i/o error",
        "input/output error",
        "conversion failed"
    };

    QString text = kw + " " + msg;
    text = text.toLower();
    foreach (const QString &networkKeyword, networkKeywords) {
        if (text.contains(networkKeyword, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

void ScreenStreamPusher::processScreenStreamError(const char *kw, QString msg)
{
    QString errLog = QString::fromUtf8(u8"采集屏幕推流失败,%1").arg(msg);
#ifdef MY_PLUGIN
    FrameLogManager::getInstance()->log(QString("VideoProcessFFmpeg"),
                                        QString("error:%1").arg(errLog),
                                        IFrameLogManager::DEBUG_TYPE);
#endif
    if (kw == nullptr || (strcmp(kw, "Unknown error occurred") == 0))
        return;

    if (((strcmp(kw, "Input/output error") == 0) || (strcmp(kw, "Couldn't get cursor info") == 0))
            && (m_sysVer == QSysInfo::WV_WINDOWS7)) {
        return;
    }

    if (isNetworkError(QString::fromLocal8Bit(kw), msg) && m_active) {
        emit signalHintMessage(QString::fromUtf8(u8"投屏错误"),
                               errLog + QString::fromUtf8(u8"，正常尝试重新连接。。。"),
                               MessageType_Warn);
        emit signalUpdateScreenPushStyle(Status_Reconnecting);
        m_stopping = true;
        killPushProcess();
        if (m_reconnectTimer && !m_reconnectTimer->isActive())
            m_reconnectTimer->start();
        return;
    }

    killPushProcess();

    emit signalHintMessage(QString::fromUtf8(u8"投屏错误"), errLog, MessageType_Err);

    emit signalUpdateScreenPushStyle(Status_Failed); // 关闭采集屏幕菜单按钮样式
    return;
}

void ScreenStreamPusher::onProcessError(QProcess::ProcessError error)
{
    if (!m_active || m_stopping)
        return;

    QString msg = m_process ? m_process->errorString() : QString::fromLocal8Bit("推流进程异常");
    if (error == QProcess::Crashed) {
        processScreenStreamError("process crashed", msg);
    } else {
        processScreenStreamError("process error", msg);
    }
}

void ScreenStreamPusher::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (!m_active || m_stopping)
        return;
#ifdef Q_OS_UNIX
    if (exitStatus == QProcess::CrashExit) {
        processScreenStreamError("process crashed", QString::fromLocal8Bit("推流进程异常退出"));
    } else if (exitCode != 0) {
        processScreenStreamError("process finished",
                                 QString::fromLocal8Bit("推流进程异常退出，退出码:%1").arg(exitCode));
    }
#endif
}

void ScreenStreamPusher::onReconnectTimeout()
{
    if (m_stopping)
        m_stopping = false;

    if (!m_active)
        return;

    start();
}

void ScreenStreamPusher::killPushProcess()
{
    if (!m_process)
        return;

#ifdef Q_OS_WIN32
    qint64 pid = m_process->processId();
    if (pid > 0) {
        QString cmd = QString("taskkill /F /T /PID %1").arg(QString::number(pid));
        QProcess::execute(cmd);
    }
#endif
    if (m_process->state() == QProcess::Running)
        m_process->kill();
}

QString ScreenStreamPusher::extractIP(const QString &value)
{
    QRegularExpression ipRegex(R"((\d{1,3}\.){3}\d{1,3})");
    QRegularExpressionMatch match = ipRegex.match(value);
    if (match.hasMatch()) {
        QHostAddress addr(match.captured(0));
        return addr.toString();
    }
    return QString();
}
