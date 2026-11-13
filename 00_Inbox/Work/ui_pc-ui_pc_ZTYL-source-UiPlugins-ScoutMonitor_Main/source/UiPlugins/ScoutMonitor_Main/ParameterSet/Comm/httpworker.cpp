#include "httpworker.h"
#include <QDebug>
#include <QNetworkReply>
#include <QPointer>
#include <QTimer>
#include <atomic>
#include <memory>
#include <QJsonParseError>
#include <QEventLoop>
#include <QString>
#include <QMessageBox>
#include "FrameLogManager.h"

HttpWorker::HttpWorker(QObject *parent) : QObject(parent)
{
    m_netManager = new QNetworkAccessManager(this);
//    connect(m_netManager, &QNetworkAccessManager::finished,
//            this, &HttpWorker::onReplyFinished);

}

HttpWorker::~HttpWorker()
{
    m_sseClient.disconnectFromServer();
    qDebug() << "HttpWorker destroyed";
    FrameLogManager::getInstance()->log("HttpWorker",QString::fromLocal8Bit("关闭SSE服务器URL:") ,IFrameLogManager::INFO_TYPE);
}

void HttpWorker::setsseClient(const QString &url)
{
    m_url = QUrl(url);

    m_sseClient.disconnectFromServer();
    // 设置SSE服务器URL（替换为实际的SSE服务器地址）  ZLM服务器推流列表
    QString strUrl = "http://" + url +"/proxy/zlmpushstreamlist/stream";
    m_sseClient.setUrl(strUrl);

    m_sseClient.setReconnectInterval(4000);
    // 流建立后无数据则判定断线（覆盖服务端进程退出但 TCP 半开）
    m_sseClient.setStreamIdleTimeout(15000);

    // 连接信号槽（UniqueConnection 避免重复连接）
    QObject::connect(&m_sseClient, &SSEClient::eventReceived, this, &HttpWorker::requestFinishedForgetZLMpushList, Qt::UniqueConnection);
    QObject::connect(&m_sseClient, &SSEClient::connectionStatusChanged, this, &HttpWorker::slotlSSELinkStatus, Qt::UniqueConnection);
    QObject::connect(&m_sseClient, &SSEClient::errorOccurred, this, &HttpWorker::onSseErrorOccurred, Qt::UniqueConnection);

    // 连接到SSE服务器
    m_sseClient.connectToServer();
}

void HttpWorker::setParams(const QJsonObject &params)
{
    m_params = params;
}

void HttpWorker::onReplyFinished()
{
    //断开SSE链接  自动释放资源
    if(m_sseReply){
        //终止长连接
        m_sseReply->abort();
        //释放m_sseReply
        m_sseReply->deleteLater();
        m_sseReply=nullptr;
    }
}


// 执行HTTP-Pos请求 发送拉流设置
void HttpWorker::doPospullStreamParaWork(const QString &url, const QJsonObject &Json, const VideoSetData &pendingContext, int reqKind)
{
    QNetworkRequest request(url);  
    request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    request.setTransferTimeout(45000);
#endif
    QJsonDocument jsonDoc(Json);
    QNetworkReply *reply = m_netManager->post(request, jsonDoc.toJson());
    FrameLogManager::getInstance()->log("HttpWorker","doPospullStreamParaWork:" + jsonDoc.toJson(),IFrameLogManager::DEBUG_TYPE);
    const VideoSetData ctxCopy = pendingContext;
    const int kindCopy = reqKind;

    static const QString kTimeoutPayload = QString::fromUtf8(u8"{\"code\":-1,\"message\":\"请求超时或网络异常，未收到有效响应\"}");
    static const QString kNoReplyPayload = QString::fromUtf8(u8"{\"code\":-1,\"message\":\"无法创建网络请求\"}");

    if (!reply) {
        FrameLogManager::getInstance()->log("HttpWorker", QString::fromLocal8Bit("doPospullStreamParaWork: QNetworkAccessManager::post 返回空"),
                                              IFrameLogManager::DEBUG_TYPE);
        emit requestFinishedForPullStreamPara(kNoReplyPayload, ctxCopy, kindCopy);
        return;
    }

    QPointer<QNetworkReply> replyPtr(reply);
    auto done = std::make_shared<std::atomic<int>>(0);

    auto finishOnce = [=](const QString &payload, bool fromFinishedSlot) {
        int expected = 0;
        if (!done->compare_exchange_strong(expected, 1))
            return;
        if (m_shuttingDown)
            return;
        emit requestFinishedForPullStreamPara(payload, ctxCopy, kindCopy);
        if (!replyPtr)
            return;
        if (fromFinishedSlot) {
            replyPtr->deleteLater();
        } else {
            replyPtr->disconnect();
            replyPtr->abort();
            replyPtr->deleteLater();
        }
    };

    connect(reply, &QNetworkReply::finished, this, [=]() {
        if (!replyPtr)
            return;
        const QString responseData = replyPtr->readAll();
        if (replyPtr->error() != QNetworkReply::NoError) {
            FrameLogManager::getInstance()->log(
                "HttpWorker",
                QString::fromLocal8Bit("doPospullStreamParaWork,发送拉流设置请求失败:") + replyPtr->errorString(),
                IFrameLogManager::DEBUG_TYPE);
        }
        finishOnce(responseData, true);
    });

    // 软超时：abort 促使绝大多数情况下进入 finished
    QTimer::singleShot(9000, this, [=]() {
        if (done->load(std::memory_order_acquire) != 0)
            return;
        if (replyPtr)
            replyPtr->abort();
    });

    // 硬兜底：个别平台/版本 abort 后仍长期收不到 finished，强制上报失败，避免界面卡在「下发中」
    QTimer::singleShot(12000, this, [=]() {
        if (done->load(std::memory_order_acquire) != 0)
            return;
        FrameLogManager::getInstance()->log(
            "HttpWorker",
            QString::fromLocal8Bit("doPospullStreamParaWork: 硬超时仍未收到 finished，强制结束本次请求"),
            IFrameLogManager::DEBUG_TYPE);
        finishOnce(kTimeoutPayload, false);
    });
}

