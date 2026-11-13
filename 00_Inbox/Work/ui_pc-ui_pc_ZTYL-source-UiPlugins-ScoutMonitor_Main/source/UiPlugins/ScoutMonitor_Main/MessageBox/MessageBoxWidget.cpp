#include "MessageBoxWidget.h"
#include "ui_MessageBoxWidget.h"
#include <QFile>
#include <QTextStream>
#include <QTimer>
#include <QDesktopWidget>
#include "UiPluginFactory.h"
#include "QKeyEvent"
#include "FrameLogManager.h"

MessageBoxWidget::MessageBoxWidget(QWidget *parent):
    CustomMoveWidget(parent),
    ui(new Ui::MessageBoxWidget)
{
    ui->setupUi(this);
    CustomMoveWidget::setTitle(ui->top_widget);

}

MessageBoxWidget::~MessageBoxWidget()
{
    delete ui;
}

//初始化
void MessageBoxWidget::init(IUiPluginBase *pPlugin)
{
    m_pPlugin = pPlugin;
    this->setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Window | Qt::SubWindow | Qt::X11BypassWindowManagerHint);

    ui->text->adjustSize(); //chensen 20200426
    ui->text->setWordWrap(true);    //chensen 20200426 begin
//    this->setAttribute(Qt::WA_TranslucentBackground,true);
    connect(ui->pushButton_ok,SIGNAL(clicked()),this,SLOT(onBtnOkClicked()));
    connect(ui->pushButton_cancel,SIGNAL(clicked()),this,SLOT(onBtnCancelClicked()));
    connect(ui->pushButton_reject,SIGNAL(clicked()),this,SLOT(onBtnRejectClicked()));

    //动画创建
    m_pAnimation = new QPropertyAnimation(this, "geometry");
    m_pAnimation->setEasingCurve(QEasingCurve::InOutSine);


    m_pTimer = new QTimer(this);
    connect(m_pTimer,SIGNAL(timeout()),this,SLOT(onTimeout()));
    m_pTimer->setInterval(3000);

    m_dateTime = QDateTime::currentDateTime().toMSecsSinceEpoch();

    getCurrentSeat(pPlugin->getRect().x());
}

void MessageBoxWidget::initData()
{
    m_raiseTimer = new QTimer(this);
    connect(m_raiseTimer,&QTimer::timeout, this, [=]()
    {
       QVector<MessageBoxWidget *> vec = m_vMessageBoxs + m_vMessageBoxsInfo;

       if(vec.size() <= 0)
           return;

       qSort(vec.begin(), vec.end(), [](MessageBoxWidget *left , MessageBoxWidget *right){
           return left->getTime() < right->getTime();
       });

        MessageBoxWidget *widget = vec.last();
        widget->raise();

    });
    m_raiseTimer->start(100);
}

