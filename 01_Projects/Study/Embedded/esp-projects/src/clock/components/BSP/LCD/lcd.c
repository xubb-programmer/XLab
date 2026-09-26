/**
 ****************************************************************************************************
* @file        lcd.c
* @author      姝ｇ偣鍘熷瓙鍥㈤槦(ALIENTEK)
* @version     V1.0
* @date        2023-08-26
* @brief       SPI LCD(MCU灞? 椹卞姩浠ｇ爜
*              鏀寔椹卞姩IC鍨嬪彿鍖呮嫭:ILI9341绛?
*
* @license     Copyright (c) 2020-2032, 骞垮窞甯傛槦缈肩數瀛愮鎶€鏈夐檺鍏徃
****************************************************************************************************
* @attention
*
 * 瀹為獙骞冲彴:姝ｇ偣鍘熷瓙 ESP32-S3 寮€鍙戞澘
* 鍦ㄧ嚎瑙嗛:www.yuanzige.com
* 鎶€鏈鍧?www.openedv.com
* 鍏徃缃戝潃:www.alientek.com
* 璐拱鍦板潃:openedv.taobao.com
*
****************************************************************************************************
*/

#define __LCD_VERSION__  "1.0"

#include "lcd.h"
#include "lcdfont.h"


#define SPI_LCD_TYPE    0   /* SPI 接口屏幕类型: 0=ST7789(320x240 IPS), 1=ILI9341(2.4寸) */           /* SPI鎺ュ彛灞忓箷绫诲瀷锛?锛?.4瀵窼PILCD  0锛?.3瀵窼PILCD锛?*/  

spi_device_handle_t MY_LCD_Handle;
uint8_t lcd_buf[LCD_TOTAL_BUF_SIZE];
lcd_obj_t lcd_self;


/* LCD闇€瑕佸垵濮嬪寲涓€缁勫懡浠?鍙傛暟鍊笺€傚畠浠瓨鍌ㄥ湪姝ょ粨鏋勪腑  */
typedef struct
{
    uint8_t cmd;
    uint8_t data[16];
    uint8_t databytes; /* 鏁版嵁涓病鏈夋暟鎹紱姣旂壒7锛濊缃悗鐨勫欢杩燂紱0xFF=cmds缁撴潫 */
} lcd_init_cmd_t;

/**
 * @brief       鍙戦€佸懡浠ゅ埌LCD锛屼娇鐢ㄨ疆璇㈡柟寮忛樆濉炵瓑寰呬紶杈撳畬鎴?鐢变簬鏁版嵁浼犺緭閲忓緢灏戯紝鍥犳鍦ㄨ疆璇㈡柟寮忓鐞嗗彲鎻愰珮閫熷害銆備娇鐢ㄤ腑鏂柟寮忕殑寮€閿€瑕佽秴杩囪疆璇㈡柟寮?
 * @param       cmd 浼犺緭鐨?浣嶅懡浠ゆ暟鎹?
 * @retval      鏃?
 */
void lcd_write_cmd(const uint8_t cmd)
{
    LCD_WR(0);
    spi2_write_cmd(MY_LCD_Handle, cmd);
}

/**
 * @brief       鍙戦€佹暟鎹埌LCD锛屼娇鐢ㄨ疆璇㈡柟寮忛樆濉炵瓑寰呬紶杈撳畬鎴?鐢变簬鏁版嵁浼犺緭閲忓緢灏戯紝鍥犳鍦ㄨ疆璇㈡柟寮忓鐞嗗彲鎻愰珮閫熷害銆備娇鐢ㄤ腑鏂柟寮忕殑寮€閿€瑕佽秴杩囪疆璇㈡柟寮?
 * @param       data 浼犺緭鐨?浣嶆暟鎹?
 * @retval      鏃?
 */
void lcd_write_data(const uint8_t *data, int len)
{
    LCD_WR(1);
    spi2_write_data(MY_LCD_Handle, data, len);
}

