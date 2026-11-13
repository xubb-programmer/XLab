#include "PullButtonUiWidget.h"
#include "ui_PullButtonUiWidget.h"
#include "UiPluginFactory.h"
#include <QFile>
#include <QTextCodec>
#include <QScreen>

PullButtonUiWidget::PullButtonUiWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::PullButtonUiWidget)
{
    ui->setupUi(this);
}

PullButtonUiWidget::~PullButtonUiWidget()
{
    delete ui;
}

/**
 * @brief initUi
 * 界面初始化函数。
 * @param pPlugin 界面框架指针
 * @return 无返回值
 */
void PullButtonUiWidget::initUi(IUiPluginBase *pPlugin)
{
    m_pPlugin = pPlugin;
    m_pPlugin->setStyle("day");

    connect(ui->ShowAndHide_Btn,SIGNAL(clicked()),this,SLOT(slotShowAndHide_Btn()));

    loadQssStyleFile();
}

/**
 * @brief initData
 * 插件数据初始化函数。
 * @param void
 * @return 无返回值
 */
void PullButtonUiWidget::initData()
{
    ;
}

/**
 * @brief resetQss
 * 切肤函数。
 * @param void
 * @return 无返回值
 */
void PullButtonUiWidget::resetQss(const QString& strStyleName)
{
    //设置换肤路径
    m_pPlugin->setStyle(strStyleName);
    loadQssStyleFile();
}

/**
 * @brief registerProtocols
 * 虚函数的具体实现，初始化插件信号注册函数。
 * @param void
 * @return 无返回值
 */
void PullButtonUiWidget::registerProtocols()
{
    std::vector<IUiPluginBase *> vPlugins = UiPluginFactory::getInstance()->getPlugins("TargetTable");
    foreach (IUiPluginBase * plugin, vPlugins) {
        connect(this,SIGNAL(signalShowTable(bool)),plugin->getWidget(),SLOT(slotShowTable(bool)));
    }
}

/**
 * @brief loadQssStyleFile
 * 设置主设置界面Qss样式。
 * @param void
 * @return 无返回值
 */
void PullButtonUiWidget::loadQssStyleFile()
{
    QString strPath = m_pPlugin->getStylePath() + QString("/PullButtonUiWidget.qss");

    Q_ASSERT(!strPath.isEmpty() && "path is empty");
    QFile textFile(strPath);

    Q_ASSERT(textFile.exists() && "file can't open");
    textFile.open(QIODevice::ReadOnly | QIODevice::Text);
    QTextStream textStream(&textFile);

    QString style = textStream.readAll();
    this->setStyleSheet(style);
}

/**
 * @brief slotShowAndHide_Btn
 * 点击目标列表显隐按钮触发的槽函数。
 * @param void
 * @return 无返回值
 */
void PullButtonUiWidget::slotShowAndHide_Btn()
{
    QScreen *screen =QApplication::primaryScreen();
    QSize screenSize =screen->size();
    if(m_bHide)
    {
        // 显示目标列表
        m_bHide = false;
        this->move((screenSize.width()-(250*(((double)(screenSize.width()-768)/screenSize.width())+1)))-width(),
                        (screenSize.height()-132)/2-height()/2);
        emit signalShowTable(true);

        // 设置按钮样式
        ui->ShowAndHide_Btn->setStyleSheet(
                    "QPushButton#ShowAndHide_Btn{border-image: url(:/resource/target-iconBtn_show-normal.png);}"
                    "QPushButton#ShowAndHide_Btn::hover{border-image: url(:/resource/target-iconBtn_show-hover.png);}"
                    "QPushButton#ShowAndHide_Btn::pressed{border-image: url(:/resource/target-iconBtn_show-pressed.png);}");
    }
    else
    {
        // 隐藏目标列表
        m_bHide = true;
        this->move(screenSize.width()-width(),
                        (screenSize.height()-132)/2-height()/2);
        emit signalShowTable(false);

        // 设置按钮样式
        ui->ShowAndHide_Btn->setStyleSheet(
                    "QPushButton#ShowAndHide_Btn{border-image: url(:/resource/target-iconBtn_hide-normal.png);}"
                    "QPushButton#ShowAndHide_Btn::hover{border-image: url(:/resource/target-iconBtn_hide-hover.png);}"
                    "QPushButton#ShowAndHide_Btn::pressed{border-image: url(:/resource/target-iconBtn_hide-pressed.png);}");
    }
}
