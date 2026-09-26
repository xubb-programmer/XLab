/**
 * @file    ui.c
 * @brief   时钟界面绘制实现(极简风格 + 差分刷新)
 *
 * 界面只有两样东西:
 *   1. 纯黑背景
 *   2. 居中的 7 段数码管大时间(琥珀色亮段 + 暗色残影)
 *
 * 差分刷新原理(重要学习点):
 *   7 段数码管每一段只有"亮/暗"两种状态。暗色残影段在初始化时画好,
 *   之后永不重画。每秒刷新时,程序把每个数字当前"亮的段"记下来,
 *   和新的数字对比,只把状态发生变化的段翻一下:
 *     - 原来暗、现在亮 -> 用琥珀色画这一段
 *     - 原来亮、现在暗 -> 用暗色画这一段
 *   这样秒数变化时只有几根笔画在动,其他笔画纹丝不动,
 *   完全不会出现"整块抹掉再重画"的闪烁效果。
 */

#include <string.h>
#include <math.h>               /* sin/cos:画圆弧用 */

#include "ui.h"
#include "lcd.h"

/* ======================== 颜色定义 ======================== */
/* RGB565 颜色合成宏:把 8bit 的 R/G/B 压缩成 16bit */
#define RGB565(r, g, b) \
    ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | (((b) & 0xF8) >> 3))

#define SEG_LIT     RGB565(255, 175, 0)       /* 亮起的段:琥珀色 */
#define SEG_DIM     RGB565(52, 56, 86)        /* 熄灭的段:暗色残影 */

/* WiFi 状态图标颜色 */
#define WIFI_COLOR_OFF          RGB565(120, 120, 130)    /* 灰:未配置 */
#define WIFI_COLOR_CONNECTING   RGB565(255, 175, 0)      /* 琥珀:重连中 */
#define WIFI_COLOR_CONNECTED    RGB565(80, 220, 120)     /* 绿:已连接 */

/* ======================== 布局参数 ======================== */
#define SCREEN_W    320

#define DIGIT_W     36                      /* 单个数字宽度 */
#define DIGIT_H     62                      /* 单个数字高度 */
#define SEG_T       6                       /* 段(笔画)粗细 */
#define DIGIT_GAP   6                       /* 数字间距 */
#define COLON_W     10                      /* 冒号占位宽度 */

#define TIME_X0     27
#define DIGIT_0_X   TIME_X0
#define DIGIT_1_X   (DIGIT_0_X + DIGIT_W + DIGIT_GAP)
#define COLON_1_X   (DIGIT_1_X + DIGIT_W + DIGIT_GAP - 4)
#define DIGIT_2_X   (COLON_1_X + COLON_W + 4)
#define DIGIT_3_X   (DIGIT_2_X + DIGIT_W + DIGIT_GAP)
#define COLON_2_X   (DIGIT_3_X + DIGIT_W + DIGIT_GAP - 4)
#define DIGIT_4_X   (COLON_2_X + COLON_W + 4)
#define DIGIT_5_X   (DIGIT_4_X + DIGIT_W + DIGIT_GAP)
#define TIME_Y      96                      /* 数字顶边 */

/* WiFi 状态圆点位置与大小(右上角) */
#define WIFI_ICON_CX    306     /* 圆点中心 X */
#define WIFI_ICON_CY    14      /* 圆点中心 Y */
#define WIFI_DOT_R      7       /* 圆点半径(像素) */

/* 6 个数字格各自的 X 坐标,方便用下标访问 */
static const int s_digit_x[6] = {
    DIGIT_0_X, DIGIT_1_X, DIGIT_2_X,
    DIGIT_3_X, DIGIT_4_X, DIGIT_5_X,
};

/* 7 段码编码表:bit0=a(上) bit1=b(右上) bit2=c(右下)
 * bit3=d(下) bit4=e(左下) bit5=f(左上) bit6=g(中) */
