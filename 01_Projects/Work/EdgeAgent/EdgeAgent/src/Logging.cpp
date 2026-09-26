/**
 * @file Logging.cpp
 * @brief 结构化日志实现。
 * @author xubb
 */

#include "Logging.h"

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/spdlog.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>

#include <cstdio>

namespace EdgeAgent::logging {

namespace {

QMutex g_mutex; ///< 保护日志器初始化过程。
const char *kFileLoggerName = "edgeagent.file"; ///< 文件日志器名称。

/**
 * @brief 计算默认日志文件路径。
 * @return 环境变量指定的路径，或程序目录下的 logs/edgeagent.log。
 */
QString defaultFilePath()
{
    const QByteArray configuredPath = qgetenv("EDGEAGENT_LOG_FILE"); // 读取外部指定的日志文件路径。
    if (!configuredPath.isEmpty()) {
        return QString::fromUtf8(configuredPath);
    }

    const QString applicationPath = QCoreApplication::instance() == nullptr
        ? QDir::currentPath()
        : QCoreApplication::applicationDirPath(); // 优先使用可执行文件所在目录。
    return QDir(applicationPath).filePath(QStringLiteral("logs/edgeagent.log"));
}

} // 匿名命名空间

void initialize(const QString &filePath)
{
    // 只创建一次日志器，避免重复初始化导致名称冲突。
    if (spdlog::get("edgeagent") == nullptr) {
        auto logger = spdlog::stdout_logger_mt("edgeagent"); // 普通日志输出到标准输出。
        logger->set_pattern("%v");
        spdlog::set_default_logger(logger);
    }
    if (spdlog::get("edgeagent.error") == nullptr) {
        auto logger = spdlog::stderr_logger_mt("edgeagent.error"); // 错误日志输出到标准错误。
        logger->set_pattern("%v");
    }
    if (spdlog::get(kFileLoggerName) == nullptr) {
        QMutexLocker locker(&g_mutex); // 避免 MQTT 回调线程重复创建文件日志器。
        if (spdlog::get(kFileLoggerName) == nullptr) {
            const QString path = filePath.isEmpty() ? defaultFilePath() : filePath; // 确定最终日志文件路径。
            const QFileInfo fileInfo(path); // 从日志路径中提取父目录。
            if (QDir().mkpath(fileInfo.absolutePath())) { // 日志目录不存在时自动创建。
                try {
                    const auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                        path.toUtf8().toStdString(), 10 * 1024 * 1024, 5); // 单文件 10 MB，最多保留 5 个历史文件。
                    auto logger = std::make_shared<spdlog::logger>(kFileLoggerName, sink); // 创建文件日志器。
                    logger->set_pattern("%v");
                    logger->flush_on(spdlog::level::info); // 每条日志立即刷新，避免异常退出时丢失。
                    spdlog::register_logger(logger);
                } catch (const std::exception &exception) {
                    std::fprintf(stderr, "日志文件初始化失败：%s\n", exception.what());
                }
            } else {
                std::fprintf(stderr, "日志目录创建失败：%s\n", path.toUtf8().constData());
            }
        }
    }
    spdlog::set_level(spdlog::level::info);
}

void write(const nlohmann::json &event, bool errorStream)
{
    initialize();
    const std::string line = event.dump(); // 将 JSON 对象序列化为单行文本。
    if (errorStream) {
        spdlog::get("edgeagent.error")->error(line);
    } else {
        spdlog::get("edgeagent")->info(line);
    }
    if (const auto fileLogger = spdlog::get(kFileLoggerName)) {
        fileLogger->info(line); // 文件统一记录普通日志和错误日志，便于完整追溯。
    }
}

} // 命名空间 EdgeAgent::logging
