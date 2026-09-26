/**
 * @file    main.c
 * @brief   ESP32-S3 网络时钟(320x240 ST7789 IPS 屏显示)
 *
 * 功能说明:
 *   1. 连接 WiFi 后通过 NTP 协议自动校准时间(北京时间,东八区)
 *   2. 屏幕界面由 ui.c 绘制:纯黑背景 + 7 段数码管大字体
 *   3. 刷新策略:静态界面只画一次,每秒只重画时间区域,不闪屏
 *   4. 如果没配置 WiFi 或校时失败,自动使用"本地时间"从设定初始值开始走
 *
 * 学习要点:
 *   - WiFi 连接流程:网络接口 -> STA 模式 -> 事件回调 -> 获取 IP
 *   - NTP 校时:SNTP 客户端 + setenv 设置时区
 *   - 时间处理:time() / localtime_r() / tm 结构体
 *   - 无闪烁刷新:只更新变化的区域,而不是整屏 clear 重画
 */

#include <stdio.h>                  /* sprintf 格式化字符串 */
#include <string.h>                 /* strcmp/strncpy 字符串函数 */
#include <time.h>                   /* time/localtime 时间函数 */
#include <sys/time.h>               /* settimeofday/gettimeofday */
#include <stdint.h>                 /* uint8_t/uint16_t 定长类型 */

#include "freertos/FreeRTOS.h"      /* FreeRTOS 内核 */
#include "freertos/task.h"          /* vTaskDelay 延时 */
#include "freertos/event_groups.h"  /* 事件组:等待 WiFi 连接成功 */

#include "esp_log.h"                /* ESP_LOGI 日志打印 */
#include "esp_wifi.h"               /* WiFi 接口 */
#include "esp_event.h"              /* 事件循环 */
#include "esp_netif.h"              /* 网络接口 */
#include "nvs_flash.h"              /* 非易失存储(WiFi 需要) */
#include "esp_sntp.h"               /* SNTP 网络校时 */
#include "esp_timer.h"              /* 高精度单调时钟(秒相位调度用) */

/* 正点原子 BSP 驱动 */
#include "lcd.h"                    /* LCD 底层驱动 */
#include "led.h"                    /* 板载 LED */
#include "iic.h"                    /* I2C 总线 */
#include "spi.h"                    /* SPI 总线(屏幕用) */
#include "xl9555.h"                 /* IO 扩展芯片 */

/* 自己写的界面模块 */
#include "ui.h"
/* 串口命令模块(配置 WiFi 用) */
#include "cmd.h"

/* ======================== 用户配置区 ======================== */
/* TODO: 改成你自己的 WiFi 名称和密码,才能联网自动校时 */
#define WIFI_SSID           "X-MCore"
#define WIFI_PASSWORD       "xubb123456"

/* NTP 校时服务器(国内推荐阿里云) */
#define NTP_SERVER          "ntp.aliyun.com"

/* 时区:东八区(北京时间,无夏令时) */
#define TIMEZONE            "CST-8"

/* 没网时的初始时间(2026-08-15 12:00:00) */
#define FALLBACK_YEAR       2026
#define FALLBACK_MONTH      8
#define FALLBACK_DAY        15
#define FALLBACK_HOUR       12
#define FALLBACK_MIN        0
#define FALLBACK_SEC        0

/* 等待 NTP 校时的最长秒数 */
#define NTP_WAIT_SECONDS    15
/* ============================================================ */

static const char *TAG = "clock";   /* 日志标签 */

static i2c_obj_t i2c0_master;       /* I2C0 控制块(XL9555 挂在这条总线上) */

/* 当前实际使用的 WiFi 名称(可能是 NVS 配置,也可能是默认宏)
 * 事件回调里打印日志要用它,而不是写死 WIFI_SSID 宏 */
static const char *s_wifi_ssid = WIFI_SSID;

/* 事件组:WiFi 连接成功后置位,程序等待这个标志再继续 */
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT  BIT0

