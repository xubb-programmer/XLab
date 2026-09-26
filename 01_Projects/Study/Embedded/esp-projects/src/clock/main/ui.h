/**
 * @file    ui.h
 * @brief   时钟界面绘制模块(极简风格:纯黑背景 + 7 段数码管时间)
 *
 * 设计原则:
 *   1. 背景只在启动时画一次(纯黑),不再变化
 *   2. 每秒只重画时间区域,画面稳定不闪屏
 */

#ifndef __UI_H__
#define __UI_H__

#include <stdint.h>

/* WiFi 状态(用于右上角状态图标) */
typedef enum {
    WIFI_STATUS_OFF = 0,        /* 未配置 WiFi(灰色) */
    WIFI_STATUS_CONNECTING,     /* 连接中/断线重连中(琥珀色) */
    WIFI_STATUS_CONNECTED,      /* 已连接(绿色) */
} wifi_status_t;

/**
 * @brief   初始化界面:把整个屏幕清成纯黑背景(启动时调用一次)
 */
void ui_init(void);

/**
 * @brief   刷新 7 段数码管时间(每秒调用)
 * @param   time_str  格式为 "HH:MM:SS" 的字符串
 */
void ui_update_time(const char *time_str);

/**
 * @brief   更新右上角的 WiFi 状态图标(只在状态变化时调用)
 * @param   status  当前 WiFi 状态
 */
void ui_set_wifi_status(wifi_status_t status);

#endif /* __UI_H__ */