// 执行HTTP-Pos请求,发送推流设置消息
void HttpWorker::doPosPushStreamParaWork(const QString &url, const QJsonObject &Json)
{  
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
    QJsonDocument jsonDoc(Json);
    QNetworkReply *reply = m_netManager->post(request,jsonDoc.toJson());
    FrameLogManager::getInstance()->log("HttpWorker","doPospullStreamParaWork:" + jsonDoc.toJson(),IFrameLogManager::DEBUG_TYPE);
    //异步处理响应（不阻塞）
    connect(reply,&QNetworkReply::finished,this,[=](){
        QString responseData;
        responseData = reply->readAll();
        emit requestFinishedForPushStreamPara(responseData);
        reply->deleteLater();
        if(reply->error()==QNetworkReply::NoError){
//            responseData = reply->readAll();
//            emit requestFinishedForPushStreamPara(responseData);
//            reply->deleteLater();
        }else{
            FrameLogManager::getInstance()->log("HttpWorker",QString::fromLocal8Bit("doPospullStreamParaWork,发送推流设置请求失败:") + reply->errorString(),IFrameLogManager::DEBUG_TYPE);
        }
    });

}

// 执行HTTP-Pos请求,发送OCR区域设置消息
void HttpWorker::doOcrpushStreamParaWork(const QString &url, const QJsonObject &Json)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
    QJsonDocument jsonDoc(Json);
    QNetworkReply *reply = m_netManager->post(request,jsonDoc.toJson());
    FrameLogManager::getInstance()->log("HttpWorker","doOcrpushStreamParaWork:" + jsonDoc.toJson(),IFrameLogManager::DEBUG_TYPE);
    //异步处理响应（不阻塞）
    connect(reply,&QNetworkReply::finished,this,[=](){
        QString responseData;
        responseData = reply->readAll();
        emit requestFinishedForOcrPushStreamPara(responseData);
        reply->deleteLater();
        if(reply->error()==QNetworkReply::NoError){
//            responseData = reply->readAll();
//            emit requestFinishedForOcrPushStreamPara(responseData);
//            reply->deleteLater();
        }else{
            FrameLogManager::getInstance()->log(
                        "HttpWorker",
                        QString::fromUtf8(u8"doOcrpushStreamParaWork,发送OCR区域设置请求失败:") + reply->errorString(),
                        IFrameLogManager::DEBUG_TYPE);
        }
    });

}