static const uint8_t s_seg_map[10] = {
    0x3F,   /* 0: a b c d e f */
    0x06,   /* 1: b c */
    0x5B,   /* 2: a b d e g */
    0x4F,   /* 3: a b c d g */
    0x66,   /* 4: b c f g */
    0x6D,   /* 5: a c d f g */
    0x7D,   /* 6: a c d e f g */
    0x07,   /* 7: a b c */
    0x7F,   /* 8: 全部 */
    0x6F,   /* 9: a b c d f g */
};

/* 差分刷新的核心:记录每个数字当前"亮的段"
 * 初始为 0(全暗),第一次刷新时会把该亮的段全部点亮 */
static uint8_t s_digit_state[6] = {0, 0, 0, 0, 0, 0};

/**
 * @brief   快速矩形填充(性能优化,重要!)
 *
 * BSP 自带的 lcd_fill 是"每个像素发一次 SPI 事务",开销很大:
 * 一个 36x6 的段 = 216 次事务,整分翻页时几十段会耗时几百毫秒,
 * 这正是时钟偶尔跳秒的根源。
 *
 * 这里改成"先把一行像素拼到内存缓冲区,再用一次 SPI 事务发一整行",
 * 同样一个段只需 6 次事务,速度快几十倍,跳秒问题随之消失。
 */
static void fill_rect_fast(int x0, int y0, int x1, int y1, uint16_t color)
{
    int w = x1 - x0 + 1;
    int h = y1 - y0 + 1;

    /* 一行像素的缓冲区(数字格最大宽度 36,即 72 字节,栈上放得下) */
    uint8_t line[36 * 2];
    for (int i = 0; i < w; i++) {
        line[i * 2]     = color >> 8;    /* RGB565 高字节在前 */
        line[i * 2 + 1] = color & 0xFF;
    }

    /* 设置填充窗口,然后逐行整块发送 */
    lcd_set_window(x0, y0, x1, y1);
    for (int row = 0; row < h; row++) {
        lcd_write_data(line, w * 2);
    }
}

/**
 * @brief   在一个数字格内画一段(seg 0~6)
 * @param   x,y   数字格左上角
 * @param   seg   段编号 0=a,1=b,2=c,3=d,4=e,5=f,6=g
 * @param   color 段颜色(亮:SEG_LIT / 暗:SEG_DIM)
 */
static void draw_one_segment(int x, int y, uint8_t seg, uint16_t color)
{
    switch (seg) {
        case 0:  /* a:上横 */
            fill_rect_fast(x + 6, y, x + DIGIT_W - 6, y + SEG_T - 1, color);
            break;
        case 1:  /* b:右上竖 */
            fill_rect_fast(x + DIGIT_W - SEG_T, y + 6,
                           x + DIGIT_W - 1, y + DIGIT_H / 2 - 3, color);
            break;
        case 2:  /* c:右下竖 */
            fill_rect_fast(x + DIGIT_W - SEG_T, y + DIGIT_H / 2 + 3,
                           x + DIGIT_W - 1, y + DIGIT_H - 6, color);
            break;
        case 3:  /* d:下横 */
            fill_rect_fast(x + 6, y + DIGIT_H - SEG_T,
                           x + DIGIT_W - 6, y + DIGIT_H - 1, color);
            break;
        case 4:  /* e:左下竖 */
            fill_rect_fast(x, y + DIGIT_H / 2 + 3,
                           x + SEG_T - 1, y + DIGIT_H - 6, color);
            break;
        case 5:  /* f:左上竖 */
            fill_rect_fast(x, y + 6,
                           x + SEG_T - 1, y + DIGIT_H / 2 - 3, color);
            break;
        case 6:  /* g:中横 */
            fill_rect_fast(x + 6, y + DIGIT_H / 2 - SEG_T / 2,
                           x + DIGIT_W - 6, y + DIGIT_H / 2 + SEG_T / 2 - 1, color);
            break;
        default:
            break;
    }
}

/**
 * @brief   绘制冒号(两个小圆点,只在初始化时画一次)
 * @param   x,y     冒号占位左上角
 */