/**
 * @brief       鍙戦€佹暟鎹埌LCD锛屼娇鐢ㄨ疆璇㈡柟寮忛樆濉炵瓑寰呬紶杈撳畬鎴?鐢变簬鏁版嵁浼犺緭閲忓緢灏戯紝鍥犳鍦ㄨ疆璇㈡柟寮忓鐞嗗彲鎻愰珮閫熷害銆備娇鐢ㄤ腑鏂柟寮忕殑寮€閿€瑕佽秴杩囪疆璇㈡柟寮?
 * @param       data 浼犺緭鐨?6浣嶆暟鎹?
 * @retval      鏃?
 */
void lcd_write_data16(uint16_t data)
{
    uint8_t dataBuf[2] = {0,0};
    dataBuf[0] = data >> 8;
    dataBuf[1] = data & 0xFF;
    LCD_WR(1);
    spi2_write_data(MY_LCD_Handle, dataBuf,2);
}

/**
 * @brief       璁剧疆绐楀彛澶у皬
 * @param       xstar锛氬乏涓婅x杞?
 * @param       ystar锛氬乏涓婅y杞?
 * @param       xend锛氬彸涓嬭x杞?
 * @param       yend锛氬彸涓嬭y杞?
 * @retval      鏃?
 */
void lcd_set_window(uint16_t xstar, uint16_t ystar,uint16_t xend,uint16_t yend)
{	
    uint8_t databuf[4] = {0,0,0,0};
    databuf[0] = xstar >> 8;
    databuf[1] = 0xFF & xstar;
    databuf[2] = xend >> 8;
    databuf[3] = 0xFF & xend;
    lcd_write_cmd(lcd_self.setxcmd);
    lcd_write_data(databuf,4);

    databuf[0] = ystar >> 8;
    databuf[1] = 0xFF & ystar;
    databuf[2] = yend >> 8;
    databuf[3] = 0xFF & yend;
    lcd_write_cmd(lcd_self.setycmd);
    lcd_write_data(databuf,4);

    lcd_write_cmd(lcd_self.wramcmd);    /* 寮€濮嬪啓鍏RAM */
}   

/**
 * @brief       浠ヤ竴绉嶉鑹叉竻绌篖CD灞?
 * @param       color 娓呭睆棰滆壊
 * @retval      鏃?
 */
void lcd_clear(uint16_t color)
{
    uint16_t i, j;
    uint8_t data[2] = {0};

    data[0] = color >> 8;
    data[1] = color;
    
    lcd_set_window(0, 0, lcd_self.width - 1, lcd_self.height - 1);

    for(j = 0; j < LCD_BUF_SIZE / 2; j++)
    {
        lcd_buf[j * 2] =  data[0];
        lcd_buf[j * 2 + 1] =  data[1];
    }

    for(i = 0; i < (LCD_TOTAL_BUF_SIZE / LCD_BUF_SIZE); i++)
    {
        lcd_write_data(lcd_buf, LCD_BUF_SIZE);
    }
}

/**
 * @brief       鍦ㄦ寚瀹氬尯鍩熷唴濉厖鍗曚釜棰滆壊
 * @param       (sx,sy),(ex,ey):濉厖鐭╁舰瀵硅鍧愭爣,鍖哄煙澶у皬涓?(ex - sx + 1) * (ey - sy + 1)
 * @param       color:瑕佸～鍏呯殑棰滆壊(32浣嶉鑹?鏂逛究鍏煎LTDC)
 * @retval      鏃?
 */
void lcd_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint16_t color)
{
    uint16_t i;
    uint16_t j;
    uint16_t width;
    uint16_t height;

    width = ex - sx + 1;
    height = ey - sy + 1;
    lcd_set_window(sx, sy, ex, ey);

    for (i = 0; i < height; i++)
    {
        for (j = 0; j < width; j++)
        {
            lcd_write_data16(color);
        }
    }
    lcd_set_window(sx, sy, ex, ey);
}


/**
 * @brief       璁剧疆鍏夋爣鐨勪綅缃?
 * @param       Xpos锛氬乏涓婅x杞?
 * @param       Ypos锛氬乏涓婅y杞?
 * @retval      鏃?
 */
