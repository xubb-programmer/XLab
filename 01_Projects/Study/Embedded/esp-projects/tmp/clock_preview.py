"""生成时钟界面预览图(2 倍放大,极简风格:纯黑背景 + 7段数码管)"""
from PIL import Image, ImageDraw

S = 2  # 缩放倍数
W, H = 320 * S, 240 * S
img = Image.new('RGB', (W, H))
d = ImageDraw.Draw(img)

def fill(x0, y0, x1, y1, color):
    d.rectangle([x0*S, y0*S, x1*S+1, y1*S+1], fill=color)

def hline(x, y, length, color):
    d.rectangle([x*S, y*S, (x+length)*S+1, y*S+1], fill=color)

def rgb(r, g, b):
    return (r, g, b)

# ---- 纯黑背景 ----
fill(0, 0, 319, 239, (0, 0, 0))

# ---- 7 段数码管 ----
DW, DH, ST, GAP, CW = 36, 62, 6, 6, 10
TX0, TY = 27, 96
D0, D1 = TX0, TX0 + DW + GAP
C1 = D1 + DW + GAP - 4
D2 = C1 + CW + 4
D3 = D2 + DW + GAP
C2 = D3 + DW + GAP - 4
D4 = C2 + CW + 4
D5 = D4 + DW + GAP

SEG = {  # (x,y,w,h) 相对数字格
   0: (6, 0, DW-12, ST),
   1: (DW-ST, 6, ST, DH//2-9),
   2: (DW-ST, DH//2+3, ST, DH//2-9),
   3: (6, DH-ST, DW-12, ST),
   4: (0, DH//2+3, ST, DH//2-9),
   5: (0, 6, ST, DH//2-9),
   6: (6, DH//2-ST//2, DW-12, ST),
}
MASK = {0:0x3F,1:0x06,2:0x5B,3:0x4F,4:0x66,
        5:0x6D,6:0x7D,7:0x07,8:0x7F,9:0x6F}

def digit(x, y, ch):
    m = MASK.get(int(ch), 0)
    for seg, box in SEG.items():
        c = rgb(52,56,86) if not (m & (1<<seg)) else rgb(255,175,0)
        fill(x+box[0], y+box[1], x+box[0]+box[2]-1, y+box[1]+box[3]-1, c)

def colon(x, y):
    fill(x, y+DH//4, x+CW-4, y+DH//4+5, rgb(255,175,0))
    fill(x, y+DH*3//4-5, x+CW-4, y+DH*3//4, rgb(255,175,0))

for pos, ch in [(D0,'1'),(D1,'2'),(D2,'0'),(D3,'0'),(D4,'1'),(D5,'0')]:
    digit(pos, TY, ch)
colon(C1, TY)
colon(C2, TY)

# ---- WiFi 状态圆点(右上角,绿色=已连接) ----
CX, CY, DOT_R = 306, 14, 7
GREEN = (80, 220, 120)
d.ellipse([(CX-DOT_R)*S, (CY-DOT_R)*S, (CX+DOT_R)*S+1, (CY+DOT_R)*S+1], fill=GREEN)

img.save(r'D:\XLab\XProjects\esp-projects\tmp\clock_ui_preview.png')
print('saved')
