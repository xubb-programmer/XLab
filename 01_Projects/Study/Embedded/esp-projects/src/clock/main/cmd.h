/**
 * @file    cmd.h
 * @brief   串口命令模块:在串口监视器里输入命令,配置设备
 *
 * 支持命令:
 *   help                    显示帮助
 *   wifi                    查看已保存的 WiFi 配置
 *   wifi <SSID> <密码>      保存 WiFi 配置到 NVS 并重启联网
 *   wifi clear              清除 NVS 中的 WiFi 配置(之后使用代码里写死的默认值)
 */

#ifndef __CMD_H__
#define __CMD_H__

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief   创建串口命令处理任务(在 app_main 里调用一次)
 */
void cmd_task_start(void);

/**
 * @brief   从 NVS 读取 WiFi 配置
 * @param   ssid      存放 SSID 的缓冲区
 * @param   ssid_len  缓冲区大小
 * @param   pass      存放密码的缓冲区
 * @param   pass_len  缓冲区大小
 * @retval  true 读取成功 / false 没有保存过
 */
bool wifi_cfg_load(char *ssid, size_t ssid_len, char *pass, size_t pass_len);

#endif /* __CMD_H__ */
