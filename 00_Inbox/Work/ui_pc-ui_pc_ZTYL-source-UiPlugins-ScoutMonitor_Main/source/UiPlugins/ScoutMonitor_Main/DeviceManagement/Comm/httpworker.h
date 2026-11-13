#ifndef HTTPWORKER_H
#define HTTPWORKER_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QByteArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QThread>

class HttpWorker : public QObject
{
    Q_OBJECT
public:
    explicit HttpWorker(QObject *parent = nullptr);
    ~HttpWorker();

signals:
    // 发送请求结果信号
    void requestFinished(const QJsonObject &result);

    // 发送设备状态查询请求结果
    void requestFinishedForDeviceStatus(QString strData,int statusCode);
    
    // 发送错误信息信号
    void requestError(const QString &errorString);
    
    // 通知线程开始工作
    void startWork();

public slots:
    // 执行HTTP-Get请求
    void doGetWork(const QString &url);

    // 处理请求响应
    void onReplyFinished(QNetworkReply *reply);

private:
    QNetworkAccessManager *m_manager;
};

#endif // HTTPWORKER_H