void lcd_set_cursor(uint16_t xpos, uint16_t ypos)
{
    lcd_set_window(xpos,ypos,xpos,ypos);	
} 

/**
 * @brief       璁剧疆LCD鐨勮嚜鍔ㄦ壂鎻忔柟鍚?瀵筊GB灞忔棤鏁?
 * @param       dir:0~7,浠ｈ〃8涓柟鍚?鍏蜂綋瀹氫箟瑙乴cd.h)
 * @retval      鏃?
 */
void lcd_scan_dir(uint8_t dir)
{
    uint8_t regval = 0;
    uint8_t dirreg = 0;
    uint16_t temp;

    /* 妯睆鏃讹紝瀵?963涓嶆敼鍙樻壂鎻忔柟鍚? 鍏朵粬IC鏀瑰彉鎵弿鏂瑰悜锛佺珫灞忔椂1963鏀瑰彉鏂瑰悜, 鍏朵粬IC涓嶆敼鍙樻壂鎻忔柟鍚?*/
    if (lcd_self.dir == 1)
    {
        dir = 5;
    }

    /* 鏍规嵁鎵弿鏂瑰紡 璁剧疆 0X36/0X3600 瀵勫瓨鍣?bit 5,6,7 浣嶇殑鍊?*/
    switch (dir)
    {
        case L2R_U2D:                           /* 浠庡乏鍒板彸,浠庝笂鍒颁笅 */
            regval |= (0 << 7) | (0 << 6) | (0 << 5);
            break;

        case L2R_D2U:                           /* 浠庡乏鍒板彸,浠庝笅鍒颁笂 */
            regval |= (1 << 7) | (0 << 6) | (0 << 5);
            break;

        case R2L_U2D:                           /* 浠庡彸鍒板乏,浠庝笂鍒颁笅 */
            regval |= (0 << 7) | (1 << 6) | (0 << 5);
            break;

        case R2L_D2U:                           /* 浠庡彸鍒板乏,浠庝笅鍒颁笂 */
            regval |= (1 << 7) | (1 << 6) | (0 << 5);
            break;

        case U2D_L2R:                           /* 浠庝笂鍒颁笅,浠庡乏鍒板彸 */
            regval |= (0 << 7) | (0 << 6) | (1 << 5);
            break;

        case U2D_R2L:                           /* 浠庝笂鍒颁笅,浠庡彸鍒板乏 */
            regval |= (0 << 7) | (1 << 6) | (1 << 5);
            break;

        case D2U_L2R:                           /* 浠庝笅鍒颁笂,浠庡乏鍒板彸 */
            regval |= (1 << 7) | (0 << 6) | (1 << 5);
            break;

        case D2U_R2L:                           /* 浠庝笅鍒颁笂,浠庡彸鍒板乏 */
            regval |= (1 << 7) | (1 << 6) | (1 << 5);
            break;
    }

    dirreg = 0x36;                              /* 瀵圭粷澶ч儴鍒嗛┍鍔↖C, 鐢?X36瀵勫瓨鍣ㄦ帶鍒?*/
    
    uint8_t date_send[1] = {regval};
    
    lcd_write_cmd(dirreg);
    lcd_write_data(date_send,1);
    
    if (regval & 0x20)
    {
        if (lcd_self.width < lcd_self.height)   /* 浜ゆ崲X,Y */
        {
            temp = lcd_self.width;
            lcd_self.width = lcd_self.height;
            lcd_self.height = temp;
        }
    }
    else
    {
        if (lcd_self.width > lcd_self.height)   /* 浜ゆ崲X,Y */
        {
            temp = lcd_self.width;
            lcd_self.width = lcd_self.height;
            lcd_self.height = temp;
        }
    }
    
    lcd_set_window(0, 0, lcd_self.width,lcd_self.height);
}

/**
 * @brief       璁剧疆LCD鏄剧ず鏂瑰悜
 * @param       dir:0,绔栧睆; 1,妯睆
 * @retval      鏃?
 */
