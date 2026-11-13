#include "ModelItemWdiget.h"
#include "ui_ModelItemWdiget.h"
#include <QFile>

ModelItemWdiget::ModelItemWdiget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::ModelItemWdiget)
{
    ui->setupUi(this);
}

ModelItemWdiget::~ModelItemWdiget()
{
    delete ui;
}

/**
 * @brief initData
 * 插件数据初始化交互
 * @param void
 * @return 无返回值
 */
void ModelItemWdiget::initUi(IUiPluginBase *plugin)
{
    m_pPlugin = plugin;
    m_pPlugin->setStyle("day");

    connect(ui->Operator_pushButton,SIGNAL(clicked()),this,SLOT(slotStartUseModel()));
    loadQssStyle();
}

/**
 * @brief loadQssStyle
 * 加载Qss样式
 * @param void
 * @return 无返回值
 */
void ModelItemWdiget::loadQssStyle()
{
    QString strPath = m_pPlugin->getStylePath() + QString("/ModelItemWdiget.qss");

    Q_ASSERT(!strPath.isEmpty() && "path is empty");
    QFile textFile(strPath);

    Q_ASSERT(textFile.exists() && "file can't open");
    textFile.open(QIODevice::ReadOnly | QIODevice::Text);
    QTextStream textStream(&textFile);

    QString style = textStream.readAll();
    this->setStyleSheet(style);
}

void ModelItemWdiget::slotStartUseModel()
{
    emit signalUseModel(m_structModelData);
}

/**
 * @brief setModelItemListInfo
 * 设置模型算法子项详情信息
 * @param dData（详情信息内容）
 * @return 无返回值
 */
void ModelItemWdiget::setModelItemListInfo(ModelData dData)
{
    ui->ModelName_label->setText(dData.strModelName);
    ui->CreateTime_label->setText(dData.strCreateTime);
    ui->SerialNumber_label->setText(QString::number(dData.nSerialNumber));

    m_structModelData.strModelName = dData.strModelName;
    m_structModelData.strCreateTime = dData.strCreateTime;
    m_structModelData.nSerialNumber = dData.nSerialNumber;
    m_structModelData.m_Status = dData.m_Status;
}

/**
 * @brief getModelData
 * 获取Item对象中存储的数据
 * @param void
 * @return ModelData(模型算法子项详情属性)
 */
ModelData ModelItemWdiget::getModelData()
{
    return m_structModelData;
}
