import os
from PIL import Image, ImageDraw, ImageFont

def draw_glyph_to_8x16(font, ch):
    img = Image.new('L', (8, 16), color=0)
    draw = ImageDraw.Draw(img)
    # Vertical offset keeps ASCII and kana tops from clipping as much as possible.
    draw.text((0, 0), ch, fill=255, font=font)
    rows = [0] * 16
    for y in range(16):
        row_val = 0
        for x in range(8):
            if img.getpixel((x, y)) > 120:
                row_val |= (1 << (7 - x))
        rows[y] = row_val
    return rows

def generate_font():
    font_data = [[0]*16 for _ in range(256)]
    
    # Windows標準フォントを優先して利用（半角カナ対応を優先）
    candidates = [
        "C:/Windows/Fonts/msgothic.ttc",  # MS Gothic
        "C:/Windows/Fonts/meiryo.ttc",    # Meiryo
        "C:/Windows/Fonts/consola.ttf",   # Consolas
        "C:/Windows/Fonts/lucon.ttf",     # Lucida Console
        "C:/Windows/Fonts/cour.ttf",      # Courier New
    ]
    font_path = None
    for path in candidates:
        if os.path.exists(path):
            font_path = path
            break

    try:
        # 8x16セルで上切れしにくいサイズに固定
        if font_path is None:
            raise FileNotFoundError("no font file")
        font = ImageFont.truetype(font_path, 12)
    except Exception:
        font = ImageFont.load_default()

    # ASCII印字可能文字 (32-126) を画像化してドットとして取得
    for i in range(32, 127):
        font_data[i] = draw_glyph_to_8x16(font, chr(i))

    # JIS X 0201 halfwidth kana/punctuation (0xA1-0xDF -> U+FF61-U+FF9F)
    for i in range(0xA1, 0xE0):
        uni = 0xFF61 + (i - 0xA1)
        font_data[i] = draw_glyph_to_8x16(font, chr(uni))

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
