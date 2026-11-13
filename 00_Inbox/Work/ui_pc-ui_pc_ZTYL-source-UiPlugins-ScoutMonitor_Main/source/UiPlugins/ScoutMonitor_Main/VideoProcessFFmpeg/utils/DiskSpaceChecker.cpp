#include "DiskSpaceChecker.h"
#include <QDir>
#include <QTimer>
#include <QDebug>
#include <QSettings>
#include <QApplication>
#include <QTextCodec>
extern uint g_freeSpaceThreshold;
static QString formatSize(qint64 bytes)
{
    const QStringList units = {"B","KB","MB","GB","TB"};
    double size = static_cast<double>(bytes);
    int i=0;
    while (size >=1024.0 && i < units.size() -1){
        size /= 1024.0;
        ++i;
    }
    return QString("%1%2").arg(size,0,'f',2).arg(units[i]);
}


void DiskSpaceChecker::setRecordPath(const QString &path)
{
    m_recordPath = path;
}

void DiskSpaceChecker::startChecking()
{
    m_timer.start(m_checkTimer*1000*60);//单位min
}

void DiskSpaceChecker::stopChecking()
{
    m_timer.stop();
}

qint64 DiskSpaceChecker::checkNow()
{
    qint64 freeBytes=0;
    qint64 totalBytes = 0;

    if(!getSpaceInfo(m_recordPath,freeBytes,totalBytes) || totalBytes == 0)
    {
        return -1.0;
    }
    return freeBytes;
}

void DiskSpaceChecker::onTimer()
{
    qint64 freeBytes=0;
    qint64 totalBytes = 0;
    QSettings storageParas(QApplication::applicationDirPath()+"/config"+"/VideoProcess.ini" , QSettings::IniFormat);
    storageParas.setIniCodec(QTextCodec::codecForName("UTF-8"));
    m_recordPath = storageParas.value("Record/path").toString();
    if(m_recordPath.isEmpty()){
        m_recordPath = QApplication::applicationDirPath()+"/Video/";
    }
    if(!getSpaceInfo(m_recordPath,freeBytes,totalBytes) || totalBytes == 0)
    {
        return;
    }
    const double freePercent = static_cast<double>(freeBytes) / static_cast<double>(totalBytes);
    const double usedPercent = 1.0 - freePercent;

    qint64 threshold = static_cast<qint64>(g_freeSpaceThreshold * 1024LL * 1024LL);
     if(freeBytes < threshold){
        emit signalLowDiskSpace();
    }
}

DiskSpaceChecker::DiskSpaceChecker(QObject *parent)
    :QObject(parent)
{
    QSettings storageParas(QApplication::applicationDirPath()+"/config"+"/VideoProcess.ini" , QSettings::IniFormat);
    storageParas.setIniCodec(QTextCodec::codecForName("UTF-8"));
    g_freeSpaceThreshold = storageParas.value("Storage/freeSpaceThreshold").toUInt();
    m_checkTimer = storageParas.value("Storage/checkTimer").toFloat();
    m_recordPath = storageParas.value("Record/path").toString();
    connect(&m_timer,&QTimer::timeout,this,&DiskSpaceChecker::onTimer);
    if (m_checkTimer == 0){
        m_checkTimer = 1;
    }
}

bool DiskSpaceChecker::getSpaceInfo(const QString &path, qint64 &freeBytes, qint64 &totalBytes)
{
    QStorageInfo storage= getRootPath(path);
    if(!storage.isValid() || !storage.isReady())
    {
        return false;
    }
    freeBytes = storage.bytesAvailable();
    totalBytes = storage.bytesTotal();
    return totalBytes > 0;
}

QStorageInfo DiskSpaceChecker::getRootPath(const QString &absPath)
{
    QString checkPath = absPath;
    while (!QDir(checkPath).exists()){
        checkPath = QFileInfo(checkPath).path();
        if(checkPath == "/" || checkPath.isEmpty()){
            break;
        }
    }
    return QStorageInfo(checkPath);
}
