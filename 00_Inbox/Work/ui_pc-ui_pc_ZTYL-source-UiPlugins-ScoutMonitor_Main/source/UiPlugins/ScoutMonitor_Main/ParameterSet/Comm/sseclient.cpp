#include "sseclient.h"
#include <QUrl>
#include <QNetworkRequest>
#include <QDebug>
#include <QRegExp>
#include <QList>
#include <QtGlobal>
#include <FrameLogManager.h>

namespace {
const int kSseConnectTimeoutMs = 6000;
const int kDefaultStreamIdleTimeoutMs = 30000;
}

#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
typedef void (QNetworkReply::*QNetworkReplyErrorSignal)(QNetworkReply::NetworkError);
static const QNetworkReplyErrorSignal kReplyErrorSignal =
    static_cast<QNetworkReplyErrorSignal>(&QNetworkReply::error);
#endif

SSEClient::SSEClient(QObject *parent) : QObject(parent),
    m_manager(new QNetworkAccessManager(this)),
    m_reply(nullptr),
    m_reconnectInterval(5000),
    m_streamIdleTimeoutMs(kDefaultStreamIdleTimeoutMs),
    m_connected(false),
    m_explicitDisconnect(false),
    m_isConnecting(false),
    m_handlingReplyEnd(false)
{
    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &SSEClient::reconnect);
    m_connectTimeoutTimer.setSingleShot(true);
    connect(&m_connectTimeoutTimer, &QTimer::timeout, this, &SSEClient::onConnectTimeout);
    m_streamIdleTimer.setSingleShot(true);
    connect(&m_streamIdleTimer, &QTimer::timeout, this, &SSEClient::onStreamIdleTimeout);
}

void SSEClient::setUrl(const QString &url)
{
    m_url = url;
}

void SSEClient::connectToServer()
{
    if (m_url.isEmpty()) {
        emit errorOccurred("URL is empty");
        return;
    }

    m_explicitDisconnect = false;
    m_reconnectTimer.stop();
    m_isConnecting = true;
    tearDownReply();
    flushTransportLayer();

    QUrl url(m_url);
    QNetworkRequest request(url);

    request.setRawHeader("Accept", "text/event-stream");
    request.setRawHeader("Cache-Control", "no-cache");
    request.setRawHeader("Connection", "keep-alive");

    m_reply = m_manager->get(request);
    if (!m_reply) {
        m_isConnecting = false;
        FrameLogManager::getInstance()->log(
            "ParameterSet",
            QString::fromLocal8Bit("SSE get 返回空，将重连: ") + m_url,
            IFrameLogManager::ERROR_TYPE);
        scheduleReconnect();
        return;
    }
    connect(m_reply, &QNetworkReply::readyRead, this, &SSEClient::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &SSEClient::onFinished);
    connect(m_reply, &QNetworkReply::sslErrors, this, &SSEClient::onSslErrors);
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    connect(m_reply, &QNetworkReply::errorOccurred, this, &SSEClient::onError);
#else
    connect(m_reply, kReplyErrorSignal, this, &SSEClient::onError);
#endif

    m_connectTimeoutTimer.start(kSseConnectTimeoutMs);
    m_isConnecting = false;

    qDebug() << "Connecting to SSE server:" << m_url;
}

void SSEClient::disconnectFromServer()
{
    m_explicitDisconnect = true;
    m_reconnectTimer.stop();
    m_connectTimeoutTimer.stop();
    stopStreamIdleTimer();
    tearDownReply();
    flushTransportLayer();
    qDebug() << "Disconnected from SSE server (no auto-reconnect)";
}

void SSEClient::setReconnectInterval(int msec)
{
    m_reconnectInterval = msec;
}

void SSEClient::setStreamIdleTimeout(int msec)
{
    m_streamIdleTimeoutMs = msec;
}

void SSEClient::onReadyRead()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply || reply != m_reply) {
        return;
    }

    m_connectTimeoutTimer.stop();

    if (!m_connected) {
        m_connected = true;
        emit connectionStatusChanged(true);
        qDebug() << "SSE connected:" << m_url;
    }

    QByteArray data = m_reply->readAll();
    m_buffer.append(data);

    processBuffer();
    bumpStreamIdleTimer();
}

void SSEClient::onFinished()
{
    m_connectTimeoutTimer.stop();
    stopStreamIdleTimer();

    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply || reply != m_reply) {
        if (!m_isConnecting && !m_explicitDisconnect && m_reply == nullptr && !m_url.isEmpty()
            && m_reconnectInterval > 0 && !m_reconnectTimer.isActive()) {
            scheduleReconnect();
        }
        return;
    }

    const QNetworkReply::NetworkError err = m_reply->error();
    if (!m_explicitDisconnect) {
        if (err == QNetworkReply::NoError) {
            FrameLogManager::getInstance()->log(
                "ParameterSet",
                QString::fromLocal8Bit("SSE 流结束(无错误)，将重连: ") + m_url,
                IFrameLogManager::DEBUG_TYPE);
        } else if (err != QNetworkReply::OperationCanceledError) {
            QString msg = QString::fromLocal8Bit("SSE 连接结束异常 [");
            msg += QString::number(static_cast<int>(err));
            msg += QString::fromLocal8Bit("] ");
            msg += m_reply->errorString();
            msg += QString::fromLocal8Bit(" — ");
            msg += m_url;
            FrameLogManager::getInstance()->log("ParameterSet", msg, IFrameLogManager::ERROR_TYPE);
        }
    }

    resetConnection();
}

