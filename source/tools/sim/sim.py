# sim.py —— 开发板屏幕像素级仿真器 v2（纯 Python 无依赖）
#
# 与固件共用同一套数据源：字库 .c 文件（lv_font_conv 产物）与 wx_icons（gen_icons 生成），
# 按 LVGL v8 的精确绘制公式渲染：
#   glyph_x = pen_x + ofs_x
#   glyph_y = pen_y + (line_height - base_line) - box_h - ofs_y     （lv_draw_sw_letter.c:131）
#   advance = (adv_w + 8) >> 4                                      （lv_font_fmt_txt.c:177，16.4 定点）
#   bpp<8 字模为连续位流（行末不补齐字节）
#
# 三重验证（每个场景都做）：
#   1) PNG 输出 out/screen_<场景>.png —— 人眼看
#   2) 元素登记表：每个元素的实测墨迹框必须落在其预定矩形内（无越界/贴边）
#   3) 同一父容器内兄弟元素墨迹框两两相交检测（无重叠）+ ASCII 降采样大图人工核对
import os, re, sys
sys.path.insert(0, os.path.dirname(__file__))
from png1bit import save_png
from gen_icons import frames as icon_frames

UI = r'C:\Espressif\projects\DesktopInfoScreen\main\ui'
OUT = os.path.join(os.path.dirname(__file__), 'out')
SCR_W, SCR_H = 400, 300

# ---------------- 字库解析 ----------------
def _nums(block):
    return [int(x, 0) for x in re.findall(r'-?\b0x[0-9a-fA-F]+|-?\b\d+', block)]

def parse_font(path):
    t = open(path, encoding='utf-8').read()
    t = re.sub(r'/\*.*?\*/', '', t, flags=re.S)          # 剥掉注释，避免 U+0020 之类被当数字
    bmp = _nums(re.search(r'glyph_bitmap\[\]\s*=\s*\{(.*?)\};', t, re.S).group(1))
    font = {'bmp': bmp, 'glyphs': {}, 'cmap': {}}

    m = re.search(r'\.line_height\s*=\s*(\d+)', t); font['lh'] = int(m.group(1))
    m = re.search(r'\.base_line\s*=\s*(\d+)', t);   font['bl'] = int(m.group(1))

    dsc_blocks = re.findall(r'\{([^{}]*?)\}', re.search(r'glyph_dsc\[\]\s*=\s*\{(.*?)\n\};', t, re.S).group(1), re.S)
    glyph_dsc = []
    for b in dsc_blocks:
        if '.bitmap_index' not in b:
            continue
        g = dict(re.findall(r'\.(\w+)\s*=\s*(-?\w+)', b))
        glyph_dsc.append({k: int(v, 0) for k, v in g.items()})
    font['dsc'] = glyph_dsc

    cm_zone = re.search(r'cmaps\[\]\s*=\s*\{(.*?)\n\};', t, re.S).group(1)
    for blk in re.findall(r'\{([^{}]*?)\}', cm_zone, re.S):
        f = dict(re.findall(r'\.(\w+)\s*=\s*([\w_]+)', blk))
        rs, rl = int(f['range_start'], 0), int(f['range_length'], 0)
        gstart = int(f['glyph_id_start'], 0)
        if f.get('unicode_list', 'NULL') == 'NULL':
            for i in range(rl):
                font['cmap'][rs + i] = gstart + i
        else:
            arr_name = f['unicode_list']
            offs = _nums(re.search(re.escape(arr_name) + r'\[\]\s*=\s*\{(.*?)\};', t, re.S).group(1))
            # SPARSE_TINY：unicode_list 存的是相对 range_start 的绝对偏移（lv_font_fmt_txt.c 按偏移二分查找）
            for i, v in enumerate(offs):
                font['cmap'][rs + v] = gstart + i
    return font

def glyph(font, ch):
    gi = font['cmap'].get(ord(ch))
    if gi is None or gi >= len(font['dsc']):
        return None
    return font['dsc'][gi]

def text_width(font, s):
    w = 0
    for ch in s:
        g = glyph(font, ch) or font['dsc'][0]
        w += (g['adv_w'] + 8) >> 4
    return w

