/**
 * @file    cmd.c
 * @brief   串口命令模块实现
 *
 * 串口命令是怎么工作的(学习重点):
 *   1. getchar() 从 USB 串口逐个读字符,没有输入时阻塞等待
 *   2. 收到的字符放进行缓冲区,并回显到屏幕,让用户看到自己打了什么
 *   3. 收到回车就把整行当命令解析执行
 *   4. WiFi 配置用 NVS 键值对保存,断电不丢,改配置不用重新编译
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_system.h"             /* esp_restart 重启 */
#include "nvs_flash.h"              /* NVS 键值存储 */

#include "cmd.h"

/* ======================== 常量定义 ======================== */
#define NVS_NAMESPACE   "wifi_cfg"  /* NVS 命名空间:相当于一个"表格" */
#define KEY_SSID        "ssid"      /* 键:WiFi 名称 */
#define KEY_PASS        "pass"      /* 键:WiFi 密码 */

#define CMD_BUF_MAX     128         /* 命令行最大长度 */
#define SSID_MAX        32          /* SSID 最长 32 字符(WiFi 标准) */
#define PASS_MAX        64          /* 密码最长 64 字符 */

static const char *TAG = "cmd";

/**
 * @brief   把 WiFi 配置写入 NVS
 * @param   ssid  WiFi 名称
 * @param   pass  WiFi 密码
 * @retval  ESP_OK 成功
 */
static esp_err_t wifi_cfg_save(const char *ssid, const char *pass)
{
    nvs_handle_t handle;

    /* 1. 打开命名空间(可读写);第一次使用会自动创建 */
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    /* 2. 写入两个键值 */
    ret = nvs_set_str(handle, KEY_SSID, ssid);
    if (ret == ESP_OK) {
        ret = nvs_set_str(handle, KEY_PASS, pass);
    }

    /* 3. 提交:真正落盘(断电不丢) */
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }

    nvs_close(handle);
    return ret;
}

bool wifi_cfg_load(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    nvs_handle_t handle;

    /* 只读打开;没初始化过会失败,直接返回 false */
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }

    size_t len = ssid_len;
    esp_err_t r1 = nvs_get_str(handle, KEY_SSID, ssid, &len);
    len = pass_len;
    esp_err_t r2 = nvs_get_str(handle, KEY_PASS, pass, &len);

    nvs_close(handle);
    return (r1 == ESP_OK && r2 == ESP_OK);
}

/**
 * @brief   执行一条命令
 * @param   line  完整的命令行(不含换行)
 */
