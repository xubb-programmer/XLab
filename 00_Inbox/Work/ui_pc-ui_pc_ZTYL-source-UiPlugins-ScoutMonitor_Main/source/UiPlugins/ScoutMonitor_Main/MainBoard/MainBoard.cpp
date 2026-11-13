#include "MainBoard.h"


MainBoard* gContent = nullptr;
MainBoard::MainBoard(QWidget *pParent)
{
    m_pWidget = new MianBoardWidget(pParent);
}

QWidget *MainBoard::getWidget()
{
    return m_pWidget;
}

void MainBoard::resetQss(const QString &strStyleName)
{

    m_pWidget->resetQss(strStyleName);
}

//初始化数据
void MainBoard::initData()
{
    m_pWidget->initData();
}

void MainBoard::initUi()
{

    m_pWidget->init(this);
    m_pWidget->show();



}

void MainBoard::close()
{
    //delete m_pWidget;
}

void MainBoard::registerProtocols()
{

    m_pWidget->registerProtocols();
}

QString MainBoard::version()
{
    return QString::fromLocal8Bit("1.0");
}

QString MainBoard::pluginName()
{
    return QString::fromLocal8Bit("MainBoard");
}

QString MainBoard::introduction()
{
    return QString::fromLocal8Bit("这是一个主界面底版插件");
}

QString MainBoard::updateDescribe()
{
    return QString::fromLocal8Bit("无");
}

IUiPluginBase *installUiPlugin(QWidget *pParent)
{
    gContent = new MainBoard(pParent);
    return gContent;
}