// 执行HTTP-Pos请求,发送ORC识别消息
void HttpWorker::doOCRSwitchWork(const QString &url, const QJsonObject &Json)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
    QJsonDocument jsonDoc(Json);
    QNetworkReply *reply = m_netManager->post(request,jsonDoc.toJson());
    FrameLogManager::getInstance()->log("HttpWorker","doOCRSwitchWork:" + jsonDoc.toJson(),IFrameLogManager::DEBUG_TYPE);
    //异步处理响应（不阻塞）
    connect(reply,&QNetworkReply::finished,this,[=](){
        QString responseData;
        responseData = reply->readAll();
        emit requestFinishedForOCRSwitch(responseData);
        reply->deleteLater();
        if(reply->error()==QNetworkReply::NoError){
//            responseData = reply->readAll();
//            emit requestFinishedForOCRSwitch(responseData);
//            reply->deleteLater();
        }else{
            FrameLogManager::getInstance()->log(
                        "HttpWorker",
                        QString::fromUtf8(u8"doOCRSwitchWork,发送ORC识别消息请求失败:") + reply->errorString(),
                        IFrameLogManager::DEBUG_TYPE);
        }
    });

}

// 执行HTTP-Pos请求,发送目标识别消息
void HttpWorker::doRecognitionSwitchWork(const QString &url, const QJsonObject &Json)
{  
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
    QJsonDocument jsonDoc(Json);
    QNetworkReply *reply = m_netManager->post(request,jsonDoc.toJson());
    FrameLogManager::getInstance()->log("HttpWorker","doRecognitionSwitchWork:" + jsonDoc.toJson(),IFrameLogManager::DEBUG_TYPE);
    //异步处理响应（不阻塞）
    connect(reply,&QNetworkReply::finished,this,[=](){
        QString responseData;
        responseData = reply->readAll();
        emit requestFinishedForRecognitionSwitch(responseData);
        reply->deleteLater();
        if(reply->error()==QNetworkReply::NoError){
//            responseData = reply->readAll();
//            emit requestFinishedForRecognitionSwitch(responseData);
//            reply->deleteLater();
        }else{
            FrameLogManager::getInstance()->log(
                        "HttpWorker",
                        QString::fromUtf8(u8"doRecognitionSwitchWork,发送目标识别消息请求失败:") + reply->errorString(),
                        IFrameLogManager::DEBUG_TYPE);
        }
    });
}

// 执行HTTP-GET请求,获取拉流列表
void HttpWorker::doGetpullStreamList(const QString &url)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    request.setTransferTimeout(45000);
#endif
    QNetworkReply *reply = m_netManager->get(request);
    FrameLogManager::getInstance()->log("HttpWorker","doGetpullStreamList:" + url,IFrameLogManager::DEBUG_TYPE);

    static const QString kTimeoutPayload = QString::fromUtf8(u8"{\"code\":-1,\"message\":\"查询拉流列表超时或网络异常\"}");
    static const QString kNoReplyPayload = QString::fromUtf8(u8"{\"code\":-1,\"message\":\"无法创建拉流列表查询请求\"}");

    if (!reply) {
        emit requestFinishedForgetpullStreamList(kNoReplyPayload);
        return;
    }

    QPointer<QNetworkReply> replyPtr(reply);
    auto done = std::make_shared<std::atomic<int>>(0);

    auto finishOnce = [=](const QString &payload, bool fromFinishedSlot) {
        int expected = 0;
        if (!done->compare_exchange_strong(expected, 1))
            return;
        if (m_shuttingDown)
            return;
        emit requestFinishedForgetpullStreamList(payload);
        if (!replyPtr)
            return;
        if (fromFinishedSlot) {
            replyPtr->deleteLater();
        } else {
            replyPtr->disconnect();
            replyPtr->abort();
            replyPtr->deleteLater();
        }
    };

    connect(reply, &QNetworkReply::finished, this, [=]() {
        if (!replyPtr)
            return;
        if (replyPtr->error() != QNetworkReply::NoError) {
            FrameLogManager::getInstance()->log(
                "HttpWorker",
                QString::fromLocal8Bit("doGetpullStreamList,获取拉流列表请求失败:") + replyPtr->errorString(),
                IFrameLogManager::DEBUG_TYPE);
            QString userMsg;
            switch (replyPtr->error()) {
            case QNetworkReply::OperationCanceledError:
                userMsg = QString::fromLocal8Bit("查询拉流列表超时或网络异常");
                break;
            case QNetworkReply::ConnectionRefusedError:
            case QNetworkReply::RemoteHostClosedError:
            case QNetworkReply::HostNotFoundError:
            case QNetworkReply::TimeoutError:
            case QNetworkReply::TemporaryNetworkFailureError:
            case QNetworkReply::NetworkSessionFailedError:
                userMsg = QString::fromLocal8Bit("查询拉流列表网络连接失败");
                break;
            default: {
                const QString raw = replyPtr->errorString();
                userMsg = raw.contains(QLatin1String("cancel"), Qt::CaseInsensitive)
                    ? QString::fromLocal8Bit("查询拉流列表超时或网络异常")
                    : raw;
                break;
            }
            }
            QJsonObject errObj;
            errObj[QStringLiteral("code")] = -1;
            errObj[QStringLiteral("message")] = userMsg;
            finishOnce(QString::fromUtf8(QJsonDocument(errObj).toJson(QJsonDocument::Compact)), true);
            return;
        }
        finishOnce(replyPtr->readAll(), true);
    });

    QTimer::singleShot(3000, this, [=]() {
        if (done->load(std::memory_order_acquire) != 0)
            return;
        if (replyPtr)
            replyPtr->abort();
    });

    QTimer::singleShot(4000, this, [=]() {
        if (done->load(std::memory_order_acquire) != 0)
            return;
        FrameLogManager::getInstance()->log(
            "HttpWorker",
            QString::fromLocal8Bit("doGetpullStreamList: 查询超时(3s)仍未结束，强制上报失败"),
            IFrameLogManager::DEBUG_TYPE);
        finishOnce(kTimeoutPayload, false);
    });
}