void MessageBoxWidget::resetQss(const QString &strStyleName)
{
    m_pPlugin->setStyle(strStyleName);
    m_strStyle = strStyleName;
    for(int nIndex = 0;nIndex < m_vMessageBoxs.size(); ++nIndex)
    {
        m_vMessageBoxs[nIndex]->resetQss(strStyleName);
    }

    for(int nIndex = 0;nIndex < m_vMessageBoxsInfo.size(); ++nIndex)
    {
        m_vMessageBoxsInfo[nIndex]->resetQss(strStyleName);
    }

    if(m_btnState == 1)
    {
        ui->pushButton_ok->setStyleSheet(QString::fromLocal8Bit("QPushButton#pushButton_ok{font:12pt \"微软雅黑\";color:white;background:transparent;border-image: url(:/resource/day/lbtn_normal1.png);}"
                                          "QPushButton#pushButton_ok:hover{font:12pt \"微软雅黑\";color:white;background:transparent;border-image: url(:/resource/day/lbtn_hover1.png);}"
                                          "QPushButton#pushButton_ok:pressed{font:12pt \"微软雅黑\";color:white;background:transparent;border-image: url(:/resource/day/lbtn_pressed1.png);}"));
    }
    else if(m_btnState == 2)
    {
        ui->pushButton_ok->setStyleSheet(QString::fromLocal8Bit("QPushButton#pushButton_ok{font:12pt \"微软雅黑\";color:white;background:transparent;border-image: url(:/resource/day/lbtn_normal2.png);}"
                                          "QPushButton#pushButton_ok:hover{font:12pt \"微软雅黑\";color:white;background:transparent;border-image: url(:/resource/day/lbtn_hover2.png);}"
                                          "QPushButton#pushButton_ok:pressed{font:12pt \"微软雅黑\";color:white;background:transparent;border-image: url(:/resource/day/lbtn_pressed2.png);}"));

        ui->pushButton_cancel->setStyleSheet(QString::fromLocal8Bit("QPushButton#pushButton_cancel{font:12pt \"微软雅黑\";color:white;background:transparent;border-image: url(:/resource/day/rbtn_normal2.png);}"
                                          "QPushButton#pushButton_cancel:hover{font:12pt \"微软雅黑\";color:white;background:transparent;border-image: url(:/resource/day/rbtn_hover2.png);}"
                                          "QPushButton#pushButton_cancel:pressed{font:12pt \"微软雅黑\";color:white;background:transparent;border-image: url(:/resource/day/rbtn_pressed2.png);}"));
    }
    else if(m_btnState == 3)
    {
        ui->pushButton_ok->setStyleSheet(QString::fromLocal8Bit("QPushButton#pushButton_ok{font:12pt \"微软雅黑\";color:white;background:transparent;border-image: url(:/resource/day/lbtn_normal3.png);}"
                                          "QPushButton#pushButton_ok:hover{font:12pt \"微软雅黑\";color:white;background:transparent;border-image: url(:/resource/day/lbtn_hover3.png);}"
                                          "QPushButton#pushButton_ok:pressed{font:12pt \"微软雅黑\";color:white;background:transparent;border-image: url(:/resource/day/lbtn_pressed3.png);}"));

        ui->pushButton_cancel->setStyleSheet(QString::fromLocal8Bit("QPushButton#pushButton_cancel{font:12pt \"微软雅黑\";color:white;background:transparent;border-image: url(:/resource/day/rbtn_normal3.png);}"
                                          "QPushButton#pushButton_cancel:hover{font:12pt \"微软雅黑\";color:white;background:transparent;border-image: url(:/resource/day/rbtn_hover3.png);}"
                                          "QPushButton#pushButton_cancel:pressed{font:12pt \"微软雅黑\";color:white;background:transparent;border-image: url(:/resource/day/rbtn_pressed3.png);}"));

        ui->pushButton_reject->setStyleSheet(QString::fromLocal8Bit("QPushButton#pushButton_reject{font:12pt \"微软雅黑\";color:white;background:transparent;border-image: url(:/resource/day/mbtn_normal3.png);}"
                                          "QPushButton#pushButton_reject:hover{font:12pt \"微软雅黑\";color:white;background:transparent;border-image: url(:/resource/day/mbtn_hover3.png);}"
                                          "QPushButton#pushButton_reject:pressed{font:12pt \"微软雅黑\";color:white;background:transparent;border-image: url(:/resource/day/mbtn_pressed3.png);}"));
    }

    loadQSS();
}

void MessageBoxWidget::setText(QString strText)
{
    ui->text->setText(strText);
}

//获取提示文本
QString MessageBoxWidget::getText()
{
    return ui->text->text();
}

//设置标题
void MessageBoxWidget::setTitle(QString strText)
{
    ui->title->setText(strText);
}

//获取标题
QString MessageBoxWidget::getTitle()
{
    return ui->title->text();
}

void MessageBoxWidget::setName(QString name)
{
    m_strName = name;
}

QString MessageBoxWidget::getName()
{
    return m_strName;
}

void MessageBoxWidget::setType(MessageType type)
{
    m_type = type;
    QString icon;
    switch (type) {
    case MessageType_Success: icon = "Success";break;
    case MessageType_Info: icon = "Info";break;
    case MessageType_Warn: icon = "Warn";break;
    case MessageType_Err: icon = "Err";break;
    default:
        break;
    }
    ui->icon->setStyleSheet(QString::fromLocal8Bit("border-image:url(:/resource/day/%1.png)").arg(icon));
}

