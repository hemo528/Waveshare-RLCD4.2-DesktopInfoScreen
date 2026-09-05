# gen_icons.py —— 天气图标光栅化器（纯 Python 无依赖）
#
# 生成 40×40 双帧 1-bit 位图：帧 A / 帧 B 由 LVGL 定时器交替显示形成"动态"效果
# （雨滴下落、雪飘、 rays 旋转、闪电闪烁）。输出：
#   1) main/ui/wx_icons.c / wx_icons.h  —— LVGL lv_img_dsc_t（RGB565：黑=0x0000 白=0xFFFF，
#      与 flush 回调的二值化阈值一致：value < 0x8000 → 黑）
#   2) tools/sim/out/icon_<name>.png    —— 预览（A|B 两帧并排）
import math, os, sys
sys.path.insert(0, os.path.dirname(__file__))
from png1bit import save_png

W = H = 40
OUT_DIR = os.path.join(os.path.dirname(__file__), 'out')
UI_DIR = r'C:\Espressif\projects\DesktopInfoScreen\main\ui'

def blank():
    return [[1] * W for _ in range(H)]

def fill_circle(c, cx, cy, r, col=0):
    for y in range(max(0, int(cy - r) - 1), min(H, int(cy + r) + 2)):
        for x in range(max(0, int(cx - r) - 1), min(W, int(cx + r) + 2)):
            if (x - cx) ** 2 + (y - cy) ** 2 <= r * r:
                c[y][x] = col

def fill_rect(c, x1, y1, x2, y2, col=0):
    for y in range(max(0, int(y1)), min(H, int(y2) + 1)):
        for x in range(max(0, int(x1)), min(W, int(x2) + 1)):
            c[y][x] = col

def seg(c, x1, y1, x2, y2, w=2.6):
    """距离场粗线段"""
    r = w / 2.0
    x1, y1, x2, y2 = float(x1), float(y1), float(x2), float(y2)
    for y in range(max(0, int(min(y1, y2) - r) - 1), min(H, int(max(y1, y2) + r) + 2)):
        for x in range(max(0, int(min(x1, x2) - r) - 1), min(W, int(max(x1, x2) + r) + 2)):
            dx, dy = x2 - x1, y2 - y1
            L2 = dx * dx + dy * dy
            t = 0.0 if L2 == 0 else max(0.0, min(1.0, ((x - x1) * dx + (y - y1) * dy) / L2))
            px, py = x1 + t * dx, y1 + t * dy
            if (x - px) ** 2 + (y - py) ** 2 <= r * r:
                c[y][x] = 0

def poly(c, pts):
    """扫描线多边形填充"""
    ys = [p[1] for p in pts]
    for y in range(max(0, int(min(ys))), min(H, int(max(ys)) + 1)):
        xs = []
        n = len(pts)
        for i in range(n):
            x1, y1 = pts[i]
            x2, y2 = pts[(i + 1) % n]
            if (y1 <= y < y2) or (y2 <= y < y1):
                xs.append(x1 + (y - y1) * (x2 - x1) / (y2 - y1))
        xs.sort()
        for i in range(0, len(xs) - 1, 2):
            fill_rect(c, xs[i], y, xs[i + 1], y)

def _stamp_union(m, shapes_circles, rect):
    for (x, y, r) in shapes_circles:
        for yy in range(max(0, int(y - r) - 1), min(H, int(y + r) + 2)):
            for xx in range(max(0, int(x - r) - 1), min(W, int(x + r) + 2)):
                if (xx - x) ** 2 + (yy - y) ** 2 <= r * r:
                    m[yy][xx] = 1
    x1, y1, x2, y2 = rect
    for yy in range(max(0, int(y1)), min(H, int(y2) + 1)):
        for xx in range(max(0, int(x1)), min(W, int(x2) + 1)):
            m[yy][xx] = 1

def _erode(m, passes=2):
    for _ in range(passes):
        nm = [row[:] for row in m]
        for y in range(H):
            for x in range(W):
                if m[y][x]:
                    for dy in (-1, 0, 1):
                        for dx in (-1, 0, 1):
                            yy, xx = y + dy, x + dx
                            if not (0 <= yy < H and 0 <= xx < W) or not m[yy][xx]:
                                nm[y][x] = 0
        m = nm
    return m