/* ============ 断线重连:退避时间控制 ============
 * 初次断开等 5 秒重连;连续失败间隔翻倍(10s/20s/40s),
 * 最多 60 秒;一旦连上就重置回 5 秒 */
#define RECONNECT_INIT_MS   5000u
#define RECONNECT_MAX_MS    60000u
static esp_timer_handle_t s_reconnect_timer = NULL;
static uint32_t s_reconnect_delay_ms = RECONNECT_INIT_MS;
static bool s_first_ip = true;      /* 记录是不是第一次拿到 IP */

/**
 * @brief   断线重连定时器回调:到点后发起一次连接尝试
 *
 * 为什么不用事件回调里直接延时?
 * 事件回调运行在 WiFi 事件任务中,长时间阻塞会耽误其他事件,
 * 所以用一次性定时器异步触发重连,是更规范的做法。
 */
static void reconnect_timer_cb(void *arg)
{
    esp_wifi_connect();
}

/**
 * @brief   WiFi 事件回调:处理连接开始、断线重连等
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "WiFi 开始连接 %s", s_wifi_ssid);
            esp_wifi_connect();
            break;

        case WIFI_EVENT_STA_DISCONNECTED:
            /* 断开:图标变琥珀色,并进入"退避重连"节奏 */
            ESP_LOGW(TAG, "WiFi 断开,%.1f 秒后重连",
                     s_reconnect_delay_ms / 1000.0);
            ui_set_wifi_status(WIFI_STATUS_CONNECTING);

            esp_timer_start_once(s_reconnect_timer,
                                 s_reconnect_delay_ms * 1000);
            if (s_reconnect_delay_ms < RECONNECT_MAX_MS) {
                s_reconnect_delay_ms *= 2;      /* 下一次间隔翻倍 */
            }
            break;

        default:
            break;
    }
}

/**
 * @brief   IP 事件回调:获取到 IP 说明网络就绪
 */
static void ip_event_handler(void *arg, esp_event_base_t event_base,
                             int32_t event_id, void *event_data)
{
    if (event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "获取到 IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

        /* 连接成功:退避重置、图标变绿 */
        s_reconnect_delay_ms = RECONNECT_INIT_MS;
        ui_set_wifi_status(WIFI_STATUS_CONNECTED);

        /* 如果是重连成功(不是第一次),立刻重新校时,
         * 不用等 SNTP 默认的 1 小时轮询周期 */
        if (!s_first_ip) {
            ESP_LOGI(TAG, "WiFi 重连成功,立即重新校时");
            esp_sntp_restart();
        }
        s_first_ip = false;
    }
}

/**
 * @brief   初始化并连接 WiFi(站点模式)
 */
static esp_err_t wifi_init_sta(const char *ssid, const char *pass)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, ESP_EVENT_ANY_ID, &ip_event_handler, NULL, NULL));

    /* 把 SSID/密码拷进配置结构体。
     * 注意:必须留一个字节给结尾的 '\0',否则 WiFi 库会解析出错 */
    wifi_config_t wifi_cfg = {0};
    strncpy((char *)wifi_cfg.sta.ssid, ssid, sizeof(wifi_cfg.sta.ssid) - 1);
    strncpy((char *)wifi_cfg.sta.password, pass, sizeof(wifi_cfg.sta.password) - 1);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));

    /* 创建断线重连用的定时器(一次性,断开后到点触发重连) */
    esp_timer_create_args_t timer_args = {
        .callback = reconnect_timer_cb,
        .name = "wifi_reconnect",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_reconnect_timer));

    ESP_ERROR_CHECK(esp_wifi_start());
    return ESP_OK;
}

/**
 * @brief   启动 SNTP 客户端(只初始化,不等待)
 *
 * 重要设计:SNTP 要在 WiFi 连接之前就启动。
 * 这样无论 WiFi 是 3 秒连上还是 3 分钟后才连上,
 * 只要拿到 IP,SNTP 都会自动开始校时,不会错过。
 */
