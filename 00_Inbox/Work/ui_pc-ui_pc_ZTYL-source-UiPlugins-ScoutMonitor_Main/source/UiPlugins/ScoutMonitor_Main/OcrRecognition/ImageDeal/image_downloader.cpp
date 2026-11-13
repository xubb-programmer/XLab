#include "image_downloader.h"
#include <QDir>
#include <QMessageBox>
#include <QDebug>
#include <QJsonDocument>

ImageDownloader::ImageDownloader(QObject *parent) : QObject(parent)
{
    m_networkManager = new QNetworkAccessManager(this);
    m_file = nullptr;
}

void ImageDownloader::downloadImage(const QUrl &imageUrl,  const QJsonObject &Json, const QString &savePath)
{
    // 检查URL是否有效
    if (!imageUrl.isValid()) {
        emit downloadFailed(QString::fromLocal8Bit("无效的URL"));
        return;
    }


    // 检查保存路径
    QFileInfo fileInfo(savePath);
    QDir dir(fileInfo.path());
    if (!dir.exists()) {
        // 如果目录不存在，尝试创建
        if (!dir.mkpath(fileInfo.path())) {
            emit downloadFailed(QString::fromLocal8Bit("无法创建保存目录: ") + fileInfo.path());
            return;
        }
    }


    // 准备文件
    if (m_file) {
        delete m_file;
        m_file = nullptr;
    }
    
    m_file = new QFile(savePath);
    if (!m_file->open(QIODevice::WriteOnly)) {
        QString error = QString::fromLocal8Bit("无法打开文件进行写入!");
        delete m_file;
        m_file = nullptr;
        emit downloadFailed(error);
        return;
    }
    
    m_savePath = savePath;

    // 发送GET请求
    QNetworkRequest request(imageUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
    QJsonDocument jsonDoc(Json);
    QNetworkReply *reply = m_networkManager->post(request,jsonDoc.toJson());
    
    // 连接信号槽
    connect(reply, &QNetworkReply::finished, 
            this, [this, reply]() { onReplyFinished(reply); });
    connect(reply, &QNetworkReply::downloadProgress,
            this, &ImageDownloader::onDownloadProgress);
}

void ImageDownloader::onReplyFinished(QNetworkReply *reply)
{
    if (reply->error() == QNetworkReply::NoError) {
        // 将接收到的数据写入文件
        if (m_file) {
            m_file->write(reply->readAll());
            m_file->close();
            delete m_file;
            m_file = nullptr;
            emit downloadCompleted(m_savePath);
        }
    } else {

        emit downloadFailed(QString::fromLocal8Bit("服务未响应，下载失败！"));

        if (m_file) {
            m_file->close();
            m_file->remove(); // 删除不完整的文件
            delete m_file;
            m_file = nullptr;
        }
    }
    
    reply->deleteLater();
}

void ImageDownloader::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal)
{
    emit downloadProgress(bytesReceived, bytesTotal);
}
