/**
 * @file main.cpp
 * @brief EdgeAgent 程序入口。
 * @author xubb
 */

#include "Config.h"
#include "EdgeAgent.h"
#include "Logging.h"

#include <nlohmann/json.hpp>

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTimer>

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv); // 创建无界面的 Qt 应用程序对象。
    QCoreApplication::setApplicationName(QStringLiteral("edgeagent"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    EdgeAgent::logging::initialize(); // 初始化控制台和文件日志，默认写入程序目录下的 logs/edgeagent.log。

    QCommandLineParser parser; // 统一解析命令行参数。
    parser.setApplicationDescription(QStringLiteral("EdgeAgent UDP、TCP、串口和 CAN 通讯代理"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption configOption(QStringList() << QStringLiteral("c") << QStringLiteral("config"), // 配置文件命令行选项。
                                           QStringLiteral("配置文件路径"),
                                           QStringLiteral("路径"));
    const QCommandLineOption runForOption(QStringLiteral("run-for-ms"), // 自动退出命令行选项。
                                           QStringLiteral("运行指定毫秒后退出，用于冒烟测试"),
                                           QStringLiteral("毫秒数"));
    parser.addOption(configOption);
    parser.addOption(runForOption);
    parser.process(application);

    QString configPath = parser.value(configOption); // 优先使用命令行指定的配置路径。
    if (configPath.isEmpty()) {
        configPath = qEnvironmentVariable("EDGEAGENT_CONFIG");
    }
    if (configPath.isEmpty()) {
        configPath = application.applicationDirPath() + QStringLiteral("/config.json");
    }

    EdgeAgent::AgentConfig config; // 配置加载成功后的运行时配置。
    QString error; // 配置加载失败时的中文错误信息。
    if (!EdgeAgent::loadConfig(configPath, &config, &error)) {
        EdgeAgent::logging::write({{"event", "configuration_error"}, {"message", error.toStdString()}}, true);
        return 2;
    }

    EdgeAgent::EdgeAgent agent(config); // 创建主协调器并交给 Qt 事件循环管理。
    QObject::connect(&application, &QCoreApplication::aboutToQuit, &agent, &EdgeAgent::EdgeAgent::stop);
    agent.start();

    bool runForOk = false; // 记录 --run-for-ms 是否成功转换为整数。
    const int runForMs = parser.value(runForOption).toInt(&runForOk); // 自动退出等待时间。
    if (!parser.value(runForOption).isEmpty() && (!runForOk || runForMs < 1)) {
        EdgeAgent::logging::write({{"event", "argument_error"},
                                   {"message", "--run-for-ms 必须是正整数"}}, true);
        return 2;
    }
    if (runForOk) {
        QTimer::singleShot(runForMs, &application, &QCoreApplication::quit);
    }

    return application.exec();
}