static void sntp_start(void)
{
    setenv("TZ", TIMEZONE, 1);      /* 设置时区为东八区 */
    tzset();

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, NTP_SERVER);
    esp_sntp_init();
}

/**
 * @brief   等待 SNTP 校时完成
 * @retval  true 校时成功 / false 超时
 */
static bool sntp_wait_sync(void)
{
    int tries = 0;
    bool synced = false;

    /* 注意:esp_sntp_get_sync_status() 是"破坏性读取"——
     * 读到 COMPLETED 后内部会立刻把状态复位成 RESET。
     * 所以绝不能调用两次来判断,必须只读一次并保存结果 */
    while (tries < NTP_WAIT_SECONDS) {
        sntp_sync_status_t st = esp_sntp_get_sync_status();
        if (st == SNTP_SYNC_STATUS_COMPLETED) {
            synced = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        tries++;
    }

    if (synced) {
        ESP_LOGI(TAG, "NTP 校时成功");
        return true;
    }
    ESP_LOGW(TAG, "NTP 校时超时,使用本地时间");
    return false;
}

/**
 * @brief   手动设置初始时间(没网时的兜底方案)
 */
static void set_fallback_time(void)
{
    struct tm t0 = {0};
    t0.tm_year  = FALLBACK_YEAR - 1900;
    t0.tm_mon   = FALLBACK_MONTH - 1;
    t0.tm_mday  = FALLBACK_DAY;
    t0.tm_hour  = FALLBACK_HOUR;
    t0.tm_min   = FALLBACK_MIN;
    t0.tm_sec   = FALLBACK_SEC;

    time_t fallback = mktime(&t0);
    struct timeval tv = { .tv_sec = fallback, .tv_usec = 0 };
    settimeofday(&tv, NULL);

    ESP_LOGI(TAG, "本地时间从 %04d-%02d-%02d %02d:%02d:%02d 开始",
             FALLBACK_YEAR, FALLBACK_MONTH, FALLBACK_DAY,
             FALLBACK_HOUR, FALLBACK_MIN, FALLBACK_SEC);
}

/**
 * @brief   程序入口
 */
void app_main(void)
{
    /* ---------- 0. 最先控制屏幕,避免上电花屏 ----------
     * 屏幕电源/复位由 XL9555 控制,而上电瞬间 XL9555 还没初始化,
     * 屏幕芯片会在"未初始化 + 不复位"的状态下带电工作,
     * 显示随机花屏。
     * 所以第一步就初始化 I2C + XL9555,并强制关闭电源、拉低复位,
     * 让屏幕保持黑屏,直到 lcd_init 正式初始化 */
    i2c0_master = iic_init(I2C_NUM_0);          /* I2C0:XL9555 用 */
    xl9555_init(i2c0_master);                   /* IO 扩展芯片 */
    xl9555_pin_write(SLCD_PWR_IO, 0);           /* 屏幕电源:关闭 */
    xl9555_pin_write(SLCD_RST_IO, 0);           /* 屏幕复位:保持低电平 */

    /* ---------- 1. NVS 与其余外设 ---------- */
    ESP_ERROR_CHECK(nvs_flash_init());          /* NVS:WiFi 配置存储 */
    led_init();                                 /* 板载 LED */
    spi2_init();                                /* SPI2:LCD 用 */

    /* 屏幕正式初始化(内部会打开电源、释放复位并发送初始化序列) */
    lcd_init();

    /* ---------- 2. 画一次静态界面(背景/标题/面板) ---------- */
    ui_init();

    /* ---------- 3. 读取 WiFi 配置:优先 NVS,其次代码默认值 ---------- */
    char nvs_ssid[33] = {0};
    char nvs_pass[65] = {0};
    bool have_nvs_cfg = wifi_cfg_load(nvs_ssid, sizeof(nvs_ssid),
                                      nvs_pass, sizeof(nvs_pass));
    if (have_nvs_cfg) {
        ESP_LOGI(TAG, "从 NVS 读取到 WiFi 配置: %s", nvs_ssid);
    }

    const char *ssid = have_nvs_cfg ? nvs_ssid : WIFI_SSID;
    const char *pass = have_nvs_cfg ? nvs_pass : WIFI_PASSWORD;
    bool use_ntp = have_nvs_cfg || (strncmp(WIFI_SSID, "YOUR_", 5) != 0);

    /* 记录实际使用的 SSID,供 WiFi 事件回调打印正确日志 */
    s_wifi_ssid = ssid;

    /* 启动串口命令任务:随时可以输入 wifi <SSID> <密码> 保存配置 */
    cmd_task_start();

    if (use_ntp) {
        ESP_LOGI(TAG, "尝试连接 WiFi(%s) 并 NTP 校时", ssid);
        ui_set_wifi_status(WIFI_STATUS_CONNECTING);   /* 图标:琥珀色,连接中 */

        /* 必须先初始化 WiFi(内部会初始化网络栈 esp_netif),
         * 再启动 SNTP——顺序反了 SNTP 会崩溃导致反复重启 */
        wifi_init_sta(ssid, pass);

        /* SNTP 在等待 IP 之前就绪(不阻塞),这样 WiFi 任何时刻连上都会自动校时 */
        sntp_start();

        EventBits_t bits = xEventGroupWaitBits(
            s_wifi_event_group, WIFI_CONNECTED_BIT,
            pdFALSE, pdTRUE, pdMS_TO_TICKS(20000));

        if (bits & WIFI_CONNECTED_BIT) {
            if (sntp_wait_sync()) {
                ESP_LOGI(TAG, "网络时间已同步");
            } else {
                set_fallback_time();
            }
        } else {
            ESP_LOGW(TAG, "WiFi 连接超时,使用本地时间");
            set_fallback_time();
        }
    } else {
        ESP_LOGW(TAG, "未配置 WiFi,使用本地时间(想联网请填写 WIFI_SSID)");
        ui_set_wifi_status(WIFI_STATUS_OFF);          /* 图标:灰色,未配置 */
        setenv("TZ", TIMEZONE, 1);
        tzset();
        set_fallback_time();
    }

    /* ---------- 4. 主循环:只刷新变化的部分 ---------- */
    char last_time[32] = "";

    /* 固定节拍调度:第一次刷新定在 1 秒后,之后每个整秒一次。
     * 用 esp_timer(单调时钟)而不是 gettimeofday(墙上时钟),
     * 这样即使系统时间被 NTP 调整,刷新节奏也不受影响 */
    int64_t next_display_us = esp_timer_get_time() + 1000000;

    while (1) {
        time_t now = time(NULL);
        struct tm t;
        localtime_r(&now, &t);                  /* 转为北京时间 */

        char time_buf[32];
        /* 把年月日转成小范围类型(uint16/uint8),既节省空间,
         * 也让编译器知道最坏长度,避免格式溢出告警 */
        sprintf(time_buf, "%02d:%02d:%02d",
                (uint8_t)t.tm_hour, (uint8_t)t.tm_min, (uint8_t)t.tm_sec);

        /* 时间每秒都在变:刷新 7 段数码管 */
        if (strcmp(time_buf, last_time) != 0) {
            ui_update_time(time_buf);
            strcpy(last_time, time_buf);
        }

        /* 串口打印,方便调试观察 */
        ESP_LOGI(TAG, "%s", time_buf);

        LED_TOGGLE();                           /* LED 每秒翻转 */

        /* 等待到预定的整秒时刻。分片睡 10ms,避免空转占 CPU */
        while (esp_timer_get_time() < next_display_us) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        /* 如果某次画屏异常慢、落后超过一整秒,
         * 跳过已经错过的帧,直接对齐到未来的整秒,保持节奏稳定 */
        if (esp_timer_get_time() - next_display_us >= 1000000) {
            ESP_LOGW(TAG, "刷新落后,跳过一帧");
            while (esp_timer_get_time() - next_display_us >= 1000000) {
                next_display_us += 1000000;
            }
        }
        next_display_us += 1000000;             /* 定下一次整秒时刻 */
    }
}