MessageType MessageBoxWidget::getType()
{
    return m_type;
}

//注册连接
void MessageBoxWidget::registerProtocols()
{   
    //罗精添加
   std::vector<IUiPluginBase *> vPlugins = UiPluginFactory::getInstance()->getPlugins("GDExplore");
   foreach (IUiPluginBase * plugin, vPlugins) {
       connect(this,SIGNAL(siganlCancelClicked()),plugin->getWidget(),SLOT(onGBZS()));
   }
   vPlugins = UiPluginFactory::getInstance()->getPlugins("CatEyeExplore");
   foreach (IUiPluginBase * plugin, vPlugins) {
       connect(this,SIGNAL(signalCancelBtnClicked(QString)),plugin->getWidget(),SLOT(sltCancelClicked(QString)));
   }
}

//
void MessageBoxWidget::show()
{
    QRect screeRect=QApplication::desktop()->availableGeometry();
    QRect orgRect=QRect(QPoint(screeRect.width()/2,screeRect.height()/2),QSize(0,0));
    this->move((screeRect.width()-this->width())/2,(screeRect.height()-this->height())/2);
//    QRect orgRect = QRect(QPoint(m_pPlugin->getRect().x(),m_pPlugin->getRect().y()),QSize(0,0));  
//    m_pAnimation->setStartValue(orgRect);
//    m_pAnimation->setEndValue(m_pPlugin->getRect());
//    m_pAnimation->setDuration(1000);
//    m_pAnimation->start();
    QWidget::show();

}

void MessageBoxWidget::setWindowState(bool bState)
{
    ui->pushButton_cancel->setVisible(bState);
}

void MessageBoxWidget::startTimer()
{
    m_pTimer->start();
}

void MessageBoxWidget::hideBtnWidget()
{
    this->setFixedSize(this->width(),this->height()-ui->widget_2->height());
    ui->widget_2->hide();
}

void MessageBoxWidget::hideTopWidget()
{
    ui->top_widget->hide();
}

void MessageBoxWidget::hideRejectBtn()
{
    ui->pushButton_reject->hide();
}

void MessageBoxWidget::setIcon(QString icon)
{
    ui->icon->setStyleSheet(QString::fromLocal8Bit("border-image:url(:/resource/day/%1.png)").arg(icon));
}

void MessageBoxWidget::setBtnState(int state)
{
    if(state == 1)
    {
        ui->pushButton_cancel->hide();
        ui->pushButton_reject->hide();
    }
    else if(state == 2)
    {
        ui->pushButton_reject->hide();
    }

    m_btnState = state;
}

//修改按钮名称 zzh添加
void MessageBoxWidget::setBtnText(QString OK, QString Cancel)
{
    ui->pushButton_ok->setText(OK);
    ui->pushButton_cancel->setText(Cancel);
}

void MessageBoxWidget::setThreeBtnText(QString OK, QString Reject, QString Cancel)
{
    ui->pushButton_ok->setText(OK);
    ui->pushButton_reject->setText(Reject);
    ui->pushButton_cancel->setText(Cancel);
}

//确定被点击
void MessageBoxWidget::onBtnOkClicked()
{
//    FrameLogManager::getInstance()->log(QString::fromLocal8Bit("MessageBox"),QString::fromLocal8Bit("onBtnOkClicked type:%1 name:%2").arg(m_type).arg(m_strName),IFrameLogManager::DEBUG_TYPE);
    this->close();
    emit signalConfirmClicked();
}

//取消被点击
void MessageBoxWidget::onBtnCancelClicked()
{
//    FrameLogManager::getInstance()->log(QString::fromLocal8Bit("MessageBox"),QString::fromLocal8Bit("onBtnCancelClicked type:%1 name:%2").arg(m_type).arg(m_strName),IFrameLogManager::DEBUG_TYPE);

    //m_pTimer->stop();
    this->close();
    emit siganlCancelClicked();
}

void MessageBoxWidget::onBtnRejectClicked()
{
//    FrameLogManager::getInstance()->log(QString::fromLocal8Bit("MessageBox"),QString::fromLocal8Bit("onBtnRejectClicked type:%1 name:%2").arg(m_type).arg(m_strName),IFrameLogManager::DEBUG_TYPE);

    this->close();
    emit signalRejectClicked();
}