void lcd_display_dir(uint8_t dir)
{
    lcd_self.dir = dir;
    
    if (lcd_self.dir == 0)                  /* 绔栧睆 */
    {
        lcd_self.width      = 240;
        lcd_self.height     = 320;
        lcd_self.wramcmd    = 0X2C;
        lcd_self.setxcmd    = 0X2A;
        lcd_self.setycmd    = 0X2B;
    }
    else                                    /* 妯睆 */
    {
        lcd_self.width      = 320;          /* 榛樿瀹藉害 */
        lcd_self.height     = 240;          /* 榛樿楂樺害 */
        lcd_self.wramcmd    = 0X2C;
        lcd_self.setxcmd    = 0X2A;
        lcd_self.setycmd    = 0X2B;
    }

    lcd_scan_dir(DFT_SCAN_DIR);             /* 榛樿鎵弿鏂瑰悜 */
}

/**
 * @brief       纭欢澶嶄綅
 * @param       self_in锛歀CD缁撴瀯浣?
 * @retval      鏃?
 */
void lcd_hard_reset(void)
{
    /* 澶嶄綅鏄剧ず灞?*/
    LCD_RST(0);
    vTaskDelay(100);
    LCD_RST(1);
    vTaskDelay(100);
}

/**
 * @brief       缁樼敾涓€涓儚绱犵偣
 * @param       self_in锛歀CD缁撴瀯浣?
 * @param       x锛歺杞村潗鏍?
 * @param       y锛歽杞村潗鏍?
 * @param       color锛氶鑹插€?
 * @retval      鏃?
 */
void lcd_draw_pixel(uint16_t x, uint16_t y, uint16_t color)
{
    lcd_set_cursor(x, y);
    lcd_write_data16(color);
}

/**
 * @brief       鐢荤嚎鍑芥暟(鐩寸嚎銆佹枩绾?
 * @param       x1,y1   璧风偣鍧愭爣
 * @param       x2,y2   缁堢偣鍧愭爣
 * @param       color 濉厖棰滆壊
 * @retval      鏃?
 */
void lcd_draw_line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
    uint16_t t; 
    int xerr = 0, yerr = 0, delta_x, delta_y, distance; 
    
    int incx, incy, urow, ucol; 

    delta_x = x2 - x1;                      /* 璁＄畻鍧愭爣澧為噺 */
    delta_y = y2 - y1; 
    urow = x1; 
    ucol = y1; 
    
    if (delta_x > 0)
    {
        incx = 1;                           /* 璁剧疆鍗曟鏂瑰悜 */
    }
    else if (delta_x == 0)
    {
        incx = 0;                           /* 鍨傜洿绾?*/
    }
    else
    {
        incx =-1;
        delta_x =-delta_x;
    } 
    if(delta_y > 0)
    {
        incy = 1; 
    }
    else if(delta_y == 0)
    {
        incy = 0;                           /* 姘村钩绾?*/
    }
    else
    {
        incy =-1;
        delta_y=-delta_y;
    } 
    
    if( delta_x>delta_y)
    {
        distance = delta_x;                 /* 閫夊彇鍩烘湰澧為噺鍧愭爣杞?*/
    }
    else
    {
        distance = delta_y; 
    }
    
    for (t = 0;t <= distance + 1;t++ )      /* 鐢荤嚎杈撳嚭 */
    {
        lcd_draw_pixel(urow,ucol,color);    /* 鐢荤偣 */ 
        xerr += delta_x ; 
        yerr += delta_y ; 
        
        if(xerr>distance)
        { 
            xerr -= distance; 
            urow += incx; 
        } 
        
        if (yerr > distance)
        { 
            yerr -= distance; 
            ucol += incy; 
        } 
    } 
}

/**
 * @brief       鐢绘按骞崇嚎
 * @param       x0,y0: 璧风偣鍧愭爣
 * @param       len  : 绾块暱搴?
 * @param       color: 鐭╁舰鐨勯鑹?
 * @retval      鏃?
 */