// 执行HTTP-Pos请求,发送网口设置参数
void HttpWorker::doNetWorkSet(const QString &url, const QJsonObject &Json)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader,QString::fromLocal8Bit("application/json"));
    QJsonDocument jsonDoc(Json);
    QNetworkReply *reply = m_netManager->post(request,jsonDoc.toJson());
    FrameLogManager::getInstance()->log("HttpWorker","doNetWorkSet:" + jsonDoc.toJson(),IFrameLogManager::DEBUG_TYPE);
    //异步处理响应（不阻塞）
    connect(reply,&QNetworkReply::finished,this,[=](){
        QString responseData;
        responseData = reply->readAll();
        emit requestFinishedForNetWorkSet(responseData);
        reply->deleteLater();
        if(reply->error()==QNetworkReply::NoError){
//            responseData = reply->readAll();
//            emit requestFinishedForNetWorkSet(responseData);
//            reply->deleteLater();
        }else{
            FrameLogManager::getInstance()->log("HttpWorker",QString::fromLocal8Bit("doNetWorkSet,发送网口设置参数请求失败:") + reply->errorString(),IFrameLogManager::DEBUG_TYPE);
        }
    });
}

// 执行HTTP-Pos请求,发送大屏切换指令
void HttpWorker::doSwitchBigScreenWork(const QString &url, const QJsonObject &Json, const VideoSetData &pendingContext)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    request.setTransferTimeout(5000);
#endif
    QJsonDocument jsonDoc(Json);
    QNetworkReply *reply = m_netManager->post(request, jsonDoc.toJson());
    FrameLogManager::getInstance()->log("HttpWorker","doSwitchBigScreenWork:" + jsonDoc.toJson(),IFrameLogManager::DEBUG_TYPE);

    const VideoSetData ctxCopy = pendingContext;
    static const QString kTimeoutPayload = QString::fromUtf8(u8"{\"code\":-1,\"message\":\"请求超时或网络异常，未收到有效响应\"}");
    static const QString kNoReplyPayload = QString::fromUtf8(u8"{\"code\":-1,\"message\":\"无法创建网络请求\"}");

    if (!reply) {
        FrameLogManager::getInstance()->log("HttpWorker",
            QString::fromLocal8Bit("doSwitchBigScreenWork: QNetworkAccessManager::post 返回空"),
            IFrameLogManager::DEBUG_TYPE);
        emit requestFinishedForSwitchBigScreenWork(kNoReplyPayload, ctxCopy);
        return;
    }

    QPointer<QNetworkReply> replyPtr(reply);
    auto done = std::make_shared<std::atomic<int>>(0);

    auto finishOnce = [=](const QString &payload, bool fromFinishedSlot) {
        int expected = 0;
        if (!done->compare_exchange_strong(expected, 1))
            return;
        if (m_shuttingDown)
            return;
        emit requestFinishedForSwitchBigScreenWork(payload, ctxCopy);
        if (!replyPtr)
            return;
        if (fromFinishedSlot) {
            replyPtr->deleteLater();
        } else {
            replyPtr->disconnect();
            replyPtr->abort();
            replyPtr->deleteLater();
        }
    };

    connect(reply, &QNetworkReply::finished, this, [=]() {
        if (!replyPtr)
            return;
        if (replyPtr->error() != QNetworkReply::NoError) {
            FrameLogManager::getInstance()->log(
                "HttpWorker",
                QString::fromLocal8Bit("doSwitchBigScreenWork,发送大屏切换请求失败:") + replyPtr->errorString(),
                IFrameLogManager::DEBUG_TYPE);
            QString userMsg;
            switch (replyPtr->error()) {
            case QNetworkReply::OperationCanceledError:
                // 软超时 abort() 或用户环境取消，勿直接展示 "Operation canceled"
                userMsg = QString::fromLocal8Bit("请求超时或网络异常，未收到有效响应");
                break;
            case QNetworkReply::ConnectionRefusedError:
            case QNetworkReply::RemoteHostClosedError:
            case QNetworkReply::HostNotFoundError:
            case QNetworkReply::TimeoutError:
            case QNetworkReply::TemporaryNetworkFailureError:
            case QNetworkReply::NetworkSessionFailedError:
                userMsg = QString::fromLocal8Bit("网络连接失败，请检查网络或服务是否可用");
                break;
            default: {
                const QString raw = replyPtr->errorString();
                if (raw.contains(QLatin1String("cancel"), Qt::CaseInsensitive)) {
                    userMsg = QString::fromLocal8Bit("请求超时或网络异常，未收到有效响应");
                } else {
                    userMsg = raw;
                }
                break;
            }
            }
            QJsonObject errObj;
            errObj[QStringLiteral("code")] = -1;
            errObj[QStringLiteral("message")] = userMsg;
            finishOnce(QString::fromUtf8(QJsonDocument(errObj).toJson(QJsonDocument::Compact)), true);
            return;
        }
        finishOnce(replyPtr->readAll(), true);
    });

    QTimer::singleShot(5000, this, [=]() {
        if (done->load(std::memory_order_acquire) != 0)
            return;
        if (replyPtr)
            replyPtr->abort();
    });

    QTimer::singleShot(8000, this, [=]() {
        if (done->load(std::memory_order_acquire) != 0)
            return;
        FrameLogManager::getInstance()->log(
            "HttpWorker",
            QString::fromLocal8Bit("doSwitchBigScreenWork: 硬超时仍未收到 finished，强制结束本次请求"),
            IFrameLogManager::DEBUG_TYPE);
        finishOnce(kTimeoutPayload, false);
    });
}