//关闭被点击
void MessageBoxWidget::onBtnCloseClicked()
{
//    FrameLogManager::getInstance()->log(QString::fromLocal8Bit("MessageBox"),QString::fromLocal8Bit("onBtnCloseClicked type:%1 name:%2").arg(m_type).arg(m_strName),IFrameLogManager::DEBUG_TYPE);

    this->close();
    emit siganlCancelClicked();
}

//提示文本改变
void MessageBoxWidget::onHintTextChanged(QString text)
{
    setText(text);
}

//标题文本改变
void MessageBoxWidget::onTitleTextChanged(QString text)
{
    setTitle(text);
}

//
void MessageBoxWidget::onMessageBoxClosed()
{
    FrameLogManager::getInstance()->log(QString::fromLocal8Bit("MessageBox"),QString::fromLocal8Bit("onMessageBoxClosed type:%1 name:%2").arg(m_type).arg(m_strName),IFrameLogManager::DEBUG_TYPE);

    MessageBoxWidget * widget = static_cast<MessageBoxWidget *>(QObject::sender());

    bool bInfo = false;
    QVector<MessageBoxWidget *> *messageBoxs;
    if(widget->getType() == MessageType_Info)
    {
        messageBoxs = &m_vMessageBoxsInfo;
        bInfo = true;
    }
    else
    {
        messageBoxs = &m_vMessageBoxs;
    }

    for(int nIndex = 0;nIndex < messageBoxs->size(); ++nIndex)
    {
        if(widget == (*messageBoxs)[nIndex])
        {
            widget->deleteLater();
            messageBoxs->remove(nIndex);
            if(bInfo)
            {
                emit  signalCancelBtnClicked(m_strName);
                m_strName = "";
            }
            else
            {
                emit  signalCancelBtnClicked("");
            }

            return;
        }
    }

//    emit signalCancelBtnClicked(m_strName);
//    widget->deleteLater();

}

void MessageBoxWidget::onBtnConfirm()
{
    //m_pTimer->stop();
    FrameLogManager::getInstance()->log(QString::fromLocal8Bit("MessageBox"),QString::fromLocal8Bit("onBtnConfirm type:%1 name:%2").arg(m_type).arg(m_strName),IFrameLogManager::DEBUG_TYPE);

    qDebug() << "MessageBoxWidget::onBtnConfirm()";
    MessageBoxWidget * widget = static_cast<MessageBoxWidget *>(QObject::sender());

    if(!widget)
        return;

    QVector<MessageBoxWidget *> *messageBoxs;
    if(widget->getType() == MessageType_Info)
    {
        messageBoxs = &m_vMessageBoxsInfo;
    }
    else
    {
        messageBoxs = &m_vMessageBoxs;
    }

    for(int nIndex = 0;nIndex < messageBoxs->size(); ++nIndex)
    {
        if(widget == (*messageBoxs)[nIndex])
        {
//            delete widget;
            messageBoxs->remove(nIndex);
            emit signalConfirmBtnClicked(m_strName);
            m_strName = "";
            widget->deleteLater();
            return;
        }
    }

//    emit signalConfirmBtnClicked(m_strName);
//    widget->deleteLater();
}

void MessageBoxWidget::onBtnReject()
{
    //m_pTimer->stop();
    FrameLogManager::getInstance()->log(QString::fromLocal8Bit("MessageBox"),QString::fromLocal8Bit("onBtnReject type:%1 name:%2").arg(m_type).arg(m_strName),IFrameLogManager::DEBUG_TYPE);

    qDebug() << "MessageBoxWidget::onBtnReject()";
    MessageBoxWidget * widget = static_cast<MessageBoxWidget *>(QObject::sender());

    QVector<MessageBoxWidget *> *messageBoxs;
    if(widget->getType() == MessageType_Info)
    {
        messageBoxs = &m_vMessageBoxsInfo;
    }
    else
    {
        messageBoxs = &m_vMessageBoxs;
    }

    for(int nIndex = 0;nIndex < messageBoxs->size(); ++nIndex)
    {
        if(widget == (*messageBoxs)[nIndex])
        {
            messageBoxs->remove(nIndex);
            emit signalRejectBtnClicked(m_strName);
            m_strName = "";
            widget->deleteLater();
            return;
        }
    }
}

