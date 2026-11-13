#include "MianBoardWidget.h"
#include "ui_MianBoardWidget.h"
#include "IUiPluginBase.h"
#include <QFile>
#include <QDebug>
#include <QPushButton>
#include <QScreen>
#include "UiPluginFactory.h"
#include <QDateTime>
#include "FrameLogManager.h"


MianBoardWidget::MianBoardWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::MianBoardWidget)
{
    ui->setupUi(this);
    this->setWindowFlags(this->windowFlags() | Qt::FramelessWindowHint);
    qApp->installEventFilter(this);
    slotDeviceStatusShow(false);
}

MianBoardWidget::~MianBoardWidget()
{
    delete ui;
}

void MianBoardWidget::init(IUiPluginBase *pPlugin)
{
    m_pPlugin = pPlugin;

    ui->offline->hide();
    ui->online->hide();
}

void MianBoardWidget::initData() {

    //刷新时间
    m_pTimer = new QTimer(this);
    connect(m_pTimer,&QTimer::timeout,this,[=](){
        QDateTime curTime = QDateTime::currentDateTime();
        ui->time_label->setText(curTime.toString("HH:mm:ss"));
        ui->date_label->setText(curTime.toString("yyyy/MM/dd"));
    });
    m_pTimer->start(100);

    connect(ui->quit_btn,&QPushButton::clicked,this,[=](){
        emit signalMessageText(QString::fromUtf8(u8"退出软件提示"),
                               QString::fromUtf8(u8"分布式图像采集与分发软件！"),
                               QString::fromUtf8(u8"提示"),
                               QString::fromUtf8(u8"退出软件"),
                               QString::fromUtf8(u8"继续运行"));
    });

    connect(ui->min_btn,&QPushButton::clicked,this,[=](){
       this->showMinimized();
    });

    connect(ui->pushButton_device,&QPushButton::clicked,this,[=](){
        const QPoint globalBtn = ui->pushButton_device->mapToGlobal(QPoint(0, ui->pushButton_device->height()));
        emit signalQPointToParameterSet(globalBtn);
    });







}
void MianBoardWidget::resetQss(const QString &strStyleName)
{
    m_pPlugin->setStyle(strStyleName);
    loadQss();
}

//加载qss
void MianBoardWidget::loadQss()
{

}
void MianBoardWidget::slotTimeout()
{


}
void MianBoardWidget::resizeEvent(QResizeEvent *event)
{
     this->setWindowState(Qt::WindowFullScreen);
}

bool MianBoardWidget::eventFilter(QObject *watched, QEvent *event)
{
    if(event->type() == QEvent::KeyRelease && watched->isWidgetType())
    {
        QKeyEvent *key = (QKeyEvent *)event;
        if((key->modifiers() == Qt::ControlModifier && key->key() == Qt::Key_Q) || (key->modifiers() == Qt::AltModifier && key->key() == Qt::Key_F4))
        {
            QLibrary lib(QApplication::applicationDirPath() + "/CenterFrame.dll");
            typedef void (*InitFunc)();
            InitFunc initFun = (InitFunc)lib.resolve("uninit");
            if(initFun)
            {
                initFun();
            }
            qApp->quit();
        }
    }
    return false;
}

