/**
 * @file    main.c
 * @brief   GPIO 入门练习:按键(中断)控制 LED
 *
 * 硬件接线(板载,无需外接):
 *   - LED   : IO1(低电平点亮)
 *   - 按键  : IO0(BOOT 键,按下为低电平,松开为高电平)
 *
 * 功能:按下 BOOT 键,Led 翻转一次(带软件消抖)
 *
 * 学习要点:
 *   1. GPIO 配置:输出(点灯)/ 输入(读按键)/ 中断(边沿触发)
 *   2. 中断处理原则:ISR 里只做"记一笔"(置标志),耗时操作放主循环
 *   3. 机械按键抖动:按下瞬间电平会反复跳变,需要软件消抖
 *   4. volatile 关键字:告诉编译器这个变量会被中断修改,别乱优化
 */

#include <stdio.h>
#include <stdbool.h>                /* bool 类型 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"            /* GPIO 全部 API 都在这里 */
#include "esp_log.h"                /* ESP_LOGI 日志 */

/* ======================== 引脚定义 ======================== */
#define LED_GPIO    GPIO_NUM_1      /* LED 引脚 */
#define KEY_GPIO    GPIO_NUM_0      /* BOOT 按键引脚 */

static const char *TAG = "button";  /* 日志标签 */

/* 中断标志:ISR 里置 true,主循环检测到后消费
 * volatile 必不可少——它告诉编译器:
 *   "这个变量会被中断处理函数修改,每次使用都必须重新从内存读,
 *    不能自作聪明地缓存到寄存器里" */
static volatile bool s_key_pressed = false;

/**
 * @brief   按键中断回调(下降沿触发:按下瞬间电平由高变低)
 *
 * 重要:ISR 运行在中断上下文,必须短小快速,
 * 不能调用 printf/vTaskDelay 等可能阻塞的 API。
 * 这里只做一件事:把"按键被按下"这件事记录下来,
 * 具体处理(消抖、翻转 LED)交给主循环。
 *
 * IRAM_ATTR 让这个函数驻留在内部 RAM,
 * 保证 flash 缓存被占用时中断也能及时响应。
 */
static void IRAM_ATTR key_isr_handler(void *arg)
{
    s_key_pressed = true;
}

/**
 * @brief   程序入口
 */
void app_main(void)
{
    /* ---------- 1. 配置 LED 引脚为输出 ---------- */
    gpio_config_t led_cfg = {
        .pin_bit_mask = 1ULL << LED_GPIO,   /* 位掩码:要配置的引脚 */
        .mode = GPIO_MODE_OUTPUT,           /* 输出模式 */
        .pull_up_en = GPIO_PULLUP_DISABLE,  /* 输出不需要上拉 */
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,     /* 输出引脚不用中断 */
    };
    gpio_config(&led_cfg);
    gpio_set_level(LED_GPIO, 1);            /* 初始熄灭(LED 低电平点亮) */

    /* ---------- 2. 配置按键引脚为输入 + 上拉 + 下降沿中断 ---------- */
    gpio_config_t key_cfg = {
        .pin_bit_mask = 1ULL << KEY_GPIO,
        .mode = GPIO_MODE_INPUT,            /* 输入模式 */
        .pull_up_en = GPIO_PULLUP_ENABLE,   /* 使能内部上拉:松开时读到高电平 */
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,     /* 下降沿触发:按下瞬间(高->低) */
    };
    gpio_config(&key_cfg);

    /* ---------- 3. 安装 GPIO 中断服务,注册按键回调 ---------- */
    gpio_install_isr_service(0);            /* 0 = 默认中断标志 */
    gpio_isr_handler_add(KEY_GPIO, key_isr_handler, NULL);

    ESP_LOGI(TAG, "初始化完成,按下 BOOT 键(IO0)翻转 LED");

    /* ---------- 4. 主循环:消费中断标志 + 软件消抖 ---------- */
    while (1) {
        /* 检测到中断标志 */
        if (s_key_pressed) {
            s_key_pressed = false;          /* 先清标志,再处理 */

            /* 软件消抖:机械按键按下瞬间,触点会弹跳,
             * 电平在几百微秒内反复变化,可能触发多次中断。
             * 等 20ms 让抖动过去,再读一次电平确认:
             *   还是低电平 -> 确实是按下
             *   已经变高   -> 是抖动,忽略 */
            vTaskDelay(pdMS_TO_TICKS(20));

            if (gpio_get_level(KEY_GPIO) == 0) {
                /* 翻转 LED:读当前电平取反再写回 */
                gpio_set_level(LED_GPIO, !gpio_get_level(LED_GPIO));
                ESP_LOGI(TAG, "按键有效,LED 已翻转");
            }
        }

        /* 轮询间隔 10ms:既及时响应按键,又不会占满 CPU */
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
