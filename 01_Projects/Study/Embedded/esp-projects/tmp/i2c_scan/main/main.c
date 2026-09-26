/**
 * @file    main.c
 * @brief   I2C 总线扫描诊断工具(正点原子 ESP32-S3 开发板)
 *
 * 用途:
 *   1. 扫描 I2C0(IO41/42):检查 XL9555 扩展 IO 芯片(地址 0x20)
 *   2. 扫描 I2C1(IO5/4):检查 OLED 屏幕(SSD1306,地址 0x3C)
 *   扫描结果通过 USB 串口(COM3)打印出来,用于排查外设连接问题
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "esp_err.h"

/* I2C0 引脚定义(接 XL9555 等板载外设) */
#define IIC0_SDA_GPIO_PIN   GPIO_NUM_41
#define IIC0_SCL_GPIO_PIN   GPIO_NUM_42

/* I2C1 引脚定义(接 OLED 屏幕) */
#define IIC1_SDA_GPIO_PIN   GPIO_NUM_5
#define IIC1_SCL_GPIO_PIN   GPIO_NUM_4

/* I2C 通信速率:400kHz,与官方例程一致 */
#define IIC_FREQ            400000

/* XL9555 寄存器地址(与官方例程一致) */
#define XL9555_ADDR             0x20
#define XL9555_OUTPUT_PORT0_REG 2
#define XL9555_CONFIG_PORT0_REG 6

/* OLED 复位脚对应 XL9555 的引脚(P0.5) */
#define OV_RESET_IO             0x0020

/**
 * @brief   从 XL9555 指定寄存器开始读取 len 个字节
 */
static esp_err_t xl9555_read_regs(i2c_port_t port, uint8_t reg, uint8_t *data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    /* 先发送器件地址 + 写位,再发送寄存器地址 */
    i2c_master_write_byte(cmd, (XL9555_ADDR << 1) | I2C_MASTER_WRITE, 1);
    i2c_master_write_byte(cmd, reg, 1);
    /* 重复起始条件,切换为读模式 */
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (XL9555_ADDR << 1) | I2C_MASTER_READ, 1);
    i2c_master_read(cmd, data, len, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(port, cmd, 100 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    return ret;
}

/**
 * @brief   向 XL9555 指定寄存器写入 len 个字节
 */
static esp_err_t xl9555_write_regs(i2c_port_t port, uint8_t reg, uint8_t *data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (XL9555_ADDR << 1) | I2C_MASTER_WRITE, 1);
    i2c_master_write_byte(cmd, reg, 1);
    i2c_master_write(cmd, data, len, 1);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(port, cmd, 100 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    return ret;
}

/**
 * @brief   初始化 XL9555 的 OLED 复位脚,并释放复位
 * @param   port  I2C0 端口号
 * @retval  ESP_OK 成功
 */
static esp_err_t oled_rst_release(i2c_port_t port)
{
    uint8_t cfg[2] = {0};
    uint8_t out[2] = {0};

    /* 把 P0.5(OV_RESET_IO)配置为输出:CONFIG 寄存器对应位写 0 */
    if (xl9555_read_regs(port, XL9555_CONFIG_PORT0_REG, cfg, 2) != ESP_OK) {
        return ESP_FAIL;
    }
    cfg[0] &= ~(uint8_t)(OV_RESET_IO & 0xFF);
    if (xl9555_write_regs(port, XL9555_CONFIG_PORT0_REG, cfg, 2) != ESP_OK) {
        return ESP_FAIL;
    }

    /* 输出寄存器 P0.5 先写 0(复位),再写 1(释放复位) */
    if (xl9555_read_regs(port, XL9555_OUTPUT_PORT0_REG, out, 2) != ESP_OK) {
        return ESP_FAIL;
    }
    out[0] &= ~(uint8_t)(OV_RESET_IO & 0xFF);
    xl9555_write_regs(port, XL9555_OUTPUT_PORT0_REG, out, 2);
    vTaskDelay(pdMS_TO_TICKS(100));

    out[0] |= (uint8_t)(OV_RESET_IO & 0xFF);
    xl9555_write_regs(port, XL9555_OUTPUT_PORT0_REG, out, 2);
    vTaskDelay(pdMS_TO_TICKS(100));

    return ESP_OK;
}

/**
 * @brief   初始化一条 I2C 总线为主机模式
 * @param   port  I2C 端口号(I2C_NUM_0 / I2C_NUM_1)
 * @param   sda   SDA 引脚
 * @param   scl   SCL 引脚
 * @retval  ESP_OK 成功,否则失败
 */
static esp_err_t i2c_bus_init(i2c_port_t port, gpio_num_t sda, gpio_num_t scl)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,            /* 主机模式 */
        .sda_io_num = sda,                  /* SDA 引脚 */
        .scl_io_num = scl,                  /* SCL 引脚 */
        .sda_pullup_en = GPIO_PULLUP_ENABLE,/* SDA 使能上拉 */
        .scl_pullup_en = GPIO_PULLUP_ENABLE,/* SCL 使能上拉 */
        .master.clk_speed = IIC_FREQ,       /* 时钟频率 400kHz */
    };

    esp_err_t ret = i2c_param_config(port, &conf);
    if (ret != ESP_OK) {
        return ret;
    }
    return i2c_driver_install(port, I2C_MODE_MASTER, 0, 0, 0);
}