def draw_glyph(canvas, font, ch, px, py):
    g = glyph(font, ch)
    if g is None:
        g = font['dsc'][0]
    bw, bh = g['box_w'], g['box_h']
    if bw == 0 or bh == 0:
        return
    gx = px + g['ofs_x']
    gy = py + (font['lh'] - font['bl']) - bh - g['ofs_y']
    base_bit = g['bitmap_index'] * 8
    bmp = font['bmp']
    for row in range(bh):
        yy = gy + row
        if not (0 <= yy < SCR_H):
            continue
        for col in range(bw):
            bitpos = base_bit + row * bw + col
            if (bmp[bitpos >> 3] >> (7 - (bitpos & 7))) & 1:
                xx = gx + col
                if 0 <= xx < SCR_W:
                    canvas[yy][xx] = 0

def draw_label(canvas, font, s, x, y, width=None, align='left', val=0):
    tw = text_width(font, s)
    if width is not None:
        if align == 'center':
            x += (width - tw) // 2
        elif align == 'right':
            x += width - tw
    for ch in s:
        draw_glyph_v(canvas, font, ch, x, y, val)
        x += (glyph(font, ch) or font['dsc'][0])['adv_w'] + 8 >> 4
    return tw

def draw_glyph_v(canvas, font, ch, px, py, val=0):
    """draw_glyph 带颜色版本：val=0 黑字（默认），val=1 白字（黑底高亮内用）"""
    g = glyph(font, ch)
    if g is None:
        g = font['dsc'][0]
    bw, bh = g['box_w'], g['box_h']
    if bw == 0 or bh == 0:
        return
    gx = px + g['ofs_x']
    gy = py + (font['lh'] - font['bl']) - bh - g['ofs_y']
    base_bit = g['bitmap_index'] * 8
    bmp = font['bmp']
    for row in range(bh):
        yy = gy + row
        if not (0 <= yy < SCR_H):
            continue
        for col in range(bw):
            bitpos = base_bit + row * bw + col
            if (bmp[bitpos >> 3] >> (7 - (bitpos & 7))) & 1:
                xx = gx + col
                if 0 <= xx < SCR_W:
                    canvas[yy][xx] = val

def draw_panel(canvas, x, y, w, h, border=2):
    for i in range(border):
        for xx in range(x + i, x + w - i):
            canvas[y + i][xx] = 0
            canvas[y + h - 1 - i][xx] = 0
        for yy in range(y + i, y + h - i):
            canvas[yy][x + i] = 0
            canvas[yy][x + w - 1 - i] = 0

def draw_bar(canvas, x, y, w, h, pct):
    draw_panel(canvas, x, y, w, h, border=1)
    ind = (pct * (w - 2)) // 100
    if ind > 0:
        for yy in range(y + 1, y + h - 1):
            for xx in range(x + 1, x + 1 + ind):
                canvas[yy][xx] = 0

def blit_icon(canvas, f40, ox, oy):
    for y in range(40):
        for x in range(40):
            if f40[y][x] == 0:
                canvas[oy + y][ox + x] = 0

def ink_bbox(canvas, rect):
    x1, y1, x2, y2 = rect
    minx, miny, maxx, maxy = 10 ** 9, 10 ** 9, -1, -1
    for y in range(max(0, y1), min(SCR_H, y2)):
        for x in range(max(0, x1), min(SCR_W, x2)):
            if canvas[y][x] == 0:
                minx, miny = min(minx, x), min(miny, y)
                maxx, maxy = max(maxx, x), max(maxy, y)
    return None if maxx < 0 else (minx, miny, maxx, maxy)

# ---------------- 元素登记与三重检测 ----------------
ELEMS = []   # [parent, name, intended_rect, ink_bbox]

def grab(canvas, rect):
    """绘制前快照矩形区域，供差分测量"""
    x1, y1, x2, y2 = rect
    return [[canvas[y][x] for x in range(x1, x2)] for y in range(y1, y2)]

def mark(canvas, parent, name, rect, before):
    """差分测量：只统计该元素【新画上】的墨迹（排除同区域先画元素干扰）"""
    x1, y1, x2, y2 = rect
    minx, miny, maxx, maxy = 10 ** 9, 10 ** 9, -1, -1
    for y in range(y1, y2):
        for x in range(x1, x2):
            if canvas[y][x] == 0 and before[y - y1][x - x1] != 0:
                minx, miny = min(minx, x), min(miny, y)
                maxx, maxy = max(maxx, x), max(maxy, y)
    ELEMS.append([parent, name, rect, None if maxx < 0 else (minx, miny, maxx, maxy)])

