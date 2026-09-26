/**
 * @file main.cpp
 * @brief EdgeAgent 可视化测试客户端程序入口。
 * @author xubb
 */

#include "TestMainWindow.h"

#include <QApplication>
#include <QStyleFactory>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv); // 创建 Qt Widgets 应用程序。
    application.setApplicationName(QStringLiteral("EdgeAgentTester"));
    application.setApplicationVersion(QStringLiteral("0.1.0"));
    application.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    edgeagent::TestMainWindow window; // 创建可视化测试窗口。
    window.show();
    return application.exec();
}