void SSEClient::onSslErrors(const QList<QSslError> &errors)
{
    QStringList sslErrorMessages;
    foreach (const QSslError &sslError, errors) {
        sslErrorMessages << sslError.errorString();
    }
    FrameLogManager::getInstance()->log(
        "ParameterSet",
        QString::fromLocal8Bit("SSL 错误: %1").arg(sslErrorMessages.join("; ")),
        IFrameLogManager::ERROR_TYPE);
    emit errorOccurred(QString("SSL errors: %1").arg(sslErrorMessages.join("; ")));

    if (m_reply) {
        m_reply->abort();
    }
}

void SSEClient::onError(QNetworkReply::NetworkError code)
{
    Q_UNUSED(code);
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply || reply != m_reply) {
        return;
    }

    emit errorOccurred(m_reply->errorString());
    resetConnection();
}

void SSEClient::reconnect()
{
    connectToServer();
}

void SSEClient::onConnectTimeout()
{
    if (m_explicitDisconnect || !m_reply) {
        return;
    }
    FrameLogManager::getInstance()->log(
        "ParameterSet",
        QString::fromLocal8Bit("SSE 连接超时(") + QString::number(kSseConnectTimeoutMs)
            + QString::fromLocal8Bit("ms)，中止并进入重连: ") + m_url,
        IFrameLogManager::DEBUG_TYPE);
    m_reply->abort();
}

void SSEClient::onStreamIdleTimeout()
{
    if (m_explicitDisconnect || !m_reply) {
        return;
    }
    FrameLogManager::getInstance()->log(
        "ParameterSet",
        QString::fromLocal8Bit("SSE 流空闲超时(") + QString::number(m_streamIdleTimeoutMs)
            + QString::fromLocal8Bit("ms)，中止并进入重连: ") + m_url,
        IFrameLogManager::DEBUG_TYPE);
    m_reply->abort();
}

void SSEClient::scheduleReconnect()
{
    if (m_explicitDisconnect || m_reconnectInterval <= 0 || m_url.isEmpty()) {
        return;
    }
    m_reconnectTimer.start(m_reconnectInterval);
}

void SSEClient::processBuffer()
{
    while (m_buffer.contains("\n\n") || m_buffer.contains("\r\n\r\n")) {
        int endIndex = -1;
        if (m_buffer.contains("\r\n\r\n")) {
            endIndex = m_buffer.indexOf("\r\n\r\n");
        } else {
            endIndex = m_buffer.indexOf("\n\n");
        }

        if (endIndex == -1) {
            break;
        }

        QString message = m_buffer.left(endIndex);
        const int separatorLen = m_buffer.mid(endIndex, 4) == "\r\n\r\n" ? 4 : 2;
        m_buffer.remove(0, endIndex + separatorLen);

        QString eventType = "message";
        QString data;

        QStringList lines = message.split(QRegExp("\r\n|\n"));
        foreach (const QString &line, lines) {
            if (line.isEmpty()) {
                continue;
            }

            int colonIndex = line.indexOf(':');
            if (colonIndex == -1) {
                continue;
            }

            QString field = line.left(colonIndex).trimmed();
            QString value = line.mid(colonIndex + 1).trimmed();

            if (field == "event") {
                eventType = value;
            } else if (field == "data") {
                data += value + "\n";
            }
        }

        if (!data.isEmpty()) {
            data.chop(1);
        }

        if (!data.isEmpty()) {
            emit eventReceived(eventType, data);
        }
    }
}

void SSEClient::tearDownReply()
{
    m_connectTimeoutTimer.stop();
    stopStreamIdleTimer();
    m_buffer.clear();
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    if (m_connected) {
        m_connected = false;
        emit connectionStatusChanged(false);
    }
}

void SSEClient::flushTransportLayer()
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 8, 0)
    m_manager->clearConnectionCache();
#endif
    m_manager->clearAccessCache();
}

void SSEClient::resetConnection()
{
    if (m_handlingReplyEnd) {
        return;
    }
    m_handlingReplyEnd = true;
    tearDownReply();
    scheduleReconnect();
    m_handlingReplyEnd = false;
}

void SSEClient::stopStreamIdleTimer()
{
    m_streamIdleTimer.stop();
}

void SSEClient::bumpStreamIdleTimer()
{
    if (m_streamIdleTimeoutMs <= 0 || m_explicitDisconnect || !m_reply) {
        return;
    }
    m_streamIdleTimer.start(m_streamIdleTimeoutMs);
}
