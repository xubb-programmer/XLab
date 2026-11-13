#ifndef DISKSPACECHECKER_H
#define DISKSPACECHECKER_H

#include <QObject>
#include <QTimer>
#include <QString>
#include <QStorageInfo>


class DiskSpaceChecker:public QObject
{
    Q_OBJECT
public:

    static DiskSpaceChecker &getInstance()
    {
        static DiskSpaceChecker s_instance;
        return s_instance;
    }

    // 禁止拷贝和赋值
    DiskSpaceChecker(const DiskSpaceChecker &) = delete;
    DiskSpaceChecker &operator=(const DiskSpaceChecker &)= delete;

    // 设置录像存储路径，并启动检查
    void setRecordPath(const QString &path);
    void startChecking();//默认60s检查一次
    void stopChecking();

    // 立即检查一次，返回剩余空间百分比（0~1）
    qint64 checkNow();

signals:
    void signalLowDiskSpace();
    //void signalLowDiskSpace(const QString &path,double usedPercent,double freePercent,qint64 freeBytes,qint64 totalBytes);

private slots:
    void onTimer();
private:
    explicit DiskSpaceChecker(QObject *parent = nullptr);
    ~DiskSpaceChecker() = default;

    bool getSpaceInfo(const QString &path,qint64 &freeBytes,qint64 &totalBytes);
    QStorageInfo getRootPath(const QString& absPath);

    QTimer m_timer;
    QString m_recordPath;
    float m_checkTimer = 5;
};

#endif // DISKSPACECHECKER_H