def check(name):
    ok = True
    print(f'—— 元素登记表（{name}）——')
    for parent, ename, rect, bb in ELEMS:
        if bb is None:
            print(f'  [空白] {ename:10s} 预定 {rect}')
            continue
        inside = rect[0] <= bb[0] and rect[1] <= bb[1] and bb[2] < rect[2] and bb[3] < rect[3]
        if not inside:
            ok = False
        tag = 'ok  ' if inside else '越界!'
        print(f'  [{tag}] {ename:10s} 墨迹 x{bb[0]}..{bb[2]} y{bb[1]}..{bb[3]}  预定 {rect}')
    # 兄弟元素两两相交
    for i in range(len(ELEMS)):
        for j in range(i + 1, len(ELEMS)):
            p1, n1, _, b1 = ELEMS[i]
            p2, n2, _, b2 = ELEMS[j]
            if p1 != p2 or b1 is None or b2 is None:
                continue
            if not (b1[2] < b2[0] or b2[2] < b1[0] or b1[3] < b2[1] or b2[3] < b1[1]):
                ok = False
                print(f'  [重叠] {n1} {b1} × {n2} {b2}')
    print('  结论：' + ('无越界、无重叠 ✓' if ok else '存在问题 ✗'))
    return ok

# ---------------- ASCII 降采样视图 ----------------
def ascii_view(canvas, x1, y1, x2, y2, step, title):
    print(f'--- ASCII {title} ({x1},{y1})-({x2},{y2}) 缩放1/{step} ---')
    for y in range(y1, y2, step):
        row = ''
        for x in range(x1, x2, step):
            black = any(canvas[yy][xx] == 0
                        for yy in range(y, min(y + step, y2))
                        for xx in range(x, min(x + step, x2)))
            row += '#' if black else '.'
        print(row)

# ---------------- 字体与图标 ----------------
F_CN = parse_font(os.path.join(UI, 'font_chinese_16.c'))
F_48 = parse_font(os.path.join(UI, 'font_num_48.c'))
F_64 = parse_font(os.path.join(UI, 'font_num_64.c'))
F_28 = parse_font(os.path.join(UI, 'font_num_28.c'))
IC_FR = icon_frames()

def new_canvas():
    return [[1] * SCR_W for _ in range(SCR_H)]

# ---------------- 场景渲染（坐标与 ui_desktop.cpp 一一对应） ----------------
# 状态栏为全局层（所有页面共享）；页面内容画在页面容器区域（绝对 y = 页内 y + 24）
def draw_statusbar(c, M, case):
    wd = case['wday']
    b = grab(c, (2, 2, 164, 24))
    draw_label(c, F_CN, case['date'] + ' 星期' + wd, 8, 4)
    M('screen', 'date', (2, 2, 164, 24), b)
    b = grab(c, (164, 2, 326, 24))
    draw_label(c, F_CN, 'WiFi ' + case['ip'], 164, 4)
    M('screen', 'wifi', (164, 2, 326, 24), b)
    b = grab(c, (326, 2, 398, 24))
    draw_label(c, F_CN, '电量 ' + case['batt'], 326, 4)
    M('screen', 'batt', (326, 2, 398, 24), b)

