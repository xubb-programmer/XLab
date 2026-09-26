/**
 * @file Logging.h
 * @brief 结构化日志接口。
 * @author xubb
 */

#pragma once

#include <nlohmann/json.hpp>

#include <QString>

namespace EdgeAgent::logging {

/**
 * @brief 初始化控制台和文件日志器。
 * @param filePath 日志文件路径；为空时读取 EDGEAGENT_LOG_FILE，未设置时使用程序目录下的 logs/edgeagent.log。
 */
void initialize(const QString &filePath = {});

/**
 * @brief 同时输出一条 JSON 结构化日志到控制台和日志文件。
 * @param event 日志对象。
 * @param errorStream 是否输出到标准错误。
 */
void write(const nlohmann::json &event, bool errorStream = false);

} // 命名空间 EdgeAgent::logging