def cloud(c, cx, cy, s=1.0, outline=True):
    """云朵：三圆 + 底带的并集；outline=True 时做真形态学腐蚀取描边（无杂点）"""
    cs = [(cx - 7.5 * s, cy + 1.5 * s, 6.5 * s),
          (cx + 0.5 * s, cy - 2.0 * s, 7.8 * s),
          (cx + 7.0 * s, cy + 2.0 * s, 5.8 * s)]
    rect = (cx - 14 * s, cy + 2.5 * s, cx + 12 * s, cy + 9 * s)
    m = [[0] * W for _ in range(H)]
    _stamp_union(m, cs, rect)
    _stamp_union_grid_to(c, m, 0)
    if outline:
        inner = _erode(m, 2)
        _stamp_union_grid_to(c, inner, 1)

def _stamp_union_grid_to(c, m, col):
    for y in range(H):
        for x in range(W):
            if m[y][x]:
                c[y][x] = col

def sun(c, cx, cy, disc, r0, r1, phase, w=3.0):
    if disc:
        fill_circle(c, cx, cy, disc)
    for i in range(8):
        a = phase + i * math.pi / 4
        seg(c, cx + r0 * math.cos(a), cy + r0 * math.sin(a),
               cx + r1 * math.cos(a), cy + r1 * math.sin(a), w)

def drop(c, x, y, dy=0):
    seg(c, x, y + dy, x - 2.5, y + 6 + dy, 2.4)

def flake(c, x, y, dy=0):
    seg(c, x - 3, y + dy, x + 3, y + dy, 1.8)
    seg(c, x, y - 3 + dy, x, y + 3 + dy, 1.8)

# ---------------- 各图标双帧 ----------------
def frames():
    F = {}
    # 晴：rays 缓旋
    a, b = blank(), blank()
    sun(a, 20, 20, 7.5, 10.5, 15.5, 0)
    sun(b, 20, 20, 7.5, 10.5, 15.5, math.pi / 8)
    F['sunny'] = (a, b)
    # 多云：小 sun + 云描边
    a, b = blank(), blank()
    sun(a, 26, 12, 4.5, 6.5, 9.5, 0, w=2.4)
    sun(b, 26, 12, 4.5, 6.5, 9.5, math.pi / 8, w=2.4)
    cloud(a, 16, 24, 1.0); cloud(b, 16, 24, 1.0)
    F['partly'] = (a, b)
    # 阴：双云
    a, b = blank(), blank()
    cloud(a, 24, 13, 0.8); cloud(a, 16, 25, 1.0)
    cloud(b, 24, 13, 0.8); cloud(b, 16, 25, 1.0)
    F['overcast'] = (a, b)
    # 雾：云 + 横线漂移
    a, b = blank(), blank()
    cloud(a, 20, 12, 0.9); cloud(b, 20, 12, 0.9)
    for i, y in enumerate((27, 31, 35)):
        dx = (-2, 0, 2)[i]
        seg(a, 8 + dx, y, 32 + dx, y, 2.2)
        dx2 = (2, 0, -2)[i]
        seg(b, 8 + dx2, y, 32 + dx2, y, 2.2)
    F['fog'] = (a, b)
    # 小雨：3 滴下落
    a, b = blank(), blank()
    cloud(a, 20, 13, 1.0); cloud(b, 20, 13, 1.0)
    for x in (13, 20, 27):
        drop(a, x, 28); drop(b, x, 30)
    F['rain_l'] = (a, b)
    # 大雨：4 滴更长
    a, b = blank(), blank()
    cloud(a, 20, 12, 1.0); cloud(b, 20, 12, 1.0)
    for x in (11, 17, 23, 29):
        seg(a, x, 27, x - 2.5, 36, 2.4); seg(b, x, 29, x - 2.5, 38, 2.4)
    F['rain_h'] = (a, b)
    # 冻雨：2 滴 + 1 雪花
    a, b = blank(), blank()
    cloud(a, 20, 13, 1.0); cloud(b, 20, 13, 1.0)
    drop(a, 13, 28); drop(b, 13, 30)
    drop(a, 27, 28); drop(b, 27, 30)
    flake(a, 20, 32); flake(b, 20, 34)
    F['sleet'] = (a, b)
    # 雪：3 雪花下飘
    a, b = blank(), blank()
    cloud(a, 20, 12, 1.0); cloud(b, 20, 12, 1.0)
    for x, y in ((13, 30), (20, 33), (27, 29)):
        flake(a, x, y); flake(b, x, y + 2)
    F['snow'] = (a, b)
    # 雷阵雨：云 + 闪电（闪烁下移）
    a, b = blank(), blank()
    cloud(a, 20, 11, 0.95); cloud(b, 20, 11, 0.95)
    bolt = [(22, 24), (15, 32), (19.5, 32), (17, 39), (26, 29), (21.5, 29), (25, 24)]
    bolt2 = [(x, y + 1.5) for (x, y) in bolt]
    poly(a, bolt); poly(b, bolt2)
    F['thunder'] = (a, b)
    return F

