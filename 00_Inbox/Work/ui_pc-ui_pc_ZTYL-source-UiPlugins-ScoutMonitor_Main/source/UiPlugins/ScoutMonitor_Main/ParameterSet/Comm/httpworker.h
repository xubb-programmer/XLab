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
#include "Comm/sseclient.h"
#include "GenericComponent/definestruct.h"

class HttpWorker : public QObject
{
    Q_OBJECT
public:
    explicit HttpWorker(QObject *parent = nullptr);
    ~HttpWorker();

    // 设置请求参数
    void setParams(const QJsonObject &params);

signals:
    // 发送ZLM请求结果信号
    void requestZLMFinished(const QJsonObject &result);

    // 发送设备状态查询请求结果
    void requestFinishedForDeviceStatus(QString strData);

    // 发送设置拉流工作参数结果（pendingContext / reqKind 与单次请求绑定，支持并发多路下发）
    void requestFinishedForPullStreamPara(QString strData, VideoSetData pendingContext, int reqKind);

    // 发送设置推流工作参数结果
    void requestFinishedForPushStreamPara(QString strData);

    // 发送设置Ocr识别区域结果
    void requestFinishedForOcrPushStreamPara(QString strData);

    // 发送ORC识别响应结果
    void requestFinishedForOCRSwitch(QString strData);

    // 发送目标识别响应结果
    void requestFinishedForRecognitionSwitch(QString strData);

    // 发送获取拉流列表响应结果
    void requestFinishedForgetpullStreamList(QString strData);

    // 发送获取ZLM服务器推流列表响应结果
    void requestFinishedForgetZLMpushList(QString strEvent,QString strData);

    //发送获取网口参数设置响应结果
    void requestFinishedForNetWorkSet(QString strData);

    //发送获取大屏切换指令响应结果（pendingContext 与该次请求绑定，支持多路并发）
    void requestFinishedForSwitchBigScreenWork(QString strData, VideoSetData pendingContext);

    // 发送错误信息信号
    void requestError(const QString &errorString);
    
    // 通知线程开始工作
    void startWork();

public slots:
    /** 在工作线程中建立/切换 SSE 连接 */
    void setsseClient(const QString &url);

    /** 在工作线程中关闭 SSE（不自动重连） */
    void SSELinkabort();

    // 执行HTTP-Pos请求,发送拉流设置消息（pendingContext 为该次请求对应的界面快照）
    void doPospullStreamParaWork(const QString &url, const QJsonObject &Json, const VideoSetData &pendingContext, int reqKind);

    // 执行HTTP-Pos请求,发送推流设置消息
    void doPosPushStreamParaWork(const QString &url, const QJsonObject &Json);

    // 执行HTTP-Pos请求,发送OCR区域设置消息
    void doOcrpushStreamParaWork(const QString &url, const QJsonObject &Json);
    
    // 处理HTTP响应(ZLM) Finish
//    void onReplyFinished(QNetworkReply *reply);
    void onReplyFinished();

    // 执行HTTP-Pos请求,发送ORC识别消息
    void doOCRSwitchWork(const QString &url, const QJsonObject &Json);

    // 执行HTTP-Pos请求,发送目标识别消息
    void doRecognitionSwitchWork(const QString &url, const QJsonObject &Json);

    // 执行HTTP-Pos请求,获取拉流列表
    void doGetpullStreamList(const QString &url);

    //执行HTTP-Pos请求,发送网口设置参数
    void doNetWorkSet(const QString &url, const QJsonObject &Json);

    // 执行HTTP-Pos请求,发送大屏切换指令（pendingContext 随响应原样带回）
    void doSwitchBigScreenWork(const QString &url, const QJsonObject &Json, const VideoSetData &pendingContext);

    /** 退出前在工作线程调用：断开 SSE、取消在途 HTTP，不再向 UI 上报 */
    void prepareShutdown();


protected slots:
    /**
     * @brief slotlSSELinkStatus
     * 接收SSE推流的心跳消息。
     * @param bStatus（true:已连接 false:连接断开）
     * @return 无返回值
     */
    void slotlSSELinkStatus(bool bStatus);

private slots:
    void onSseErrorOccurred(const QString &errorString);

private:
    QNetworkAccessManager *m_netManager=nullptr;  //普通链接
    SSEClient m_sseClient; //创建SSE客户端实例
    QNetworkReply *m_sseReply=nullptr; //sse链接 reply
    QUrl m_url;
    QJsonObject m_params;
    bool m_shuttingDown = false;

    // 解析JSON响应
    QJsonObject parseJsonResponse(const QByteArray &data);
};

#endif // HTTPWORKER_H