void MianBoardWidget::registerProtocols()
{

    std::vector<IUiPluginBase *> vPlugins = UiPluginFactory::getInstance()->getPlugins("MessageBox");
    foreach (IUiPluginBase * plugin, vPlugins) {
        connect(this,SIGNAL(signalHintMessage(QString, QString, MessageType)),plugin->getWidget(),SLOT(onMessageText(QString,QString,MessageType)));
        connect(this,SIGNAL(signalMessageText(QString,QString,QString,QString,QString)),plugin->getWidget(),SLOT(onMessageText(QString,QString,QString,QString,QString)));
        connect(plugin->getWidget(),SIGNAL(signalConfirmBtnClicked(QString)),this,SLOT(sltConfirmClicked(QString)));
    }

    vPlugins = UiPluginFactory::getInstance()->getPlugins("SignalTransit");
    foreach (IUiPluginBase * plugin, vPlugins) {
        connect(plugin->getWidget(),SIGNAL(btnClicked(QString)),this,SLOT(onBtnClicked(QString)));
        connect(plugin->getWidget(),SIGNAL(signalWGState(bool *)),this,SLOT(sltWGState(bool *)));

        connect(this,SIGNAL(signalPluginMsg(pluginmsg)),plugin->getWidget(),SIGNAL(signalPluginMsg(pluginmsg)));   //发送
        connect(plugin->getWidget(),SIGNAL(signalPluginMsg(pluginmsg)),this,SLOT(slotsignalPluginMsg(pluginmsg))); //接收
    }

    vPlugins = UiPluginFactory::getInstance()->getPlugins("DeviceManagement");
    foreach (IUiPluginBase * plugin, vPlugins) {
        connect(plugin->getWidget(),SIGNAL(signalDeviceStatusShow(bool)),this,SLOT(slotDeviceStatusShow(bool)));

    }

    vPlugins = UiPluginFactory::getInstance()->getPlugins("DeviceManagement");
    foreach (IUiPluginBase * plugin, vPlugins) {
        connect(plugin->getWidget(),SIGNAL(signalDeviceStatusShow(bool)),this,SLOT(slotDeviceStatusShow(bool)));
        connect(plugin->getWidget(),SIGNAL(signalHDMIINStatusShow(bool)),this,SLOT(slotHDMIINStatusShow(bool)));
    }

    vPlugins = UiPluginFactory::getInstance()->getPlugins("ParameterSet");
    foreach (IUiPluginBase * plugin, vPlugins) {
        connect(plugin->getWidget(),SIGNAL(signalZLMParPushStatus(int,QString)),this,SLOT(slotPushVideoStatusShow(int,QString)));
        connect(this,SIGNAL(signalQPointToParameterSet(QPoint)),plugin->getWidget(),SLOT(slotRecQPointFromMianBoard(QPoint)));
    }
    vPlugins = UiPluginFactory::getInstance()->getPlugins("VideoProcessFFmpeg");
    foreach (IUiPluginBase * plugin, vPlugins) {
        connect(plugin->getWidget(),SIGNAL(signalPullVideoStatus(VideoStatus,QString)),this,SLOT(slotPullVideoStatusShow(VideoStatus,QString)));
    }


}


//确认按钮点击
void MianBoardWidget::sltConfirmClicked(QString text)
{

    if(text == QString::fromUtf8(u8"退出软件提示"))
    {
        QLibrary lib(QApplication::applicationDirPath() + "/CenterFrame");
        typedef void (*InitFunc)();
        InitFunc initFun = (InitFunc)lib.resolve("uninit");
        if(initFun)
        {
            initFun();
        }
        qApp->quit();
    }

}

//设备
void MianBoardWidget::slotDeviceStatusShow(bool online)
{
    if(m_DeviceStatus==online)
        return;

    m_DeviceStatus=online;

    if(online)
    {
//        ui->offline->hide();
//        ui->online->show();

        //设置视频设备在线状态
        ui->label_sb->setStyleSheet("QLabel#label_sb{border-image: url(:/resource/day/step0_0.png);}");
        ui->label_line0->setStyleSheet("QLabel#label_line0{background-color:#30E0A1;}");


    }else {
//        ui->offline->show();
//        ui->online->hide();

        //设置视频设备离线状态
        ui->label_sb->setStyleSheet("QLabel#label_sb{border-image: url(:/resource/day/step0_unfinish.png);}");
        ui->label_cj->setStyleSheet("QLabel#label_cj{border-image: url(:/resource/day/step1_unfinish.png);}");
        ui->label_cl->setStyleSheet("QLabel#label_cl{border-image: url(:/resource/day/step2_unfinish.png);}");
        ui->label_xs->setStyleSheet("QLabel#label_cl{border-image: url(:/resource/day/step3_unfinish.png);}");

        ui->label_line0->setStyleSheet("background-color:#B3BFCF;");
        ui->label_line1->setStyleSheet("background-color:#B3BFCF;");
        ui->label_line2->setStyleSheet("background-color:#B3BFCF;");
        ui->label_cl->setToolTip("");
        ui->label_xs->setToolTip("");

        m_DealStatus=false;
        m_HDMIINstatus=false;
        m_pullVideoStatus=-1;
        m_pushVideoStatus=-1;

    }
}

