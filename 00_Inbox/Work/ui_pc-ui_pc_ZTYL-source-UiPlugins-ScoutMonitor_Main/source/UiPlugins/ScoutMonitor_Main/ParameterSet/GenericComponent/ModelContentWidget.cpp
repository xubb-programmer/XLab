#include "ModelContentWidget.h"
#include "ui_ModelContentWidget.h"
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QDateTime>

ModelContentWidget::ModelContentWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::ModelContentWidget)
{
    ui->setupUi(this);
}

ModelContentWidget::~ModelContentWidget()
{
    delete ui;
}


bool compareTargetNum(ModelItemWdiget* tgt1, ModelItemWdiget* tgt2)
{
    return tgt1->getModelData().nSerialNumber > tgt2->getModelData().nSerialNumber;
}

/**
 * @brief initUi
 * 插件初始化界面
 * @param plugin（插件指针）
 * @return 无返回值
 */
void ModelContentWidget::initUi(IUiPluginBase *plugin)
{
    m_pPlugin = plugin;
    m_pPlugin->setStyle("day");

    connect(ui->UpFile_pushButton,SIGNAL(clicked()),this,SLOT(slotUpFile_pushButton()));

    ui->ModelStatus_pushButton->hide();
    ui->ModelName_label->hide();
    ui->scrollArea->setStyleSheet("QScrollArea {background-color:transparent; border:0px;}"
                                  "QScrollArea QWidget {background-color:transparent;}");

    // 默认隐藏启用标签显示Wdiget
    ui->ModelShow_widget->hide();
    loadQssStyle();
}

/**
 * @brief loadQssStyle
 * 设置组件样式
 * @param void
 * @return 无返回值
 */
void ModelContentWidget::loadQssStyle()
{
    QString strPath = m_pPlugin->getStylePath() + QString("/ModelContentWidget.qss");

    Q_ASSERT(!strPath.isEmpty() && "path is empty");
    QFile textFile(strPath);

    Q_ASSERT(textFile.exists() && "file can't open");
    textFile.open(QIODevice::ReadOnly | QIODevice::Text);
    QTextStream textStream(&textFile);

    QString style = textStream.readAll();
    this->setStyleSheet(style);
}

/**
 * @brief addModelUi
 * 添加模型Item界面
 * @param strData
 * @return 无返回值
 */
void ModelContentWidget::addModelUi(QString strData)
{
    ModelData tempData;
    tempData.strModelName = strData;
    tempData.strCreateTime = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    tempData.nSerialNumber = m_currentModelCnt+1;
    m_currentModelCnt++;
    updateModelItemData(tempData);
}

/**
 * @brief updateModelItemData
 * 刷新模型Item界面列表
 * @param strData
 * @return 无返回值
 */
void ModelContentWidget::updateModelItemData(ModelData dData)
{
    for(int index = 0; index < m_vModelIndoData.size();++index)
    {
        if(m_vModelIndoData[index].strModelName == dData.strModelName)
        {
            m_vModelIndoData[index].nSerialNumber = dData.nSerialNumber;
            m_vModelIndoData[index].strCreateTime = dData.strCreateTime;
            for(int n = 0 ; n != m_listModelItem.size(); ++n)
            {
                m_listModelItem.at(n)->setModelItemListInfo(m_vModelIndoData.at(n));
            }
            updateModelItemUI();
            return;
        }
    }

    m_vModelIndoData.push_back(dData);

    //判断此时视频参数设置列表个数是否超过5个
    if(m_vModelIndoData.size() > MAX_VIDEOSET_NUM)
    {
        //提示不能再进行添加了
    }
    else
    {
        ModelItemWdiget * item = new ModelItemWdiget(ui->scrollAreaWidgetContents);
        item->initUi(m_pPlugin);
        item->setFixedWidth(ui->scrollAreaWidgetContents->width() - m_nScrollOffset);
        // 点击模型启用按钮
        qRegisterMetaType<ModelData>("ModelData");
        connect(item,SIGNAL(signalUseModel(ModelData)),this,SLOT(slotUseModel(ModelData)));

        ModelData tempData;
        tempData.nSerialNumber   = dData.nSerialNumber;
        tempData.strModelName     = dData.strModelName;
        tempData.strCreateTime     = dData.strCreateTime;
        tempData.m_Status     = dData.m_Status;

        item->setModelItemListInfo(tempData);
        item->setVisible(true);
        m_listModelItem.append(item);
    }
    updateModelItemUI();
}

/**
 * @brief updateModelItemUI
 * 刷新模型列表布局
 * @param void
 * @return 无返回值
 */
void ModelContentWidget::updateModelItemUI()
{
    // 界面Item显示前，先根据序号进行排序，然后再依据序号顺序进行显示
    std::sort(m_listModelItem.begin(),m_listModelItem.end(),compareTargetNum);

    int nIndex = 0;
    int height = 0;
    int height_single;
    int y = 0;
    int x = 5;

    for(int n = 0 ; n < m_listModelItem.size(); ++n)
    {
        ModelItemWdiget *item = (ModelItemWdiget *)m_listModelItem.at(m_listModelItem.size()-n-1);
        item->resize(800,60);
        item->move(x, y);
        height_single = item->height();
        y = height + height_single +5;
        ++nIndex;
        height = y;

        ui->Icon_widget->hide();
    }

    if(m_listModelItem.size() == 0)
    {
        ui->Icon_widget->show();
    }

    ui->scrollAreaWidgetContents->setFixedHeight(height);
    ui->scrollArea->setWidget(ui->scrollAreaWidgetContents);
}

/**
 * @brief slotUpFile_pushButton
 * 点击添加选择使用的模型
 * @param void
 * @return 无返回值
 */
void ModelContentWidget::slotUpFile_pushButton()
{
    // 打开文件选择对话框
    QString filePath = QFileDialog::getOpenFileName(
                this,
                QString::fromUtf8(u8"选择文件"),
                QDir::homePath());

    // 检查是否选择了文件
    if(!filePath.isEmpty())
    {
        // 从文件路径中提取文件名
        QFileInfo fileInfo(filePath);
        QString fileName = fileInfo.fileName();

        // 创建模型Item界面
        addModelUi(fileName);
    }
}

/**
 * @brief slotUseModel
 * 点击使用模型
 * @param dData(模型信息)
 * @return 无返回值
 */
void ModelContentWidget::slotUseModel(ModelData dData)
{
    ui->ModelStatus_pushButton->setStyleSheet(
                "QPushButton#ModelStatus_pushButton{border-image: url(:/resource/mark.png);}"
                "QPushButton#ModelStatus_pushButton::hover{border-image: url(:/resource/mark.png);}"
                "QPushButton#ModelStatus_pushButton::pressed{border-image: url(:/resource/mark.png);}"
                );
    ui->ModelName_label->setText(dData.strModelName);

    ui->ModelShow_widget->show();
    ui->ModelStatus_pushButton->show();
    ui->ModelName_label->show();
}
