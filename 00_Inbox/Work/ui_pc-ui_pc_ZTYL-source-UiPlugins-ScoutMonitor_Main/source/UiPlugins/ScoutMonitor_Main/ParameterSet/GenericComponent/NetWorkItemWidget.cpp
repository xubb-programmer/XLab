#include "NetWorkItemWidget.h"
#include "ui_NetWorkItemWidget.h"
#include "FrameLogManager.h"
#include <QSettings>
#include <QTextCodec>
#include <QJsonArray>
#include <QFile>


NetWorkItemWidget::NetWorkItemWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::NetWorkItemWidget)
{
    ui->setupUi(this);
}

NetWorkItemWidget::~NetWorkItemWidget()
{
    delete ui;
}

/**
 * @brief initData
 * 插件数据初始化交互
 * @param void
 * @return 无返回值
 */
void NetWorkItemWidget::initUi(IUiPluginBase *plugin,int num)
{
    m_pPlugin = plugin;
    m_pPlugin->setStyle("day");

    ui->label_name->setText(QString::fromLocal8Bit("网口")+QString::number(num));
    loadQssStyle(0);
    initData();

//    // 文本框限制输入字符
//      QRegExp reg("[0-9]+.");
//      QValidator *validator = new QRegExpValidator(reg);
//      ui->lineEdit_pagennum10->setValidator(validator);
}

/**
 * @brief initCfg
 * 读取配置文件获取发送时的基础UI
 * @param void
 * @return 无返回值
 */
void NetWorkItemWidget::initCfg()
{
    QSettings settings(QApplication::applicationDirPath() + "/config/communication.ini" , QSettings::IniFormat);
    settings.setIniCodec(QTextCodec::codecForName("GBK"));
    settings.beginGroup("HTTP");
    settings.endGroup();

}

/**
 * @brief initData
 * 插件数据初始化交互
 * @param void
 * @return 无返回值
 */
void NetWorkItemWidget::initData()
{
    // 连接信号槽
    connect(ui->pushButton_send,SIGNAL(clicked(bool)),this,SLOT(slotNetSet_pushButton(bool)));
//    connect(this, &ParaContentWidget::signalPosPullStreamPara, m_worker, &HttpWorker::doPospullStreamParaWork);
//    connect(m_worker, &HttpWorker::requestFinishedForPullStreamPara, this, &ParaContentWidget::slotFinishedForPullStreamPara);
}


/**
 * @brief loadQssStyle
 * 设置界面Qss初始化样式表
 * @param void
 * @return void
 */
void NetWorkItemWidget::loadQssStyle(int status)
{
    QString strPath;
    if(status){
         strPath = m_pPlugin->getStylePath() + QString("/NetWorkItemWidgetOnline.qss");
    }else{
         strPath = m_pPlugin->getStylePath() + QString("/NetWorkItemWidgetOffline.qss");
    }


    Q_ASSERT(!strPath.isEmpty() && "path is empty");
    QFile textFile(strPath);

    Q_ASSERT(textFile.exists() && "file can't open");
    textFile.open(QIODevice::ReadOnly | QIODevice::Text);
    QTextStream textStream(&textFile);

    QString style = textStream.readAll();
    this->setStyleSheet(style);
}

/**
 * @brief updateStatu 更新网口在线离线状态
 * @param netdata 状态数据
 */
void NetWorkItemWidget::updateStatu(NetSetData netdata)
{

    if(netdata.status){//    在线
         loadQssStyle(1);
        ui->lineEdit_ip->setText(netdata.ip);
        ui->lineEdit_subnet->setText(netdata.subnet);
        ui->lineEdit_gateway->setText(netdata.gateway);
        ui->pushButton_send->setEnabled(true);
        ui->pushButton_send->setCheckable(true);
//        ui->pushButton_send->setStyleSheet(QString::fromLocal8Bit("font:20px Alibaba PuHuiTi 2.0;color:white;border-image: url(:/resource/btn-下发-normal.png);"));
        ui->lineEdit_ip->setEnabled(true);
        ui->lineEdit_subnet->setEnabled(true);
        ui->lineEdit_gateway->setEnabled(true);

    }else{
        //    离线
        ui->lineEdit_ip->setText(netdata.ip);
        ui->lineEdit_subnet->setText(netdata.subnet);
        ui->lineEdit_gateway->setText(netdata.gateway);
        ui->pushButton_send->setEnabled(false);
        ui->lineEdit_ip->setEnabled(false);
        ui->lineEdit_subnet->setEnabled(false);
        ui->lineEdit_gateway->setEnabled(false);
        ui->pushButton_send->setStyleSheet(QString::fromLocal8Bit("font:20px Alibaba PuHuiTi 2.0;color:white;border-image: url(:/resource/btn-下发-disable.png);"));
        loadQssStyle(0);
    }



}

/**
 * @brief signalSendNetsetData 下发网络设置参数
 * @param netdata 网络设置参数
 */
void NetWorkItemWidget::slotNetSet_pushButton(bool flag)
{


    // 获取输入数据
    QString ipAddress = ui->lineEdit_ip->text().trimmed();
    QString netmask = ui->label_subnet->text().trimmed();
    QString gateway = ui->label_gateway->text().trimmed();

//    // 验证输入
//    if (ipAddress.isEmpty() || netmask.isEmpty() || gateway.isEmpty()) {
//        QMessageBox::warning(this, "验证失败", "IP地址和子网掩码不能为空");
//        return;
//    }

    NetSetData netdata;
    netdata.netName=ui->label_name->text().trimmed();
    netdata.ip=ui->lineEdit_ip->text().trimmed();
    netdata.subnet=ui->lineEdit_subnet->text().trimmed();
    netdata.gateway=ui->lineEdit_gateway->text().trimmed();
    emit signalSendNetsetData(netdata);
}