void MessageBoxWidget::onClose(MessageBoxWidget *widget)
{
    QVector<MessageBoxWidget *> *messageBoxs;
    if(widget->getType() == MessageType_Info)
    {
        messageBoxs = &m_vMessageBoxsInfo;
    }
    else
    {
        messageBoxs = &m_vMessageBoxs;
    }

    for(int nIndex = 0;nIndex < messageBoxs->size(); ++nIndex)
    {
        if(widget == (*messageBoxs)[nIndex])
        {
            widget->deleteLater();
            messageBoxs->remove(nIndex);
            m_strName = "";
            return;
        }
    }
}

//弹窗提示
void MessageBoxWidget::onHitleAndTitle(QString title, QString text)
{
    onMessageText(title,text,MessageType_Warn);
}

//弹窗确认关联
void MessageBoxWidget::ontitleAndTitle(QString name, QString text)
{
    onMessageText(name,text,MessageType_Info);
}

//chensen 20200426 begin
void MessageBoxWidget::onMessageText(QString title, QString text, MessageType type)
{
    MessageType mtype = type;
    MessageBoxWidget *widget = nullptr;
    FrameLogManager::getInstance()->log(QString::fromLocal8Bit("MessageBox"),QString::fromLocal8Bit("onMessageText title:%1 text:%2 type:%3 m_strName:%4").arg(title).arg(text).arg(type).arg(m_strName),IFrameLogManager::DEBUG_TYPE);

    if(mtype == MessageType_Success)
    {
        for(int nIndex = 0;nIndex < m_vMessageBoxs.size(); ++nIndex)
        {
            if(m_vMessageBoxs[nIndex]->getText() == text && m_vMessageBoxs[nIndex]->getTitle() == title){
                return;
            }
        }

        widget = new MessageBoxWidget();
        widget->init(m_pPlugin);
		widget->resetQss(m_strStyle);
        widget->hideBtnWidget();
        widget->setTitle(title);
        widget->setText(text);
        widget->startTimer();
        widget->setType(type);
        widget->setParent(NULL);

        connect(widget,SIGNAL(siganlCancelClicked()),SLOT(onMessageBoxClosed()));

        widget->show();

    }
    else if(mtype == MessageType_Info)
    {
        qDebug() << "MessageBoxWidget::onMessageText" << m_strName << m_vMessageBoxsInfo.size();
        if(m_strName != "")
        {
//            this->close();
            for(int nIndex = 0;nIndex < m_vMessageBoxsInfo.size(); ++nIndex)
            {
//                m_vMessageBoxsInfo.at(nIndex)->hide();
                delete m_vMessageBoxsInfo.at(nIndex);
                m_vMessageBoxsInfo[nIndex] = NULL;
            }
            m_vMessageBoxsInfo.clear();
        }

        m_strName = title;
        widget = new MessageBoxWidget(m_pPlugin->getWidget()->parentWidget());
        widget->init(m_pPlugin);
        widget->setParent(NULL);

        //修改弹窗界面按钮文字zzh添加
        if(title==QString::fromUtf8(u8"防卫方案接收"))
        {
            qDebug()<<"fangweifanganjeishouchenggong";
            widget->setTitle(QString::fromUtf8(u8"接收任务"));
            setBtnText(QString::fromUtf8(u8"执行"),
                       QString::fromUtf8(u8"挂起"));
        }
        else
        {
           widget->setTitle(QString::fromUtf8(u8"提示"));
        }
        widget->setName(m_strName);
        widget->setText(text);
        widget->setBtnState(2);
        widget->setType(type);
        widget->resetQss(m_strStyle);
        widget->setWindowModality(Qt::ApplicationModal);


        connect(widget,SIGNAL(signalConfirmClicked()),SLOT(onBtnConfirm()));
        connect(widget,SIGNAL(siganlCancelClicked()),SLOT(onMessageBoxClosed()));

        widget->show();
        m_vMessageBoxsInfo.push_back(widget);
        return;

    }
    else if(mtype == MessageType_Warn)
    {
        for(int nIndex = 0;nIndex < m_vMessageBoxs.size(); ++nIndex)
        {
            if(m_vMessageBoxs[nIndex]->getText() == text && m_vMessageBoxs[nIndex]->getTitle() == title){
                return;
            }
        }

        widget = new MessageBoxWidget();
        widget->init(m_pPlugin);
        widget->setTitle(title);
        widget->setText(text);
        widget->setBtnState(1);
        widget->resetQss(m_strStyle);
        widget->setType(type);
        widget->setParent(NULL);
        widget->setWindowModality(Qt::ApplicationModal);

        connect(widget,SIGNAL(signalConfirmClicked()),SLOT(onMessageBoxClosed()));
        connect(widget,SIGNAL(siganlCancelClicked()),SLOT(onMessageBoxClosed()));

        widget->show();
    }
    else if(mtype == MessageType_Err)
    {
        for(int nIndex = 0;nIndex < m_vMessageBoxs.size(); ++nIndex)
        {
            if(m_vMessageBoxs[nIndex]->getText() == text && m_vMessageBoxs[nIndex]->getTitle() == title){
                return;
            }
        }

        widget = new MessageBoxWidget();
        widget->init(m_pPlugin);
        widget->setTitle(title);
        widget->setText(text);
        widget->setBtnState(1);
        widget->resetQss(m_strStyle);
        widget->setType(type);
        widget->setParent(NULL);
        widget->setWindowModality(Qt::ApplicationModal);

        connect(widget,SIGNAL(signalConfirmClicked()),SLOT(onMessageBoxClosed()));
        connect(widget,SIGNAL(siganlCancelClicked()),SLOT(onMessageBoxClosed()));

        widget->show();
    }

	m_vMessageBoxs.push_back(widget);
	loadQSS();
	
}

