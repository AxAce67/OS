import sys
import os
from PIL import Image, ImageDraw, ImageFont

def generate_font():
    font_data = [[0]*16 for _ in range(256)]
    
    # Windows標準の等幅フォントを優先して利用
    candidates = [
        "C:/Windows/Fonts/consola.ttf",  # Consolas
        "C:/Windows/Fonts/lucon.ttf",    # Lucida Console
        "C:/Windows/Fonts/cour.ttf",     # Courier New
    ]
    font_path = None
    for path in candidates:
        if os.path.exists(path):
            font_path = path
            break

    try:
        # 8x16に収まりやすいサイズで生成
        if font_path is None:
            raise FileNotFoundError("no font file")
        font = ImageFont.truetype(font_path, 15)
    except:
        font = ImageFont.load_default()

    # ASCII印字可能文字 (32-126) を画像化してドットとして取得
    for i in range(32, 127):
        img = Image.new('L', (8, 16), color=0)
        draw = ImageDraw.Draw(img)
        # 太りすぎを避けるため少し上寄せで描画
        draw.text((0, -3), chr(i), fill=255, font=font)
        
        for y in range(16):
            row_val = 0
            for x in range(8):
                if img.getpixel((x, y)) > 96:
                    row_val |= (1 << (7 - x)) # 上位ビット側からピクセルを詰める
            font_data[i][y] = row_val

    # C言語の配列としてファイルに書き出し
    out_path = os.path.join('c:/os', 'kernel', 'graphics', 'font.c')
    with open(out_path, 'w') as f:
        f.write('#include <stdint.h>\n')
        f.write('#include "font.h"\n\n')
        f.write('const uint8_t kFont[256][16] = {\n')
        for i in range(256):
            f.write('    {')
            f.write(', '.join(f'0x{val:02x}' for val in font_data[i]))
            f.write('},\n')
        f.write('};\n')

if __name__ == '__main__':
    generate_font()