ICONS = ['sunny', 'partly', 'overcast', 'fog', 'rain_l', 'rain_h', 'sleet', 'snow', 'thunder']

# ---------------- C 输出 ----------------
def frame_bytes(f):
    """40×40 的 0/1 网格 → RGB565 小端字节（黑=0x0000 白=0xFFFF）"""
    out = bytearray()
    for row in f:
        for v in row:
            out += b'\x00\x00' if v == 0 else b'\xff\xff'
    return out

def emit_c(F):
    lines = []
    lines.append('// wx_icons.c —— 由 tools/sim/gen_icons.py 自动生成，勿手改')
    lines.append('// 40×40 双帧动态天气图标；黑=0x0000 白=0xFFFF（与 flush 二值化一致）')
    lines.append('#include "wx_icons.h"')
    lines.append('')
    decls = []
    for name in ICONS:
        for tag in ('a', 'b'):
            data = frame_bytes(F[name][0 if tag == 'a' else 1])
            arr = f'icon_{name}_{tag}_data'
            decls.append((name, tag, arr))
            hexs = ','.join(f'0x{b:02x}' for b in data)
            lines.append(f'static const uint8_t {arr}[] = {{{hexs}}};')
            lines.append('')
    for name, tag, arr in decls:
        lines.append(
            f'const lv_img_dsc_t icon_{name}_{tag} = {{\n'
            f'    .header = {{ .cf = LV_IMG_CF_TRUE_COLOR, .always_zero = 0, .reserved = 0, .w = {W}, .h = {H} }},\n'
            f'    .data_size = {W * H * 2},\n'
            f'    .data = {arr},\n'
            f'}};')
        lines.append('')
    # wx_icon_frame：枚举值 → 帧 A/B 图源
    lines.append('static const lv_img_dsc_t *const icon_tab[WX_ICON_COUNT][2] = {')
    for name in ICONS:
        lines.append(f'    {{ &icon_{name}_a, &icon_{name}_b }},')
    lines.append('};')
    lines.append('')
    lines.append('const lv_img_dsc_t *wx_icon_frame(int icon, int frame)')
    lines.append('{')
    lines.append('    if (icon < 0 || icon >= WX_ICON_COUNT) icon = WX_ICON_OVERCAST;')
    lines.append('    return icon_tab[icon][frame & 1];')
    lines.append('}')
    with open(os.path.join(UI_DIR, 'wx_icons.c'), 'w', encoding='utf-8') as fp:
        fp.write('\n'.join(lines))

HEADER = '''// wx_icons.h —— 由 tools/sim/gen_icons.py 自动生成，勿手改
#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// 图标枚举（data_store.wx_icon 使用）
enum {
    WX_ICON_SUNNY = 0,
    WX_ICON_PARTLY,
    WX_ICON_OVERCAST,
    WX_ICON_FOG,
    WX_ICON_RAIN_L,
    WX_ICON_RAIN_H,
    WX_ICON_SLEET,
    WX_ICON_SNOW,
    WX_ICON_THUNDER,
    WX_ICON_COUNT,
};

extern const lv_img_dsc_t icon_sunny_a,    icon_sunny_b;
extern const lv_img_dsc_t icon_partly_a,   icon_partly_b;
extern const lv_img_dsc_t icon_overcast_a, icon_overcast_b;
extern const lv_img_dsc_t icon_fog_a,      icon_fog_b;
extern const lv_img_dsc_t icon_rain_l_a,   icon_rain_l_b;
extern const lv_img_dsc_t icon_rain_h_a,   icon_rain_h_b;
extern const lv_img_dsc_t icon_sleet_a,    icon_sleet_b;
extern const lv_img_dsc_t icon_snow_a,     icon_snow_b;
extern const lv_img_dsc_t icon_thunder_a,  icon_thunder_b;

// wx_icon 枚举值 → 帧 A/B 图源
const lv_img_dsc_t *wx_icon_frame(int icon, int frame);

#ifdef __cplusplus
}
#endif
'''

def emit_h():
    with open(os.path.join(UI_DIR, 'wx_icons.h'), 'w', encoding='utf-8') as fp:
        fp.write(HEADER)

if __name__ == '__main__':
    F = frames()
    os.makedirs(OUT_DIR, exist_ok=True)
    emit_h()
    emit_c(F)
    # 预览：A | B 并排，4px 白色分隔
    for name in ICONS:
        a, b = F[name]
        comp = [row + [1] * 4 + row2 for row, row2 in zip(a, b)]
        save_png(os.path.join(OUT_DIR, f'icon_{name}.png'), comp, W * 2 + 4, H)
    print('icons generated:', ', '.join(ICONS))
