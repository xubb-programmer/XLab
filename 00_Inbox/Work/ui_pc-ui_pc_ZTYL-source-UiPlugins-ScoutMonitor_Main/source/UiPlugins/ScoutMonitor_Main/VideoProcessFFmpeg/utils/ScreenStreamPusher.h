/*
* File：ScreenStreamPusher.h
* Description：屏幕采集推流控制类，负责启动/停止屏幕推流子进程及其错误处理
*/
#ifndef SCREENSTREAMPUSHER_H
#define SCREENSTREAMPUSHER_H

#include <QObject>
#include <QProcess>
#include <QSysInfo>
#include <QTimer>
#include "utils/FFmpegWorkerCommon.h"

class ScreenStreamPusher : public QObject
{
    Q_OBJECT
public:
    explicit ScreenStreamPusher(QObject *parent = nullptr);
    ~ScreenStreamPusher();

    /**
     * @brief start 启动屏幕采集推流子进程
     * @return 0 成功，< 0 失败
     */
    int start();

    /**
     * @brief stop 停止屏幕采集推流子进程
     * @return 0 成功
     */
    int stop();

    bool isActive() const;
    void setActive(bool active);

    QString getOutputUrl() const;

signals:
    // 发送弹窗提示
    void signalHintMessage(QString title, QString msg, MessageType type);
   // 更新采集屏幕的按钮样式
    void signalUpdateScreenPushStyle(int state);

private slots:
    void onReadyReadOutput();
    void onProcessError(QProcess::ProcessError error);
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onReconnectTimeout();

private:
    bool    checkFFmpegError(const QByteArray &out);
    bool    isNetworkError(const QString &kw, const QString &msg) const;
    void    processScreenStreamError(const char *kw, QString msg);
    void    killPushProcess();
    QString extractIP(const QString &value);

    QProcess             *m_process  = nullptr;
    QTimer               *m_reconnectTimer = nullptr;
    bool                  m_active   = false;
    bool                  m_stopping  = false;
    QString               m_outputUrl = "";
    QSysInfo::WinVersion  m_sysVer;
};

#endif // SCREENSTREAMPUSHER_H
