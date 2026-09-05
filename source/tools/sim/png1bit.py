# png1bit.py —— 最小 1-bit PNG 写入器（纯标准库，0=黑 1=白）
import zlib, struct, os

# 镜像输出目录：每次 save_png 都会同步一份到微雪文件夹（用户要求）
MIRROR_DIR = r'C:\Users\hemo\Desktop\微雪\仿真图'

def save_png(path, pixels, w, h):
    """pixels: list of h rows, each row list of w ints (0=黑,1=白)。输出 8-bit 灰度 PNG。
    同时镜像一份到 MIRROR_DIR（同名文件）。"""
    raw = bytearray()
    for row in pixels:
        raw.append(0)  # filter type 0
        for v in row:
            raw.append(0x00 if v == 0 else 0xFF)
    def chunk(tag, data):
        c = struct.pack('>I', len(data)) + tag + data
        return c + struct.pack('>I', zlib.crc32(tag + data) & 0xFFFFFFFF)
    png = b'\x89PNG\r\n\x1a\n'
    png += chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 0, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(bytes(raw), 6))
    png += chunk(b'IEND', b'')
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(png)
    # 镜像到微雪文件夹
    try:
        os.makedirs(MIRROR_DIR, exist_ok=True)
        with open(os.path.join(MIRROR_DIR, os.path.basename(path)), 'wb') as f:
            f.write(png)
    except OSError as e:
        print(f'[png1bit] 镜像到 {MIRROR_DIR} 失败: {e}')