static void cmd_execute(const char *line)
{
    /* ---------- help 命令 ---------- */
    if (strcmp(line, "help") == 0 || strcmp(line, "?") == 0) {
        printf("可用命令:\r\n");
        printf("  wifi                    查看当前保存的 WiFi 配置\r\n");
        printf("  wifi <SSID> <密码>      保存 WiFi 配置并重启联网\r\n");
        printf("  wifi clear              清除已保存的配置(恢复使用代码默认值)\r\n");
        printf("  help                    显示本帮助\r\n");
        return;
    }

    /* ---------- wifi 相关命令 ---------- */
    if (strncmp(line, "wifi", 4) == 0) {
        /* 只有 "wifi",没有参数:查询已保存的配置 */
        if (line[4] == '\0') {
            char ssid[SSID_MAX + 1] = {0};
            char pass[PASS_MAX + 1] = {0};
            if (wifi_cfg_load(ssid, sizeof(ssid), pass, sizeof(pass))) {
                /* 学习项目明文打印;真实产品绝不能打印密码! */
                printf("已保存配置: SSID=%s 密码=%s\r\n", ssid, pass);
            } else {
                printf("NVS 里没有 WiFi 配置\r\n");
            }
            return;
        }

        /* "wifi clear":清除 NVS 里的配置
         * 注意:程序开机时 NVS 配置优先于代码里的 #define,
         * 所以想用回写死的默认 WiFi,必须先清掉 NVS */
        if (strcmp(line, "wifi clear") == 0) {
            nvs_handle_t handle;
            esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
            if (ret == ESP_OK) {
                nvs_erase_key(handle, KEY_SSID);
                nvs_erase_key(handle, KEY_PASS);
                ret = nvs_commit(handle);
                nvs_close(handle);
            }
            if (ret == ESP_OK) {
                printf("WiFi 配置已清除,3 秒后重启(将使用代码默认配置)\r\n");
                vTaskDelay(pdMS_TO_TICKS(3000));
                esp_restart();
            } else {
                printf("清除失败,请重试\r\n");
            }
            return;
        }

        /* "wifi <SSID> <密码>":保存配置 */
        if (line[4] == ' ') {
            const char *ssid = line + 5;            /* 指向 SSID 起点 */
            const char *sp = strchr(ssid, ' ');     /* 找到下一个空格 */
            if (sp == NULL || sp == ssid) {
                printf("格式错误,正确格式: wifi <SSID> <密码>\r\n");
                return;
            }

            /* 拆出 SSID(不允许含空格,WiFi 标准如此) */
            char ssid_buf[SSID_MAX + 1];
            size_t ssid_len = (size_t)(sp - ssid);
            if (ssid_len > SSID_MAX) {
                printf("SSID 太长(最多 %d 字符)\r\n", SSID_MAX);
                return;
            }
            memcpy(ssid_buf, ssid, ssid_len);
            ssid_buf[ssid_len] = '\0';

            /* 剩下的是密码 */
            const char *pass = sp + 1;
            if (strlen(pass) == 0) {
                printf("密码不能为空\r\n");
                return;
            }
            if (strlen(pass) > PASS_MAX) {
                printf("密码太长(最多 %d 字符)\r\n", PASS_MAX);
                return;
            }

            /* 写入 NVS */
            if (wifi_cfg_save(ssid_buf, pass) == ESP_OK) {
                printf("WiFi 配置已保存: %s,3 秒后重启联网...\r\n", ssid_buf);
                vTaskDelay(pdMS_TO_TICKS(3000));
                esp_restart();                      /* 重启后自动读配置连网 */
            } else {
                printf("保存失败,请重试\r\n");
            }
            return;
        }
    }

    printf("未知命令: %s(输入 help 查看帮助)\r\n", line);
}

/**
 * @brief   串口命令处理任务
 *          原理:逐字符读取 + 行缓冲 + 回显 + 回车解析
 */
static void cmd_task(void *arg)
{
    char line[CMD_BUF_MAX];
    int idx = 0;

    printf("\r\n[配置终端] 输入 help 查看可用命令\r\n");
    printf("配置 WiFi 示例: wifi MyHomeWiFi 12345678\r\n");

    while (1) {
        /* 1. 读取一个字符
         * 注意:USB 串口在"没有数据"时 getchar() 可能返回 -1(EOF)
         * 而不是阻塞,如果不处理,这里会空转占满 CPU,
         * 触发任务看门狗。所以读到无效字符就小睡一下让出 CPU */
        int c = getchar();
        if (c < 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        /* 2. 回车/换行:整行命令处理完 */
        if (c == '\r' || c == '\n') {
            if (idx > 0) {
                line[idx] = '\0';
                printf("\r\n");
                cmd_execute(line);
                idx = 0;
            } else {
                printf("\r\n");
            }
            continue;
        }

        /* 3. 退格(0x7F 是很多终端发的删除键) */
        if (c == '\b' || c == 0x7F) {
            if (idx > 0) {
                idx--;
                printf("\b \b");        /* 屏幕上的字符也擦掉 */
                fflush(stdout);
            }
            continue;
        }

        /* 4. 普通可打印字符:存进缓冲区 + 回显 */
        if (c >= 0x20 && c < 0x7F) {
            if (idx < CMD_BUF_MAX - 1) {
                line[idx++] = (char)c;
                putchar(c);             /* 回显:让用户看到输入 */
                fflush(stdout);
            }
        }
    }
}

void cmd_task_start(void)
{
    /* 独立任务,优先级 5;大部分时间阻塞在 getchar,不占 CPU */
    xTaskCreate(cmd_task, "cmd", 4096, NULL, 5, NULL);
}