def render(case, icon_name, frame=0):
    c = new_canvas()
    ELEMS.clear()
    wd = case['wday']

    def M(parent, name, rect, before):
        mark(c, parent, name, rect, before)

    draw_statusbar(c, M, case)

    # 页面1 内容（容器顶 y=24：以下绝对坐标 = 固件页内坐标 + 24）
    # 时钟区（星期行已去掉；64px 大钟）
    b = grab(c, (100, 26, 300, 80))
    draw_label(c, F_64, case['hm'], 0, 30, width=400, align='center')
    M('pageA', 'time', (100, 26, 300, 80), b)
    b = grab(c, (288, 54, 340, 78))
    draw_label(c, F_28, case['ss'], 292, 58)
    M('pageA', 'sec', (288, 54, 340, 78), b)

    # 室内面板（固件 pad_all=0，子控件相对内容区=面板角+border(2)）
    draw_panel(c, 6, 102, 192, 100)
    ELEMS.append(['pageA', 'panel_in', (6, 102, 198, 202), (6, 102, 197, 201)])
    b = grab(c, (10, 106, 196, 130))
    draw_label(c, F_CN, '室内', 18, 110)
    M('panel_in', 'in_title', (10, 106, 196, 130), b)
    b = grab(c, (12, 134, 98, 182))
    draw_label(c, F_28, case['in_t'], 20, 146)
    M('panel_in', 'in_temp', (12, 134, 98, 182), b)
    b = grab(c, (98, 134, 196, 182))
    draw_label(c, F_28, case['in_h'], 102, 146)
    M('panel_in', 'in_humi', (98, 134, 196, 182), b)

    # 天气面板
    draw_panel(c, 202, 102, 192, 100)
    ELEMS.append(['pageA', 'panel_wx', (202, 102, 394, 202), (202, 102, 393, 201)])
    b = grab(c, (206, 106, 308, 130))
    draw_label(c, F_CN, case['city'], 214, 110)
    M('panel_wx', 'wx_title', (206, 106, 308, 130), b)
    b = grab(c, (206, 106, 393, 130))
    draw_label(c, F_CN, case['upd'], 214, 110, width=172, align='right')
    M('panel_wx', 'wx_time', (206, 106, 393, 130), b)
    blit_icon(c, IC_FR[icon_name][frame], 214, 134)          # 内容区 (10,30) + border 2
    mark(c, 'panel_wx', 'img_wx', (212, 132, 256, 176), [[1] * 44 for _ in range(44)])
    b = grab(c, (258, 136, 393, 182))
    draw_label(c, F_28, case['wx_t'], 262, 144)
    M('panel_wx', 'wx_temp', (258, 136, 393, 182), b)
    b = grab(c, (206, 174, 393, 201))
    draw_label(c, F_CN, case['wx_cond'], 214, 180, width=172)
    M('panel_wx', 'wx_cond', (206, 174, 393, 201), b)
    # 用量区（行距 18px：黑体 16px 实测墨迹高 18px，17px 会上下相触）
    b = grab(c, (6, 202, 240, 220))
    draw_label(c, F_CN, '大模型用量 · 剩余', 8, 204)
    M('pageA', 'q_title', (6, 202, 240, 220), b)
    for i, q in enumerate(case['q']):
        y = 222 + i * 18
        b = grab(c, (6, y - 2, 52, y + 17))
        draw_label(c, F_CN, case['qname'][i], 8, y)
        M('pageA', f'q_name{i}', (6, y - 2, 52, y + 17), b)
        draw_bar(c, 52, y + 2, 278, 12, q)
        ELEMS.append(['pageA', f'q_bar{i}', (50, y + 1, 332, y + 15), (52, y + 2, 329, y + 13)])
        b = grab(c, (334, y - 2, 384, y + 17))
        draw_label(c, F_CN, str(q) + '%', 338, y)
        M('pageA', f'q_pct{i}', (334, y - 2, 384, y + 17), b)
    return c

# 页面2：当月日历（无标题——状态栏已有日期；列宽 57=400/7，行高 40，日期用 28px 粗体数字）
def render_calendar(case):
    c = new_canvas()
    ELEMS.clear()

    def M(parent, name, rect, before):
        mark(c, parent, name, rect, before)

    draw_statusbar(c, M, case)

    # 星期头（页内 y=6 → 绝对 30）
    for i, h in enumerate(['日', '一', '二', '三', '四', '五', '六']):
        b = grab(c, (57 * i, 28, 57 * i + 57, 50))
        draw_label(c, F_CN, h, 57 * i, 30, width=57, align='center')
        M('pageB', f'cal_h{i}', (57 * i, 28, 57 * i + 57, 50), b)

    # 日期网格（页内 label y=34+r*40+10 → 绝对 68+40r，16px 黑体；当天 = 独立 55×34 黑块 + 白字）
    # 固件高亮块：pos (57c+1, 34+40r+3) 尺寸 55×34 → 绝对 (57c+1, 61+40r)
    for r in range(6):
        for cc in range(7):
            cell = r * 7 + cc
            day = cell - case['cal_first_wday'] + 1
            x, y = 57 * cc, 68 + r * 40
            if day < 1 or day > case['cal_days']:
                continue
            if day == case['cal_today']:
                b = grab(c, (x, 59 + r * 40, x + 57, 97 + r * 40))
                for yy in range(61 + r * 40, 95 + r * 40):
                    for xx in range(x + 1, x + 56):
                        c[yy][xx] = 0
                draw_label(c, F_CN, str(day), x, y, width=57, align='center', val=1)
                M('pageB', f'cal_d{day}', (x, 59 + r * 40, x + 57, 97 + r * 40), b)
            else:
                b = grab(c, (x + 8, y - 2, x + 49, y + 18))
                draw_label(c, F_CN, str(day), x, y, width=57, align='center')
                M('pageB', f'cal_d{day}', (x + 8, y - 2, x + 49, y + 18), b)
    return c