void lcd_draw_hline(uint16_t x, uint16_t y, uint16_t len, uint16_t color)
{
    if ((len == 0) || (x > lcd_self.width) || (y > lcd_self.height))return;

    lcd_fill(x, y, x + len - 1, y, color);
}

/**
 * @brief       鐢讳竴涓煩褰?
 * @param       x1,y1   璧风偣鍧愭爣
 * @param       x2,y2   缁堢偣鍧愭爣
 * @param       color 濉厖棰滆壊
 * @retval      鏃?
 */
void lcd_draw_rectangle(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,uint16_t color)
{
    lcd_draw_line(x0, y0, x1, y0,color);
    lcd_draw_line(x0, y0, x0, y1,color);
    lcd_draw_line(x0, y1, x1, y1,color);
    lcd_draw_line(x1, y0, x1, y1,color);
}

/**
 * @brief       鐢讳竴涓渾
 * @param       x0,y0   鍦嗗績鍧愭爣
 * @param       r   鍦嗗崐寰?
 * @param       color 濉厖棰滆壊
 * @retval      鏃?
 */
void lcd_draw_circle(uint16_t x0, uint16_t y0, uint16_t r, uint16_t color)
{
    int a, b;
    int di;
    a = 0;
    b = r;
    di = 3 - (r << 1);

    while (a <= b)
    {
        lcd_draw_pixel(x0 - b, y0 - a, color);
        lcd_draw_pixel(x0 + b, y0 - a, color);
        lcd_draw_pixel(x0 - a, y0 + b, color);
        lcd_draw_pixel(x0 - b, y0 - a, color);
        lcd_draw_pixel(x0 - a, y0 - b, color);
        lcd_draw_pixel(x0 + b, y0 + a, color);
        lcd_draw_pixel(x0 + a, y0 - b, color);
        lcd_draw_pixel(x0 + a, y0 + b, color);
        lcd_draw_pixel(x0 - b, y0 + a, color);
        a++;

        if (di < 0)
        {
            di += 4 * a + 6;
        }
        else
        {
            di += 10 + 4 * (a - b);
            b--;
        }

        lcd_draw_pixel(x0 + a, y0 + b, color);
    }
}

/**
 * @brief       鍦ㄦ寚瀹氫綅缃樉绀轰竴涓瓧绗?
 * @param       x,y  : 鍧愭爣
 * @param       chr  : 瑕佹樉绀虹殑瀛楃:" "--->"~"
 * @param       size : 瀛椾綋澶у皬 12/16/24/32
 * @param       mode : 鍙犲姞鏂瑰紡(1); 闈炲彔鍔犳柟寮?0);
 * @param       color : 瀛楃鐨勯鑹?
 * @retval      鏃?
 */
