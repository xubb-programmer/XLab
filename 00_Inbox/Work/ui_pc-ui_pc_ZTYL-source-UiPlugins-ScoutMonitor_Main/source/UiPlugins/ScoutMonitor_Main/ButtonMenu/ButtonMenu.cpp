/*
* ButtonMenu.cpp
* Author：王泽文
* Created：2025-07-29
* Last Modified：2025-07-29
* Description：用于界面集成框架虚函数的具体实现。
*/
#include "ButtonMenu.h"
#include <QScreen>
#define BTNMENWIDGET  48

ButtonMenu* gContent = nullptr;
ButtonMenu::ButtonMenu(QWidget *pParent)
{
    m_pWidget = new ButtonMenuWidget(pParent);
    m_pWidget->show();

}

QWidget *ButtonMenu::getWidget()
{
    return m_pWidget;
}

void ButtonMenu::resetQss(const QString &strStyleName)
{
    m_pWidget->resetQss(strStyleName);
}

//初始化数据
void ButtonMenu::initData()
{
    m_pWidget->initData();
}

void ButtonMenu::initUi()
{
    m_pWidget->initUi(this);
    m_pWidget->show();

    QScreen *screen =QApplication::primaryScreen();
    QSize screenSize =screen->size();
    m_pWidget->resize(screenSize.width(),BTNMENWIDGET);
    m_pWidget->move  (0,screenSize.height()-BTNMENWIDGET);
    m_pWidget->update();
}

void ButtonMenu::close()
{
    //delete m_pWidget;
}

void ButtonMenu::registerProtocols()
{
    m_pWidget->registerProtocols();
}

QString ButtonMenu::version()
{
    return QString::fromLocal8Bit("1.0");
}

QString ButtonMenu::pluginName()
{
    return QString::fromLocal8Bit("ButtonMenu");
}

QString ButtonMenu::introduction()
{
    return QString::fromLocal8Bit("情报软件一级菜单");
}

QString ButtonMenu::updateDescribe()
{
    return QString::fromLocal8Bit("无");
}

IUiPluginBase *installUiPlugin(QWidget *pParent)
{
    gContent = new ButtonMenu(pParent);
    return gContent;
}