CASES = [
    dict(name='常规', date='06/30', wday='三', hm='14:32', ss='05', sync='NTP 已同步',
         ip='192.168.100.82', batt='87%', in_t='24.5C', in_h='56%',
         city='南京', upd='更新 14:30', wx_t='31C', wx_cond='小雨·湿78%',
         qname=('5小时', '7天', '30天'), q=(23, 41, 12)),
    dict(name='极值', date='12/25', wday='日', hm='23:59', ss='59', sync='RTC 时间',
         ip='192.168.100.123', batt='100%', in_t='-40.0C', in_h='100%',
         city='南京', upd='更新 23:45', wx_t='-3C', wx_cond='雷阵雨伴冰雹·湿100%',
         qname=('5小时', '7天', '30天'), q=(100, 100, 100)),
    dict(name='无数据', date='01/01', wday='四', hm='00:00', ss='00', sync='RTC 时间',
         ip='...', batt='--', in_t='--', in_h='--',
         city='南京', upd='更新 --', wx_t='--C', wx_cond='无数据',
         qname=('5小时', '7天', '30天'), q=(0, 0, 0)),
]

COMBOS = [(0, 'rain_l', 0), (1, 'thunder', 1), (2, 'overcast', 0),
          (3, 'overcast', 0), (4, 'overcast', 0), (5, 'overcast', 0)]

# 页面2：日历场景（cal_first_wday=1号的星期 0=周日；cal_today=None 表示时钟未就绪的空日历）
CAL_CASES = [
    dict(name='日历常规', date='09/05', wday='六', ip='192.168.100.82', batt='87%',
         cal_first_wday=2, cal_days=30, cal_today=5),
    dict(name='日历极值', date='08/31', wday='一', ip='192.168.100.123', batt='100%',
         cal_first_wday=6, cal_days=31, cal_today=31),   # 31天+周六开头=6行满格
    dict(name='日历无数据', date='01/01', wday='四', ip='...', batt='--',
         cal_first_wday=0, cal_days=0, cal_today=None),
]

if __name__ == '__main__':
    os.makedirs(OUT, exist_ok=True)
    print(f"字库 metrics：CN lh={F_CN['lh']} bl={F_CN['bl']} | 48 lh={F_48['lh']} bl={F_48['bl']} | 28 lh={F_28['lh']} bl={F_28['bl']}")
    all_cases = [(c, 'page0') for c in CASES] + [(c, 'cal') for c in CAL_CASES]
    all_ok = True
    for i, (ci, icon, fr) in enumerate(COMBOS):
        kind = 'page0' if ci < len(CASES) else 'cal'
        case = CASES[ci] if kind == 'page0' else CAL_CASES[ci - len(CASES)]
        if kind == 'page0':
            c = render(case, icon, fr)
        else:
            c = render_calendar(case)
        save_png(os.path.join(OUT, f'screen_{i}_{case["name"]}.png'), c, SCR_W, SCR_H)
        print(f'\n======== 场景{i} {case["name"]}（icon={icon} 帧{fr}）========')
        ascii_view(c, 0, 0, 400, 300, 4, f'全屏 {case["name"]}')
        if not check(case['name']):
            all_ok = False
    # 天气面板 2×2 特写（极值场景，验证图标形状与文本对齐）
    c1 = render(CASES[1], 'thunder', 1)
    print()
    ascii_view(c1, 202, 102, 394, 202, 2, '天气面板特写（极值/雷阵雨伴冰雹）')
    print('\n总体：' + ('全部通过 ✓' if all_ok else '有问题需修 ✗'))
    print('PNG 输出 →', OUT)
