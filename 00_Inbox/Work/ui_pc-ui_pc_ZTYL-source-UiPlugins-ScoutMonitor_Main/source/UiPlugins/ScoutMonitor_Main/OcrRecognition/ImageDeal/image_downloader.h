#ifndef IMAGE_DOWNLOADER_H
#define IMAGE_DOWNLOADER_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QFile>
#include <QUrl>

class ImageDownloader : public QObject
{
    Q_OBJECT
public:
    explicit ImageDownloader(QObject *parent = nullptr);
     
    /**
     * @brief downloadImage
     * 下载图片并保存到指定路径
     * @param imageUrl(下载地址) savePath(图片存储路径)
     * @return void
     */
    void downloadImage(const QUrl &imageUrl,  const QJsonObject &Json,const QString &savePath);

signals:
    // 下载完成信号
    void downloadCompleted(const QString &filePath);
    // 下载失败信号
    void downloadFailed(const QString &errorString);
    // 下载进度信号
    void downloadProgress(qint64 bytesReceived, qint64 bytesTotal);

private slots:
    /**
     * @brief onReplyFinished
     * 处理下载响应
     * @param reply(下载响应 reply->error():下载成功 reply->errorString():失败原因)
     * @return void
     */
    void onReplyFinished(QNetworkReply *reply);

    /**
     * @brief onDownloadProgress
     * 处理下载进度
     * @param bytesReceived(当前已接收的字节数) bytesTotal(总字节数)
     * @return void
     */
    void onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);

private:
    QNetworkAccessManager *m_networkManager;
    QFile *m_file;
    QString m_savePath;
};

#endif // IMAGE_DOWNLOADER_H