static void draw_colon(int x, int y)
{
    fill_rect_fast(x, y + DIGIT_H / 4, x + COLON_W - 4, y + DIGIT_H / 4 + 5, SEG_LIT);
    fill_rect_fast(x, y + DIGIT_H * 3 / 4 - 5, x + COLON_W - 4, y + DIGIT_H * 3 / 4, SEG_LIT);
}

/**
 * @brief   刷新一个数字格(差分:只翻动状态变化的段)
 * @param   index  数字格下标 0~5
 * @param   ch     字符 '0'~'9'
 */
static void update_digit(int index, char ch)
{
    uint8_t new_mask = (ch >= '0' && ch <= '9') ? s_seg_map[ch - '0'] : 0;
    uint8_t old_mask = s_digit_state[index];

    /* 逐段比较,只处理状态发生变化的那几段 */
    for (uint8_t seg = 0; seg < 7; seg++) {
        uint8_t old_bit = old_mask & (1 << seg);
        uint8_t new_bit = new_mask & (1 << seg);

        if (old_bit != new_bit) {
            /* 该段状态变了:原来暗->亮用琥珀色,原来亮->暗用暗色 */
            draw_one_segment(s_digit_x[index], TIME_Y, seg,
                             new_bit ? SEG_LIT : SEG_DIM);
        }
    }

    /* 记住当前状态,供下一次刷新对比 */
    s_digit_state[index] = new_mask;
}

/* ======================== 对外接口 ======================== */

void ui_init(void)
{
    /* 1. 纯黑背景 */
    lcd_clear(BLACK);

    /* 2. 把 6 个数字格的暗色残影段一次性画好(之后永不重画) */
    for (int i = 0; i < 6; i++) {
        for (uint8_t seg = 0; seg < 7; seg++) {
            draw_one_segment(s_digit_x[i], TIME_Y, seg, SEG_DIM);
        }
        s_digit_state[i] = 0;   /* 当前没有任何段点亮 */
    }

    /* 3. 两个冒号画好 */
    draw_colon(COLON_1_X, TIME_Y);
    draw_colon(COLON_2_X, TIME_Y);
}

void ui_update_time(const char *time_str)
{
    /* time_str = "HH:MM:SS",对应 6 个数字格的下标:
     *   0  1      2  3      4  5
     *   H  H  :   M  M  :   S  S
     * 差分刷新会自动只翻动变化的段,无需擦除任何区域 */
    update_digit(0, time_str[0]);
    update_digit(1, time_str[1]);
    update_digit(2, time_str[3]);
    update_digit(3, time_str[4]);
    update_digit(4, time_str[6]);
    update_digit(5, time_str[7]);
}

void ui_set_wifi_status(wifi_status_t status)
{
    /* 根据状态选颜色 */
    uint16_t color = WIFI_COLOR_OFF;
    if (status == WIFI_STATUS_CONNECTING) {
        color = WIFI_COLOR_CONNECTING;
    } else if (status == WIFI_STATUS_CONNECTED) {
        color = WIFI_COLOR_CONNECTED;
    }

    /* 1. 用黑色擦掉旧圆点区域 */
    fill_rect_fast(WIFI_ICON_CX - WIFI_DOT_R - 2, WIFI_ICON_CY - WIFI_DOT_R - 2,
                   WIFI_ICON_CX + WIFI_DOT_R + 2, WIFI_ICON_CY + WIFI_DOT_R + 2,
                   BLACK);

    /* 2. 画一个实心圆点:逐行计算圆边界上的 x 范围,整行填充。
     * 这样画出来是真正的圆,而不是方块 */
    for (int dy = -WIFI_DOT_R; dy <= WIFI_DOT_R; dy++) {
        int dx = (int)(sqrt((double)(WIFI_DOT_R * WIFI_DOT_R - dy * dy)) + 0.5);
        fill_rect_fast(WIFI_ICON_CX - dx, WIFI_ICON_CY + dy,
                       WIFI_ICON_CX + dx, WIFI_ICON_CY + dy, color);
    }
}