/**
 * @brief slotlSSELinkStatus
 * 接收SSE推流的心跳消息。
 * @param bStatus（true:已连接 false:连接断开）
 * @return 无返回值
 */
void HttpWorker::slotlSSELinkStatus(bool bStatus)
{
    if (bStatus) {
        qDebug() << "Connected to SSE server";
    } else {
        qDebug() << "Disconnected from SSE server";
    }
}

void HttpWorker::onSseErrorOccurred(const QString &errorString)
{
    FrameLogManager::getInstance()->log(
        "HttpWorker",
        QString::fromLocal8Bit("设置SSE服务器URL:") + errorString,
        IFrameLogManager::ERROR_TYPE);
}


/**
 * @brief SSELinkabort
 * 关闭连接
 * @return 无返回值
 */
void HttpWorker::SSELinkabort()
{
    m_sseClient.disconnectFromServer();
    qDebug() << "HttpWorker destroyed";
    FrameLogManager::getInstance()->log("HttpWorker",QString::fromLocal8Bit("关闭SSE服务器URL:") ,IFrameLogManager::INFO_TYPE);
}

void HttpWorker::prepareShutdown()
{
    if (m_shuttingDown)
        return;
    m_shuttingDown = true;

    m_sseClient.disconnectFromServer();

    if (!m_netManager)
        return;

    const auto replies = m_netManager->findChildren<QNetworkReply *>();
    for (QNetworkReply *reply : replies) {
        if (!reply)
            continue;
        reply->disconnect();
        reply->abort();
        reply->deleteLater();
    }
}


QJsonObject HttpWorker::parseJsonResponse(const QByteArray &data)
{
    QJsonParseError parseError;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(data, &parseError);
    
    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "JSON parse error:" << parseError.errorString();
        return QJsonObject();
    }
    
    if (jsonDoc.isObject()) {
        return jsonDoc.object();
    } else {
        qWarning() << "Invalid JSON response";
        return QJsonObject();
    }
}