void MessageBoxWidget::onMessageText(QString name, QString text, QString topTitle, QString confirm, QString cancel)
{
    FrameLogManager::getInstance()->log(QString::fromLocal8Bit("MessageBox"),QString::fromLocal8Bit("onMessageText name:%1 text:%2 topTitle:%3 confirm:%4 cancel:%5 m_strName:%6").arg(name).arg(text).arg(topTitle).arg(confirm).arg(cancel).arg(m_strName),IFrameLogManager::DEBUG_TYPE);
    if(m_strName != "")
    {
//            this->close();
        for(int nIndex = 0;nIndex < m_vMessageBoxsInfo.size(); ++nIndex)
        {
//            m_vMessageBoxsInfo.at(nIndex)->hide();
            delete m_vMessageBoxsInfo.at(nIndex);
            m_vMessageBoxsInfo[nIndex] = NULL;
        }
        m_vMessageBoxsInfo.clear();
    }

    MessageBoxWidget *widget = nullptr;
    m_strName = name;
    widget = new MessageBoxWidget();
    widget->init(m_pPlugin);
    widget->setText(text);
    widget->setTitle(topTitle);
    widget->setBtnText(confirm,cancel);
    widget->setBtnState(2);
    widget->resetQss(m_strStyle);
    widget->setType(MessageType_Info);
    widget->setParent(NULL);
    widget->setWindowModality(Qt::ApplicationModal);

    connect(widget,SIGNAL(signalConfirmClicked()),SLOT(onBtnConfirm()));
    connect(widget,SIGNAL(siganlCancelClicked()),SLOT(onMessageBoxClosed()));
    widget->resetQss(m_strStyle);
    widget->show();

    m_vMessageBoxsInfo.push_back(widget);

    loadQSS();
}