/**
 * @brief   扫描一条 I2C 总线上所有有响应的从机地址
 * @param   port  I2C 端口号
 * @param   name  总线名称(用于打印)
 * @retval  无
 */
static void scan_bus(i2c_port_t port, const char *name)
{
    printf("\n===== 扫描 %s (I2C%d) =====\n", name, port);
    int found = 0;

    /* I2C 合法从机地址范围一般是 0x08 ~ 0x77 */
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        /* 发送从机地址 + 写标志位,等待 ACK */
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, 1);
        i2c_master_stop(cmd);

        /* 执行本次探测,50ms 超时 */
        esp_err_t ret = i2c_master_cmd_begin(port, cmd, 50 / portTICK_PERIOD_MS);
        i2c_cmd_link_delete(cmd);

        if (ret == ESP_OK) {
            printf("  [找到设备] 地址 = 0x%02X\n", addr);
            found++;
        }
    }

    if (found == 0) {
        printf("  没有找到任何设备!\n");
    } else {
        printf("  共找到 %d 个设备\n", found);
    }
}

/**
 * @brief   程序入口:初始化两条 I2C 总线并分别扫描
 */
void app_main(void)
{
    /* 初始化 I2C0:XL9555 扩展 IO 芯片在这条总线上 */
    esp_err_t ret0 = i2c_bus_init(I2C_NUM_0, IIC0_SDA_GPIO_PIN, IIC0_SCL_GPIO_PIN);

    /* 初始化 I2C1:OLED 屏幕在这条总线上 */
    esp_err_t ret1 = i2c_bus_init(I2C_NUM_1, IIC1_SDA_GPIO_PIN, IIC1_SCL_GPIO_PIN);

    /* 通过 XL9555 释放 OLED 的复位脚(和官方例程保持一致) */
    esp_err_t rst_ret = ESP_FAIL;
    if (ret0 == ESP_OK) {
        rst_ret = oled_rst_release(I2C_NUM_0);
    }

    /* 循环扫描并打印,这样随时打开串口都能看到结果 */
    while (1) {
        printf("\n********** I2C 扫描诊断开始 **********\n");
        printf("I2C0 初始化: %s\n", ret0 == ESP_OK ? "成功" : "失败");
        printf("I2C1 初始化: %s\n", ret1 == ESP_OK ? "成功" : "失败");
        printf("OLED 复位释放: %s\n", rst_ret == ESP_OK ? "成功" : "失败");

        /* 分别扫描两条总线 */
        scan_bus(I2C_NUM_0, "XL9555/板载外设");
        scan_bus(I2C_NUM_1, "OLED 屏幕");

        printf("********** 扫描结束 **********\n");
        printf("预期结果: I2C0 应找到 0x20(XL9555); I2C1 应找到 0x3C(OLED)\n");

        /* 每 5 秒重复扫描一次 */
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
