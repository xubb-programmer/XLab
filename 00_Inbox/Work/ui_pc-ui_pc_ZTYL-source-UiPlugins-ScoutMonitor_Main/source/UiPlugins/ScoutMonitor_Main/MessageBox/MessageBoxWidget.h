#ifndef MESSAGEBOXWIDGET_H
#define MESSAGEBOXWIDGET_H

#include <QPropertyAnimation>
#include <QWidget>
#include "IUiPluginBase.h"
#include "CustomMoveWidget.h"
#include "MessageType.h"
#include <QDateTime>
namespace Ui {
class MessageBoxWidget;
}

class MessageBoxWidget: public CustomMoveWidget
{
    Q_OBJECT

public:
    explicit MessageBoxWidget(QWidget *parent = NULL);
    ~MessageBoxWidget();

    //初始化
    void init(IUiPluginBase *pPlugin);

    void initData();

    //切换皮肤
    virtual void resetQss(const QString& strStyleName);

    //设置提示文本
    void setText(QString strText);

    //获取提示文本
    QString getText();

    //设置标题
    void setTitle(QString strText);

    //获取标题
    QString getTitle();

    //设置唯一标识
    void setName(QString name);

    //获取唯一标识
    QString getName();

    //设置弹窗类型
    void setType(MessageType type);

    //获取弹窗类型
    MessageType getType();


    //注册连接
    virtual void registerProtocols();

    //
    void show();

    //
    void setWindowState(bool bState);

    void startTimer();

    void hideBtnWidget();
    void hideTopWidget();
    void hideRejectBtn();

    void setIcon(QString icon);

    void setBtnState(int state);

    //设置按钮文本zzh添加
    void setBtnText(QString OK,QString Cancel);
    void setThreeBtnText(QString OK, QString Reject, QString Cancel);

    inline qint64 getTime() {return m_dateTime;}

    inline QPoint getPluginPoint() {return QPoint(m_pPlugin->getRect().x(),m_pPlugin->getRect().y());}
public slots:

    //弹窗提示
    void onHitleAndTitle(QString title , QString text);

    //弹窗确认关联
    void ontitleAndTitle(QString name , QString text);

    //chensen 20200426 begin
    //双按钮弹窗
    void onMessageText(QString title, QString text , MessageType type);
    void onMessageText(QString name, QString text ,QString topTitle, QString confirm, QString cancel);
    //三按钮弹窗
    void onThreeMessageText(QString name, QString text ,QString confirm, QString reject, QString cancel);

    //chensen 20200426 end

private slots:
    void onTimeout();

    //确定被点击
    void onBtnOkClicked();

    //取消被点击
    void onBtnCancelClicked();

    //拒绝被点击
    void onBtnRejectClicked();

    //关闭被点击
    void onBtnCloseClicked();

    //提示文本改变
    void onHintTextChanged(QString text);

    //标题文本改变
    void onTitleTextChanged(QString text);

    void onMessageBoxClosed();

    void onBtnConfirm();

    void onBtnReject();

    void onClose(MessageBoxWidget *widget);
private:
    void loadQSS();

protected:
    void showEvent(QShowEvent *event);

    void hideEvent(QHideEvent *event);

    bool eventFilter(QObject *watched, QEvent *event);

signals:
    //确定点击
    void signalConfirmClicked();

    //确定点击
    void signalConfirmBtnClicked(QString);

    //拒绝点击
    void signalRejectClicked();

    //拒绝点击
    void signalRejectBtnClicked(QString);

    //取消点击
    void siganlCancelClicked();
    //取消点击
    void signalCancelBtnClicked(QString);
private:
    Ui::MessageBoxWidget *ui;
    IUiPluginBase *m_pPlugin = nullptr;
    QVector<MessageBoxWidget *> m_vMessageBoxs;//已经显示的窗口
    QVector<MessageBoxWidget *> m_vMessageBoxsInfo;//已经显示的窗口
    QPropertyAnimation *m_pAnimation = nullptr;//动画类创建 用于显隐特效
    bool m_bHide = false;
    QString m_strName;
    QString m_text;
    QTimer *m_pTimer = nullptr;
    QString m_strStyle = "day";
    MessageType m_type = MessageType_Warn;
    int m_btnState = 2;    //按底部按钮数量

    QTimer *m_raiseTimer = nullptr;
    qint64 m_dateTime;
};

#endif // MESSAGEBOXWIDGET_H