void lcd_show_char(uint16_t x, uint16_t y, uint8_t chr, uint8_t size, uint8_t mode, uint16_t color)
{
    uint8_t temp = 0,t1 = 0, t = 0;
    uint8_t *pfont = 0;
    uint8_t csize = 0;                                      /* 寰楀埌瀛椾綋涓€涓瓧绗﹀搴旂偣闃甸泦鎵€鍗犵殑瀛楄妭鏁?*/
    uint16_t colortemp = 0;
    uint8_t sta = 0;

    csize = (size / 8 + ((size % 8) ? 1 : 0)) * (size / 2); /* 寰楀埌瀛椾綋涓€涓瓧绗﹀搴旂偣闃甸泦鎵€鍗犵殑瀛楄妭鏁?*/
    chr = chr - ' ';                                        /* 寰楀埌鍋忕Щ鍚庣殑鍊硷紙ASCII瀛楀簱鏄粠绌烘牸寮€濮嬪彇妯★紝鎵€浠?' '灏辨槸瀵瑰簲瀛楃鐨勫瓧搴擄級 */

    if ((x > (lcd_self.width - size / 2)) || (y > (lcd_self.height - size)))
    {
        return;
    }

    lcd_set_window(x, y, x + size / 2 - 1, y + size - 1);   /* (x,y,x+8-1,y+16-1) */

    switch (size)
    {
        case 12:
            pfont = (uint8_t *)asc2_1206[chr];              /* 璋冪敤1206瀛椾綋 */
            sta = 6;
            break;

        case 16:
            pfont = (uint8_t *)asc2_1608[chr];              /* 璋冪敤1608瀛椾綋 */
            sta = 8;
            break;

        case 24:
            pfont = (uint8_t *)asc2_2412[chr];              /* 璋冪敤2412瀛椾綋 */
            break;

        case 32:
            pfont = (uint8_t *)asc2_3216[chr];              /* 璋冪敤3216瀛椾綋 */
            sta = 8;
            break;

        default:
            return ;
    }

    if (size != 24)
    {
        csize = (size / 8 + ((size % 8) ? 1 : 0)) * (size / 2);
        
        for (t = 0; t < csize; t++)
        {
            temp = pfont[t];                                /* 鑾峰彇瀛楃鐨勭偣闃垫暟鎹?*/

            for (t1 = 0; t1 < sta; t1++)
            {
                if (temp & 0x80)
                {
                    colortemp = color;
                }
                else if (mode == 0)                     /* 鏃犳晥鐐?涓嶆樉绀?*/
                {
                    colortemp = 0xFFFF;
                }

                lcd_write_data16(colortemp);
                temp <<= 1;
            }
        }
    }
    else
    {
        csize = (size * 16) / 8;
        
        for (t = 0; t < csize; t++)
        {
            temp = asc2_2412[chr][t];

            if (t % 2 == 0)
            {
                sta = 8;
            }
            else
            {
                sta = 4;
            }

            for (t1 = 0; t1 < sta; t1++)
            {
                if(temp & 0x80)
                {
                    colortemp = color;
                }
                else if (mode == 0)                         /* 鏃犳晥鐐?涓嶆樉绀?*/
                {
                    colortemp = 0xFFFF;
                }

                lcd_write_data16(colortemp);
                temp <<= 1;
            }
        }
    }
}

/**
 * @brief       m^n鍑芥暟
 * @param       m,n     杈撳叆鍙傛暟
 * @retval      m^n娆℃柟
 */
uint32_t lcd_pow(uint8_t m, uint8_t n)
{
    uint32_t result = 1;

    while(n--)result *= m;

    return result;
}

/**
 * @brief       鏄剧ずlen涓暟瀛?
 * @param       x,y : 璧峰鍧愭爣
 * @param       num : 鏁板€?0 ~ 2^32)
 * @param       len : 鏄剧ず鏁板瓧鐨勪綅鏁?
 * @param       size: 閫夋嫨瀛椾綋 12/16/24/32
 * @retval      鏃?
 */
void lcd_show_num(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size, uint16_t color)
{
    uint8_t t, temp;
    uint8_t enshow = 0;

    for (t = 0; t < len; t++)                                               /* 鎸夋€绘樉绀轰綅鏁板惊鐜?*/
    {
        temp = (num / lcd_pow(10, len - t - 1)) % 10;                       /* 鑾峰彇瀵瑰簲浣嶇殑鏁板瓧 */

        if (enshow == 0 && t < (len - 1))                                   /* 娌℃湁浣胯兘鏄剧ず,涓旇繕鏈変綅瑕佹樉绀?*/
        {
            if (temp == 0)
            {
                lcd_show_char(x + (size / 2)*t, y, ' ', size, 0, color);    /* 鏄剧ず绌烘牸,鍗犱綅 */
                continue;                                                   /* 缁х画涓嬩釜涓€浣?*/
            }
            else
            {
                enshow = 1;                                                 /* 浣胯兘鏄剧ず */
            }

        }

        lcd_show_char(x + (size / 2)*t, y, temp + '0', size, 0, color);     /* 鏄剧ず瀛楃 */
    }
}

