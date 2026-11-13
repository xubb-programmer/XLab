#include "httpworker.h"
#include <QDebug>
#include <QNetworkReply>
#include <QJsonParseError>
#include <QEventLoop>
#include <QString>
#include "FrameLogManager.h"

HttpWorker::HttpWorker(QObject *parent) : QObject(parent)
{
    m_manager = new QNetworkAccessManager(this);
    connect(m_manager,&QNetworkAccessManager::finished,this,&HttpWorker::onReplyFinished);
}

HttpWorker::~HttpWorker()
{
    qDebug() << "HttpWorker destroyed";
}

// 执行HTTP-Get请求
void HttpWorker::doGetWork(const QString &url)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
    m_manager->get(request);
}

// 处理请求响应
void HttpWorker::onReplyFinished(QNetworkReply *reply)
{
    if(reply->error() != QNetworkReply::NoError)
    {
        reply->deleteLater();
        return;
    }

    // 获取HTTP状态码
    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    // 获取响应数据
    QString responseData = reply->readAll();

    //发送请求响应
    emit requestFinishedForDeviceStatus(responseData,statusCode);

    reply->deleteLater();
}