//采集
void MianBoardWidget::slotHDMIINStatusShow(bool online)
{

    if(m_HDMIINstatus==online)
        return;


    m_HDMIINstatus=online;

    if(online)
    {
        //设置视频采集在线状态
        ui->label_cj->setStyleSheet("QLabel#label_cj{border-image: url(:/resource/day/step1_0.png);}");
        ui->label_line1->setStyleSheet("QLabel#label_line1{background-color:#30E0A1;}");
    }else {
        //设置视频采集离线状态
        ui->label_cj->setStyleSheet("QLabel#label_cj{border-image: url(:/resource/day/step1_unfinish.png);}");
        ui->label_cl->setStyleSheet("QLabel#label_cl{border-image: url(:/resource/day/step2_unfinish.png);}");
        ui->label_xs->setStyleSheet("QLabel#label_cl{border-image: url(:/resource/day/step3_unfinish.png);}");
        ui->label_line1->setStyleSheet("background-color:#B3BFCF;");
        ui->label_line2->setStyleSheet("background-color:#B3BFCF;");
        m_DealStatus=false;
        m_pullVideoStatus=-1;
        m_pushVideoStatus=-1;
    }
}

//处理
void MianBoardWidget::slotPushVideoStatusShow(int status,QString reason)
{
    FrameLogManager::getInstance()->log(QString("MianBoard"),QString("CL slotPushVideoStatusShow status:%1,reason:%2").arg(QString::number(status))
                                        .arg(reason),IFrameLogManager::ERROR_TYPE);
    if(!m_HDMIINstatus)
        return;

    m_DealStatus=true;
    if(m_pushVideoStatus!=status || m_pushVideoreason!=reason){
        m_pushVideoStatus=status;
        ui->label_cl->setToolTip("");
        ui->label_line2->setStyleSheet("background-color:#B3BFCF;");
        switch (status) {
        case 0:
            ui->label_cl->setStyleSheet("QLabel#label_cl{border-image: url(:/resource/day/step2_0.png);}");
            ui->label_line2->setStyleSheet(R"(background-color:#30E0A1;)");
            break;
        case 1:
            ui->label_cl->setStyleSheet("QLabel#label_cl{border-image: url(:/resource/day/step2_1.png);}");
            break;
        case 2:
            ui->label_cl->setStyleSheet("QLabel#label_cl{border-image: url(:/resource/day/step2_2.png);}");
            ui->label_cl->setToolTip(reason);
            m_DealStatus=false;
            break;
        default:
            ui->label_cl->setStyleSheet("QLabel#label_cl{border-image: url(:/resource/day/step2_unfinish.png);}");
            ui->label_xs->setStyleSheet("QLabel#label_cl{border-image: url(:/resource/day/step3_unfinish.png);}");

            ui->label_line2->setStyleSheet("background-color:#B3BFCF;");
            ui->label_cl->setToolTip("");
            ui->label_xs->setToolTip("");

            m_DealStatus=false;
            m_pullVideoStatus=-1;
            break;
        }
    }


}

//显示
void MianBoardWidget::slotPullVideoStatusShow(VideoStatus status, QString reason)
{
    FrameLogManager::getInstance()->log(QString("MianBoard"),QString("XS slotPullVideoStatusShow status:%1,reason:%2").arg(QString::number(status))
                                        .arg(reason),IFrameLogManager::ERROR_TYPE);
    if(!m_DealStatus)
        return;

    if(m_pullVideoStatus!=status || m_pullVideoreason!=reason){
        m_pullVideoStatus=status;
        m_pullVideoreason=reason;
        ui->label_xs->setToolTip("");
        switch (status) {
        case Status_Playing:
            ui->label_xs->setStyleSheet(QString("QLabel#label_xs{border-image:url(:/resource/day/step3_%1.png);}").arg(0));
            break;
        case Status_Connecting:
            ui->label_xs->setStyleSheet(QString("QLabel#label_xs{border-image:url(:/resource/day/step3_%1.png);}").arg(1));
            break;
        case Status_Reconnecting:
            ui->label_xs->setStyleSheet("QLabel#label_xs{border-image: url(:/resource/day/step3_unfinish.png);}");
            break;
        case Status_Failed:
            ui->label_xs->setToolTip(reason);
            ui->label_xs->setStyleSheet(QString("QLabel#label_xs{border-image:url(:/resource/day/step3_%1.png);}").arg(2));
            break;
        default:
            break;
        }
    }
}

void MianBoardWidget::showEvent(QShowEvent *event)
{
    if(!isMinimized())
    {
        this->setAttribute(Qt::WA_Mapped);
    }
    QWidget::showEvent(event);
}