/**
 * @brief       鎵╁睍鏄剧ずlen涓暟瀛?楂樹綅鏄?涔熸樉绀?
 * @param       x,y : 璧峰鍧愭爣
 * @param       num : 鏁板€?0 ~ 2^32)
 * @param       len : 鏄剧ず鏁板瓧鐨勪綅鏁?
 * @param       size: 閫夋嫨瀛椾綋 12/16/24/32
 * @param       mode: 鏄剧ず妯″紡
 *              [7]:0,涓嶅～鍏?1,濉厖0.
 *              [6:1]:淇濈暀
 *              [0]:0,闈炲彔鍔犳樉绀?1,鍙犲姞鏄剧ず.
 * @param       color : 鏁板瓧鐨勯鑹?
 * @retval      鏃?
 */
void lcd_show_xnum(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size, uint8_t mode, uint16_t color)
{
    uint8_t t, temp;
    uint8_t enshow = 0;

    for (t = 0; t < len; t++)                                                           /* 鎸夋€绘樉绀轰綅鏁板惊鐜?*/
    {
        temp = (num / lcd_pow(10, len - t - 1)) % 10;                                   /* 鑾峰彇瀵瑰簲浣嶇殑鏁板瓧 */

        if (enshow == 0 && t < (len - 1))                                               /* 娌℃湁浣胯兘鏄剧ず,涓旇繕鏈変綅瑕佹樉绀?*/
        {
            if (temp == 0)
            {
                if (mode & 0X80)                                                        /* 楂樹綅闇€瑕佸～鍏? */
                {
                    lcd_show_char(x + (size / 2)*t, y, '0', size, mode & 0X01, color);  /* 鐢?鍗犱綅 */
                }
                else
                {
                    lcd_show_char(x + (size / 2)*t, y, ' ', size, mode & 0X01, color);  /* 鐢ㄧ┖鏍煎崰浣?*/
                }
                continue;
            }
            else
            {
                enshow = 1;                                                             /* 浣胯兘鏄剧ず */
            }
        }
        lcd_show_char(x + (size / 2)*t, y, temp + '0', size, mode & 0X01, color);
    }
}


/**
 * @brief       鏄剧ず瀛楃涓?
 * @param       x,y         : 璧峰鍧愭爣
 * @param       width,height: 鍖哄煙澶у皬
 * @param       size        : 閫夋嫨瀛椾綋 12/16/24/32
 * @param       p           : 瀛楃涓查鍦板潃
 * @retval      鏃?
 */
void lcd_show_string(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t size, char *p, uint16_t color)
{
    uint8_t x0 = x;
    width += x;
    height += y;

    while ((*p <= '~') && (*p >= ' '))   /* 鍒ゆ柇鏄笉鏄潪娉曞瓧绗? */
    {
        if (x >= width)
        {
            x = x0;
            y += size;
        }

        if (y >= height)break;  /* 閫€鍑?*/

        lcd_show_char(x, y, *p, size, 0, color);
        x += size / 2;
        p++;
    }
}

/**
 * @brief       鎵撳紑LCD
 * @param       self_in锛歋PI鎺у埗鍧?
 * @retval      mp_const_none锛氬垵濮嬪寲鎴愬姛
 */
void lcd_on(void)
{
    LCD_PWR(1);
    vTaskDelay(10);
}

/**
 * @brief       鍏抽棴LCD
 * @param       self_in锛歋PI鎺у埗鍧?
 * @retval      mp_const_none锛氬垵濮嬪寲鎴愬姛
 */
void lcd_off(void)
{
    LCD_PWR(0);
    vTaskDelay(10);
}

/**
 * @brief       LCD鍒濆鍖?
 * @param       鏃?
 * @retval      鏃?
 */
