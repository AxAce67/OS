import sys
import os
from PIL import Image, ImageDraw, ImageFont

def generate_font():
    font_data = [[0]*16 for _ in range(256)]
    
    # Windowsの標準等幅フォント(Consolas)を利用して8x16フォントを生成
    font_path = "C:/Windows/Fonts/consola.ttf"
    if not os.path.exists(font_path):
        font_path = "C:/Windows/Fonts/cour.ttf" # Courier New fallback
        
    try:
        # サイズ16でフォントをロード
        font = ImageFont.truetype(font_path, 16)
    except:
        font = ImageFont.load_default()

    # ASCII印字可能文字 (32-126) を画像化してドットとして取得
    for i in range(32, 127):
        img = Image.new('L', (8, 16), color=0)
        draw = ImageDraw.Draw(img)
        # 上下に少しオフセットをつけて中央寄りに描画
        draw.text((0, -2), chr(i), fill=255, font=font)
        
        for y in range(16):
            row_val = 0
            for x in range(8):
                if img.getpixel((x, y)) > 32:
                    row_val |= (1 << (7 - x)) # 上位ビット側からピクセルを詰める
            font_data[i][y] = row_val

    # C言語の配列としてファイルに書き出し
    with open('c:/os/font.c', 'w') as f:
        f.write('#include <stdint.h>\n\n')
        f.write('const uint8_t kFont[256][16] = {\n')
        for i in range(256):
            f.write('    {')
            f.write(', '.join(f'0x{val:02x}' for val in font_data[i]))
            f.write('},\n')
        f.write('};\n')

if __name__ == '__main__':
    generate_font()
