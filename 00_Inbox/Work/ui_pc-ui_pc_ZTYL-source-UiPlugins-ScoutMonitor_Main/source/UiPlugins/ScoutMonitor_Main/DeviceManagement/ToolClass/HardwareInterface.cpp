#include "HardwareInterface.h"
#include "ui_HardwareInterface.h"
#include <QFile>

HardwareInterface::HardwareInterface(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::HardwareInterface)
{
    ui->setupUi(this);
}

HardwareInterface::~HardwareInterface()
{
    delete ui;
}

// 初始化界面
void HardwareInterface::initUi(IUiPluginBase *plugin)
{
    m_pPlugin = plugin;
    m_pPlugin->setStyle("day");
}

// 设置图标样式
void HardwareInterface::setIcon(QString strType, QString strStatus)
{
    m_strDeviceType = strType;
    m_strOnlineStatus = strStatus;
    loadQssStyle();
}

// 设置组件名称
void HardwareInterface::setDeviceName(QString strDeviceName)
{
    ui->DeviceName_label->setText(strDeviceName);
}

// 获取组件名称
QString HardwareInterface::getDeviceName()
{
    return ui->DeviceName_label->text();
}

// 设置组件1名称
void HardwareInterface::setDevice1Name(QString strDeviceName)
{
    ui->Dev1_label->setText(strDeviceName);
}

// 获取组件1名称
QString HardwareInterface::getDevice1Name()
{
    return ui->Dev1_label->text();
}

// 设置组件1名称
void HardwareInterface::setDevice1Value(QString strDeviceName)
{
    ui->Dev1_Value_label->setText(strDeviceName);
}

// 获取组件1名称
QString HardwareInterface::getDevice1Value()
{
    return ui->Dev1_Value_label->text();
}

// 设置组件2名称
void HardwareInterface::setDevice2Name(QString strDeviceName)
{
    ui->Dev2_label->setText(strDeviceName);
}

// 获取组件2名称
QString HardwareInterface::getDevice2Name()
{
    return ui->Dev2_label->text();
}

// 设置组件2名称
void HardwareInterface::setDevice2Value(QString strDeviceName)
{
    ui->Dev2_Value_label->setText(strDeviceName);
}

// 获取组件2名称
QString HardwareInterface::getDevice2Value()
{
    return ui->Dev2_Value_label->text();
}

/**
 * @brief hideDevice1Name
 * 隐藏组件1名称
 * @param void
 * @return void
 */
void HardwareInterface::hideDevice1Name()
{
    ui->Dev1_label->hide();
}

/**
 * @brief hideDevice1Value
 * 隐藏组件1名称值
 * @param void
 * @return void
 */
void HardwareInterface::hideDevice1Value()
{
    ui->Dev1_Value_label->hide();
}

void HardwareInterface::hideDevice2Name()
{
    ui->Dev2_label->hide();
}

void HardwareInterface::hideDevice2Value()
{
    ui->Dev2_Value_label->hide();
}

/**
 * @brief hideDeviceIcon
 * 隐藏组件图标
 * @param void
 * @return void
 */
void HardwareInterface::hideDeviceIcon()
{
    ui->HardwareContent_widget->hide();
    ui->HardwareIcon_widget->hide();
}

// 设置组件样式
void HardwareInterface::loadQssStyle()
{
    QString strPath = m_pPlugin->getStylePath() + QString("/") +
            QString(m_strDeviceType) + QString("_") +
            QString(m_strOnlineStatus) + QString(".qss");

    Q_ASSERT(!strPath.isEmpty() && "path is empty");
    QFile textFile(strPath);

    Q_ASSERT(textFile.exists() && "file can't open");
    textFile.open(QIODevice::ReadOnly | QIODevice::Text);
    QTextStream textStream(&textFile);

    QString style = textStream.readAll();
    this->setStyleSheet(style);
}