void lcd_init(void)
{
    int cmd = 0;
    esp_err_t ret = 0;
    
    lcd_self.dir = 0;
    lcd_self.wr = LCD_NUM_WR;                                       /* 閰嶇疆WR寮曡剼 */
    lcd_self.cs = LCD_NUM_CS;                                       /* 閰嶇疆CS寮曡剼 */
    
    gpio_config_t gpio_init_struct;

    /* SPI椹卞姩鎺ュ彛閰嶇疆 */
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 60 * 1000 * 1000,                         /* SPI鏃堕挓 */
        .mode = 0,                                                  /* SPI妯″紡0 */
        .spics_io_num = lcd_self.cs,                                /* SPI璁惧寮曡剼 */
        .queue_size = 7,                                            /* 浜嬪姟闃熷垪灏哄 7涓?*/
    };
    
    /* 娣诲姞SPI鎬荤嚎璁惧 */
    ret = spi_bus_add_device(SPI2_HOST, &devcfg, &MY_LCD_Handle);   /* 閰嶇疆SPI鎬荤嚎璁惧 */
    ESP_ERROR_CHECK(ret);

    gpio_init_struct.intr_type = GPIO_INTR_DISABLE;                 /* 澶辫兘寮曡剼涓柇 */
    gpio_init_struct.mode = GPIO_MODE_OUTPUT;                       /* 閰嶇疆杈撳嚭妯″紡 */
    gpio_init_struct.pin_bit_mask = 1ull << lcd_self.wr;            /* 閰嶇疆寮曡剼浣嶆帺鐮?*/
    gpio_init_struct.pull_down_en = GPIO_PULLDOWN_DISABLE;          /* 澶辫兘涓嬫媺 */
    gpio_init_struct.pull_up_en = GPIO_PULLUP_ENABLE;               /* 浣胯兘涓嬫媺 */
    gpio_config(&gpio_init_struct);                                 /* 寮曡剼閰嶇疆 */

    lcd_hard_reset();                                               /* LCD纭欢澶嶄綅 */

    /* 鍒濆鍖栦唬鐮?*/
#if SPI_LCD_TYPE                                                    /* 瀵?.4瀵窵CD瀵勫瓨鍣ㄨ繘琛岃缃?*/
    lcd_init_cmd_t ili_init_cmds[] =
    {
        {0x11, {0}, 0x80},
        {0x36, {0x00}, 1},
        {0x3A, {0x65}, 1},
        {0X21, {0}, 0x80},
        {0x29, {0}, 0x80},
        {0, {0}, 0xff},
    };

#else                                                               /* 涓嶄负0鍒欒涓轰娇鐢?.3瀵窼PILCD灞忥紝閭ｄ箞灞忓箷灏嗕笉浼氬弽鏄?*/
    lcd_init_cmd_t ili_init_cmds[] =
    {
        {0x11, {0}, 0x80},
        {0x36, {0x00}, 1},
        {0x3A, {0x65}, 1},
        {0xB2, {0x0C, 0x0C, 0x00, 0x33,0x33}, 5},
        {0xB7, {0x75}, 1},
        {0xBB, {0x1C}, 1},
        {0xC0, {0x2c}, 1},
        {0xC2, {0x01}, 1},
        {0xC3, {0x0F}, 1},
        {0xC4, {0x20}, 1},
        {0xC6, {0X01}, 1},
        {0xD0, {0xA4,0xA1}, 2},
        {0xE0, {0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B, 0x3F, 0x54, 0x4C, 0x18, 0x0D, 0x0B, 0x1F, 0x23}, 14},
        {0xE1, {0xD0, 0x04, 0x0C, 0x11, 0x13, 0x2C, 0x3F, 0x44, 0x51, 0x2F, 0x1F, 0x1F, 0x20, 0x23}, 14},
        {0X21, {0}, 0x80},
        {0x29, {0}, 0x80},
        {0, {0}, 0xff},
    };
#endif

    /* 寰幆鍙戦€佽缃墍鏈夊瘎瀛樺櫒 */
    while (ili_init_cmds[cmd].databytes != 0xff)
    {
        lcd_write_cmd(ili_init_cmds[cmd].cmd);
        lcd_write_data(ili_init_cmds[cmd].data, ili_init_cmds[cmd].databytes & 0x1F);
        
        if (ili_init_cmds[cmd].databytes & 0x80)
        {
            vTaskDelay(120);
        }
        
        cmd++;
    }

    lcd_display_dir(1);                                             /* 璁剧疆灞忓箷鏂瑰悜 */
    LCD_PWR(1);
    /* 初始化完清成黑色而不是白色,避免屏幕闪一下白屏 */
    lcd_clear(BLACK);                                               /* 娓呭睆 */
}