void MessageBoxWidget::onThreeMessageText(QString name, QString text, QString confirm, QString reject, QString cancel)
{
    FrameLogManager::getInstance()->log(QString::fromLocal8Bit("MessageBox"),QString::fromLocal8Bit("onThreeMessageText name:%1 text:%2 confirm:%3 reject:%4 cancel:%5 m_strName:%6").arg(name).arg(text).arg(confirm).arg(reject).arg(cancel).arg(m_strName),IFrameLogManager::DEBUG_TYPE);
    if(m_strName != "")
    {
//            this->close();
        for(int nIndex = 0;nIndex < m_vMessageBoxsInfo.size(); ++nIndex)
        {
//            m_vMessageBoxsInfo.at(nIndex)->hide();
            delete m_vMessageBoxsInfo.at(nIndex);
            m_vMessageBoxsInfo[nIndex] = NULL;
        }
        m_vMessageBoxsInfo.clear();
    }

    MessageBoxWidget *widget = nullptr;
    m_strName = name;
    widget = new MessageBoxWidget();
    widget->init(m_pPlugin);
    widget->resetQss(m_strStyle);
    widget->setParent(NULL);

    widget->setText(text);
    widget->setBtnState(3);
    widget->setType(MessageType_Info);
    widget->setTitle(QString::fromUtf8(u8"提示"));
    widget->setThreeBtnText(confirm,reject,cancel);
    widget->setWindowModality(Qt::ApplicationModal);

    connect(widget,SIGNAL(signalConfirmClicked()),SLOT(onBtnConfirm()));
    connect(widget,SIGNAL(siganlCancelClicked()),SLOT(onMessageBoxClosed()));
    connect(widget,SIGNAL(signalRejectClicked()),SLOT(onBtnReject()));

    widget->resetQss(m_strStyle);
    widget->show();

    m_vMessageBoxsInfo.push_back(widget);

    loadQSS();
}


void MessageBoxWidget::onTimeout()
{
    onBtnCancelClicked();
}


void MessageBoxWidget::loadQSS()
{
    QFile textFile(m_pPlugin->getStylePath() + "/MessageBoxWidget.qss");
    Q_ASSERT(textFile.exists() && "file can't open");
    textFile.open(QIODevice::ReadOnly | QIODevice::Text);
    QTextStream textStream(&textFile);
    textStream.setCodec("UTF-8");
    this->setStyleSheet(textStream.readAll());
}


void MessageBoxWidget::showEvent(QShowEvent *event)
{
    qApp->installEventFilter(this);
    QString parentName = "null";
    if(parentWidget())
    {
        this->move((parentWidget()->width() - this->width())*0.5,
                   (parentWidget()->height() - this->height())*0.5);

        parentName = parent()->objectName();

    }
    raise();
    FrameLogManager::getInstance()->log(QString::fromLocal8Bit("MessageBox"),QString::fromLocal8Bit("showEvent x:%1 y:%2 width:%3 height:%4 visiable:%5 isTopLevel:%6 parent:%7")
                                        .arg(this->geometry().x()).arg(this->geometry().y()).arg(this->geometry().width()).arg(this->geometry().height())
                                        .arg(this->isVisible()).arg(this->isTopLevel()).arg(parentName),IFrameLogManager::DEBUG_TYPE);

//    event->ignore();
}

void MessageBoxWidget::hideEvent(QHideEvent *event)
{
    qDebug() << "MessageBoxWidget::hideEvent";
    qApp->removeEventFilter(this);
    FrameLogManager::getInstance()->log(QString::fromLocal8Bit("MessageBox"),QString::fromLocal8Bit("hideEvent x:%1 y:%2 width:%3 height:%4 visiable:%5").arg(this->geometry().x()).arg(this->geometry().y()).arg(this->geometry().width()).arg(this->geometry().height()).arg(this->isVisible()),IFrameLogManager::DEBUG_TYPE);

}

bool MessageBoxWidget::eventFilter(QObject *watched, QEvent *event)
{
    if(event->type() == QEvent::KeyRelease)
    {
        QKeyEvent *key = (QKeyEvent *)event;
        if(key->key() == Qt::Key_Enter || key->key() == Qt::Key_Return)
        {
            onBtnOkClicked();
        }
        else if(key->key() == Qt::Key_Escape)
        {
            onBtnCloseClicked();
        }
    }
//    else if(event->type() == QEvent::Close)
//    {
//        //点击按钮进入按钮的槽加个标志位，已经进的不再处理
//        MessageBoxWidget *widget = static_cast<MessageBoxWidget *>(sender());
//        onClose(widget);
//    }
    return false;
}
