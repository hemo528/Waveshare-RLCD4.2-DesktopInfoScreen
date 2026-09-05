// ui_desktop.cpp —— 桌面信息屏界面（400×300 横屏，LVGL v8，双页面）
//
// 页面结构（v1.2 起）：
//   状态栏（日期/WiFi/电量）为全局层，所有页面共享
//   页面1（默认）：时钟 + 室内温湿度 + 天气 + 大模型用量（坐标与 v1.0 相同，整体上移 24px）
//   页面2：当月日历（标题 + 星期头 + 6×7 网格 + 当天黑底白字高亮，跨天/跨月自动重绘）
//   板上左右两颗键（GPIO0 / GPIO18，可在 app_config.h 对调）切页，长按无动作
//
// 布局（页面内坐标按像素写死；全部经过 tools/sim/sim.py 像素级仿真验证，无越界/无重叠）：
//   ┌────────────────────────────────────────────────┐ y=0
//   │ 06/30 星期三           WiFi 192.168.100.82 电量 87% │ h=24 状态栏（全局）
//   ├────────────────────────────────────────────────┤ y=24
//   │ 页面1：                    14:32   05           │      时钟区 h=102
//   │         星期三 · NTP 已同步                      │
//   │ 室内                 │ 南京          更新 14:30 │      中部 h=100
//   │ 24.5C   56%          │ [动态图标] 31C           │
//   │                      │ 小雨·湿78%               │
//   │ 大模型用量 · 剩余                                │      用量区 h=72
//   │ 5小时 ▓▓▓▓░░░░░░  94%                           │
//   │ 7天   ▓▓▓▓▓▓░░░░  66%                           │
//   │ 30天  ▓▓▓▓▓▓▓░░░  83%                           │
//   │ 页面2：        日 一 二 三 四 五 六               │      日历（无标题，状态栏已有日期）
//   │        1  2  3  4  5  6  7                      │      6行×40px，28px 粗体数字
//   │        ...          [30]                        │      [30]=当天黑底
//   └────────────────────────────────────────────────┘ y=300
//
// 切页方式：瞬切（面板 ~17Hz 自刷新跑不动滑动动画，实测瞬切手感最好；
// TE 帧同步保留在 display_bsp，消除所有常规刷新的撕裂）
//
// 字体说明（全部 bpp=1，与 1-bit 屏二值化严格匹配）：
//   - chinese_16  黑体 16px 子集（界面全部中文 + ASCII；含日历用的 年/月）
//   - num_48/28   Arial Bold 数字（时钟/温度值，天生加粗，无抗锯齿灰边）
// 线程说明：
//   - ui_tick(1s) 与 key_poll(100ms)、icon_anim(500ms) 都是 lv_timer 回调，LVGL 任务内串行
//   - 按键扫描任务只写原子变量，key_poll 里取走事件后切页——跨任务无锁安全
#include "ui_desktop.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "../app_config.h"
#include "../data/data_store.h"
#include "../key/key_bsp.h"
#include "wifi_sta_bsp.h"
#include "wx_icons.h"
#include "esp_log.h"

static const char *UI_TAG = "ui";

// 字库是纯 C 符号，声明必须放在命名空间外（否则 C++ 改名导致链接失败）
LV_FONT_DECLARE(chinese_16);
LV_FONT_DECLARE(num_48);
LV_FONT_DECLARE(num_64);
LV_FONT_DECLARE(num_28);

namespace {

#define F_CN  (&chinese_16)
#define F_N48 (&num_48)
#define F_N64 (&num_64)
#define F_N28 (&num_28)

#define PAGE_N  2          // 页面总数
#define CAL_ROWS 6         // 日历最多 6 行（31 天 + 首行偏移最多 37 格）
#define CAL_COLS 7

// strftime(%a) 只给英文星期，这里自己映射（"日一二三四五六"都在字库里）
const char *const WEEKDAY_CN[7] = { "日", "一", "二", "三", "四", "五", "六" };

struct ui_objects_t {
    // 状态栏（全局层）
    lv_obj_t *lbl_date;
    lv_obj_t *lbl_wifi;
    lv_obj_t *lbl_batt;
    // 页面容器
    lv_obj_t *page[PAGE_N];
    int       cur_page = 0;
    // 页面1：时钟区
    lv_obj_t *lbl_time;   // HH:MM（num_64 粗体；配网模式下改显示 192.168.4.1 并切 num_48）
    lv_obj_t *lbl_sec;    // SS（num_28，基线对齐主时钟；配网模式隐藏）
    lv_obj_t *lbl_ap_hint;// "连接热点 … 打开管理页"（仅配网模式显示）
    bool      ap_shown = false;   // 当前是否处于配网显示（切换时钟字体用，避免每秒重设）
    // 页面1：中部面板
    lv_obj_t *lbl_in_title;
    lv_obj_t *lbl_in_temp;
    lv_obj_t *lbl_in_humi;
    lv_obj_t *lbl_wx_title;
    lv_obj_t *lbl_wx_time;
    lv_obj_t *img_wx;     // 动态天气图标（40×40）
    lv_obj_t *lbl_wx_temp;
    lv_obj_t *lbl_wx_cond;
    // 页面1：用量区
    lv_obj_t *lbl_q_title;
    lv_obj_t *lbl_q_name[3];
    lv_obj_t *bar_q[3];
    lv_obj_t *lbl_q_pct[3];
    // 页面2：日历
    lv_obj_t *cal_hl;                       // 当天高亮块（独立黑底矩形，比文字墨迹大很多）
    lv_obj_t *lbl_cal_head[CAL_COLS];
    lv_obj_t *lbl_cal_day[CAL_ROWS * CAL_COLS];
    int       cal_key = -1;   // 已渲染的 (y,m,d) 组合，用于跨天重绘
} u;

int  s_wx_icon = WX_ICON_OVERCAST;  // ui_tick 从快照更新；图标动画定时器只读
int  s_frame   = 0;                 // 当前动画帧

// ---------- 通用小工具 ----------
lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, lv_coord_t x, lv_coord_t y, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_color(l, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_font(l, font, 0);
    lv_label_set_text(l, text);
    lv_obj_set_pos(l, x, y);
    return l;
}

// 页面容器：白底、无边框、pad/radius/shadow 全归零（1-bit 屏 + 默认主题防坑，坐标确定性）
lv_obj_t *make_page(lv_obj_t *parent, lv_coord_t y)
{
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_set_size(p, 400, 276);
    lv_obj_set_pos(p, 0, y);
    lv_obj_set_style_bg_color(p, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(p, 0, 0);
    lv_obj_set_style_pad_all(p, 0, 0);
    lv_obj_set_style_radius(p, 0, 0);
    lv_obj_set_style_shadow_width(p, 0, 0);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    return p;
}

lv_obj_t *make_panel(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_set_size(p, w, h);
    lv_obj_set_pos(p, x, y);
    lv_obj_set_style_bg_color(p, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(p, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(p, 2, 0);
    lv_obj_set_style_radius(p, 0, 0);   // 1-bit 屏上圆角会生成灰阶锯齿，直角更干净
    // 默认主题 card 样式会给 lv_obj 塞 pad_all=PAD_DEF(20px@DISP_MEDIUM)，子控件被推 +22px
    // （pad 20 + border 2）导致越界——显式归零，坐标回归确定性
    lv_obj_set_style_pad_all(p, 0, 0);
    lv_obj_set_style_shadow_width(p, 0, 0);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    return p;
}

lv_obj_t *make_bar(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *b = lv_bar_create(parent);
    lv_obj_set_size(b, w, h);
    lv_obj_set_pos(b, x, y);
    lv_bar_set_range(b, 0, 100);
    // 反射屏 1-bit 显示：主槽白色 + 黑边框，指示条黑色
    lv_obj_set_style_bg_color(b, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_border_color(b, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_border_width(b, 1, LV_PART_MAIN);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x000000), LV_PART_INDICATOR);
    // 主题默认给 bar 主槽和指示条加 LV_RADIUS_CIRCLE（胶囊圆角），1-bit 下有锯齿且与仿真不符
    lv_obj_set_style_radius(b, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(b, 0, LV_PART_INDICATOR);
    lv_obj_set_style_pad_all(b, 0, 0);
    return b;
}

// ---------- 页面切换（瞬切） ----------
// 曾实现水平滑动动画：TE 帧同步消除了撕裂，但面板自刷新仅 ~17Hz（TE 实测 58ms），
// 任何滑动的帧间跳跃感都压不掉，实测不如干脆的瞬切——遂移除动画。
// TE 同步保留在显示驱动里（时钟/天气等所有常规刷新同样消除撕裂）。
void page_switch(int idx)
{
    if (idx < 0 || idx >= PAGE_N || idx == u.cur_page) return;
    for (int i = 0; i < PAGE_N; i++) {
        if (i == idx) lv_obj_clear_flag(u.page[i], LV_OBJ_FLAG_HIDDEN);
        else          lv_obj_add_flag(u.page[i], LV_OBJ_FLAG_HIDDEN);
    }
    u.cur_page = idx;
    ESP_LOGI(UI_TAG, "page -> %d", idx + 1);   // 串口可见的切页证据
}

// ---------- 页面2：日历 ----------
// 每月天数（tm_mon 0-11）；闰年规则：4 年一闰、百年不闰、400 年再闰
int days_in_month(int year, int mon)
{
    static const int d[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (mon == 1) {
        bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        return leap ? 29 : 28;
    }
    return d[mon];
}

// (year,mon,day) 变化时重建日历文本与高亮；返回 true = 本次有实际更新
// 布局：状态栏已含日期，页内不再放标题；格子放大（行高 40px，日期用 num_28 粗体数字）
bool cal_refresh(const struct tm &t)
{
    if (t.tm_year < 120) return false;            // 2020 年以前 = 时钟未就绪
    int key = (t.tm_year + 1900) * 10000 + (t.tm_mon + 1) * 100 + t.tm_mday;
    if (key == u.cal_key) return false;
    u.cal_key = key;

    char buf[32];

    // 本月 1 号是星期几：由"今天是星期几"回推
    int first_wday = ((t.tm_wday - (t.tm_mday - 1) % 7) % 7 + 7) % 7;
    int ndays = days_in_month(t.tm_year + 1900, t.tm_mon);

    for (int cell = 0; cell < CAL_ROWS * CAL_COLS; cell++) {
        int c = cell % CAL_COLS, r = cell / CAL_COLS;
        int day = cell - first_wday + 1;          // 该格对应的日期（<1 或 >ndays = 空格）
        lv_obj_t *l = u.lbl_cal_day[cell];
        if (day < 1 || day > ndays) {
            lv_label_set_text(l, "");
            continue;
        }
        snprintf(buf, sizeof(buf), "%d", day);
        lv_label_set_text(l, buf);
        if (day == t.tm_mday) {
            // 当天：55×34 黑底块（行高 40 里的居中大块）+ 白字 —— 反射屏上对比最强
            lv_obj_set_pos(u.cal_hl, 57 * c + 1, 34 + r * 40 + 3);
            lv_obj_clear_flag(u.cal_hl, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), 0);
        } else {
            lv_obj_set_style_text_color(l, lv_color_hex(0x000000), 0);
        }
    }
    return true;
}

// ---------- 100ms 节拍：按键轮询（切页必须快于 1s 才跟手） ----------
void key_poll(lv_timer_t *timer)
{
    (void)timer;
    switch (key_consume_event()) {
    case KEY_EVT_NEXT:
        page_switch((u.cur_page + 1) % PAGE_N);
        break;
    case KEY_EVT_PREV:
        page_switch((u.cur_page + PAGE_N - 1) % PAGE_N);
        break;
    default:
        break;
    }
}

// ---------- 1s 节拍：读 data_store，刷新控件 ----------
void ui_tick(lv_timer_t *timer)
{
    (void)timer;
    char buf[96], buf2[192];   // buf2 加大，避免 -Wformat-truncation 对 "%s · %s" 的保守告警

    // 取数据（快照）
    data_lock();
    struct tm tmv = g_data.now_tm;
    bool   iok    = g_data.indoor_ok;
    float  it     = g_data.indoor_temp_c;
    float  ih     = g_data.indoor_rh;
    int    batt   = g_data.batt_pct;
    bool   wxok   = g_data.wx_ok;
    float  wt     = g_data.wx_temp_c;
    int    wrh    = g_data.wx_rh;
    char   wtxt[sizeof(g_data.wx_text)];
    strcpy(wtxt, g_data.wx_text);
    time_t wx_ts  = g_data.wx_ts;
    int    q[3];
    q[0] = g_data.quota_pct[0]; q[1] = g_data.quota_pct[1]; q[2] = g_data.quota_pct[2];
    data_unlock();

    // 图标枚举：无数据时给"阴"兜底
    s_wx_icon = wxok ? g_data.wx_icon : WX_ICON_OVERCAST;

    const char *wd = WEEKDAY_CN[tmv.tm_wday % 7];

    // 状态栏（全局）：日期 + 星期
    strftime(buf, sizeof(buf), "%m/%d", &tmv);
    snprintf(buf2, sizeof(buf2), "%s 星期%s", buf, wd);
    lv_label_set_text(u.lbl_date, buf2);
    if (wifi_ap_active()) {
        snprintf(buf, sizeof(buf), "配网 192.168.4.1");
    } else {
        snprintf(buf, sizeof(buf), "WiFi %s", wifi_sta_ip_str());
    }
    lv_label_set_text(u.lbl_wifi, buf);
    if (batt >= 0) {
        snprintf(buf, sizeof(buf), "电量 %d%%", batt);
    } else {
        snprintf(buf, sizeof(buf), "电量 --");
    }
    lv_label_set_text(u.lbl_batt, buf);

    // 页面1：时钟区（num_64 粗体数字；配网模式下钟位显示 192.168.4.1，秒位隐藏）
    if (wifi_ap_active()) {
        if (!u.ap_shown) {   // 进入配网显示：切字体 + 显隐标签（只在模式变化时做一次）
            u.ap_shown = true;
            lv_obj_set_style_text_font(u.lbl_time, F_N48, 0);
            lv_obj_add_flag(u.lbl_sec, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(u.lbl_ap_hint, LV_OBJ_FLAG_HIDDEN);
        }
        lv_label_set_text(u.lbl_time, "192.168.4.1");
    } else {
        if (u.ap_shown) {    // 回到正常显示
            u.ap_shown = false;
            lv_obj_set_style_text_font(u.lbl_time, F_N64, 0);
            lv_obj_clear_flag(u.lbl_sec, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(u.lbl_ap_hint, LV_OBJ_FLAG_HIDDEN);
        }
        strftime(buf, sizeof(buf), "%H:%M", &tmv);
        lv_label_set_text(u.lbl_time, buf);
        strftime(buf, sizeof(buf), "%S", &tmv);
        lv_label_set_text(u.lbl_sec, buf);
    }

    // 页面1：室内面板（数值用 num_28 粗体；湿度不带 RH 后缀——"100%RH"在 28px 下宽约 110px
    // 会穿出面板内缘，仿真器实测；单位由 C/% 区分即可）
    if (iok) {
        snprintf(buf, sizeof(buf), "%.1fC", it);
        snprintf(buf2, sizeof(buf2), "%d%%", (int)ih);
    } else {
        snprintf(buf, sizeof(buf), "--");
        snprintf(buf2, sizeof(buf2), "--");
    }
    lv_label_set_text(u.lbl_in_temp, buf);
    lv_label_set_text(u.lbl_in_humi, buf2);

    // 页面1：天气面板（温度数字紧贴图标右侧；现象词独占底部整行。
    // 格式不带空格：极值"雷阵雨伴冰雹·湿100%"实测墨迹到 x≈367，带空格会顶到面板边框）
    if (wxok) {
        snprintf(buf, sizeof(buf), "%dC", (int)wt);
        snprintf(buf2, sizeof(buf2), "%s·湿%d%%", wtxt, wrh);
    } else {
        snprintf(buf, sizeof(buf), "--C");
        snprintf(buf2, sizeof(buf2), "无数据");
    }
    lv_label_set_text(u.lbl_wx_temp, buf);
    lv_label_set_text(u.lbl_wx_cond, buf2);
    if (wx_ts > 0) {
        struct tm wt_ = *localtime(&wx_ts);
        strftime(buf, sizeof(buf), "%H:%M", &wt_);
        snprintf(buf2, sizeof(buf2), "更新 %s", buf);
    } else {
        snprintf(buf2, sizeof(buf2), "更新 --");
    }
    lv_label_set_text(u.lbl_wx_time, buf2);

    // 页面1：用量区
    for (int i = 0; i < 3; i++) {
        lv_bar_set_value(u.bar_q[i], q[i], LV_ANIM_OFF);
        snprintf(buf, sizeof(buf), "%d%%", q[i]);
        lv_label_set_text(u.lbl_q_pct[i], buf);
    }

    // 页面2：日历（只在日期变化时重绘文本，1s 轮询无压力）
    cal_refresh(tmv);
}

// ---------- 图标动画：500ms 交替 A/B 帧 ----------
void icon_anim(lv_timer_t *timer)
{
    (void)timer;
    s_frame ^= 1;
    lv_img_set_src(u.img_wx, wx_icon_frame(s_wx_icon, s_frame));
}

} // namespace

void ui_desktop_create(void)
{
    lv_obj_t *scr = lv_scr_act();
    // 屏幕同样归零内边距（默认主题 scr 样式虽只有 row/col gap，显式归零保证确定性）
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    // ===== 状态栏（全局，y 0..24） =====
    u.lbl_date = make_label(scr, F_CN, 8, 4, "");
    u.lbl_wifi = make_label(scr, F_CN, 164, 4, "");
    u.lbl_batt = make_label(scr, F_CN, 326, 4, "");

    // ===== 页面容器（y 24..300） =====
    u.page[0] = make_page(scr, 24);
    u.page[1] = make_page(scr, 24);
    lv_obj_add_flag(u.page[1], LV_OBJ_FLAG_HIDDEN);

    // ===== 页面1：时钟区（页面内 y 0..102，绝对 24..126） =====
    // 星期行已去掉（状态栏有日期+星期）；num_64 墨迹约 47px 高，y=30 上下留白均衡
    u.lbl_time = make_label(u.page[0], F_N64, 0, 30, "--:--");
    lv_obj_set_width(u.lbl_time, 400);
    lv_obj_set_style_text_align(u.lbl_time, LV_TEXT_ALIGN_CENTER, 0);
    u.lbl_sec  = make_label(u.page[0], F_N28, 292, 58, "--");
    u.lbl_ap_hint = make_label(u.page[0], F_CN, 0, 72, "");
    lv_obj_set_width(u.lbl_ap_hint, 400);
    lv_obj_set_style_text_align(u.lbl_ap_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(u.lbl_ap_hint, "连接热点 " APP_SETUP_AP_SSID " 打开管理页");
    lv_obj_add_flag(u.lbl_ap_hint, LV_OBJ_FLAG_HIDDEN);

    // ===== 页面1：中部两面板（页面内 y 102..202） =====
    lv_obj_t *p_in = make_panel(u.page[0], 6, 102, 192, 100);
    u.lbl_in_title = make_label(p_in, F_CN, 10, 6, "室内");
    u.lbl_in_temp  = make_label(p_in, F_N28, 12, 42, "--");
    u.lbl_in_humi  = make_label(p_in, F_N28, 94, 42, "--");   // "100%"最宽约 70px，不贴边框

    lv_obj_t *p_wx = make_panel(u.page[0], 202, 102, 192, 100);
    u.lbl_wx_title = make_label(p_wx, F_CN, 10, 6, APP_WEATHER_CITY);
    u.lbl_wx_time  = make_label(p_wx, F_CN, 10, 6, "更新 --");
    lv_obj_set_width(u.lbl_wx_time, 172);
    lv_obj_set_style_text_align(u.lbl_wx_time, LV_TEXT_ALIGN_RIGHT, 0);
    u.img_wx = lv_img_create(p_wx);
    lv_img_set_src(u.img_wx, wx_icon_frame(WX_ICON_OVERCAST, 0));
    lv_obj_set_pos(u.img_wx, 10, 30);
    u.lbl_wx_temp  = make_label(p_wx, F_N28, 58, 40, "--C");
    u.lbl_wx_cond  = make_label(p_wx, F_CN, 10, 76, "无数据");
    lv_obj_set_width(u.lbl_wx_cond, 172);

    // ===== 页面1：大模型用量区（页面内 y 204..276） =====
    // 行距 18px：黑体 16px 实测墨迹高 18px（上下各溢出 2px），17px 行距会上下相触（仿真器实测）
    u.lbl_q_title = make_label(u.page[0], F_CN, 8, 204, "大模型用量 · 剩余");

    static const char *names[3] = { "5小时", "7天", "30天" };
    for (int i = 0; i < 3; i++) {
        int y = 222 + i * 18;
        u.lbl_q_name[i] = make_label(u.page[0], F_CN, 8, y, names[i]);
        u.bar_q[i]      = make_bar(u.page[0], 52, y + 2, 278, 12);
        u.lbl_q_pct[i]  = make_label(u.page[0], F_CN, 338, y, "0%");
    }

    // ===== 页面2：日历（页内坐标；列宽 57=400/7，行高 40=6行铺满 276） =====
    // 星期头 y=6；网格 6 行起点 y=34（34+240=274，页内 276 正好放下）
    // 日期数字用 num_28 粗体（比 16px 黑体醒目一倍），label 定宽 57 居中
    for (int i = 0; i < CAL_COLS; i++) {
        u.lbl_cal_head[i] = make_label(u.page[1], F_CN, 57 * i, 6, WEEKDAY_CN[i]);
        lv_obj_set_width(u.lbl_cal_head[i], 57);
        lv_obj_set_style_text_align(u.lbl_cal_head[i], LV_TEXT_ALIGN_CENTER, 0);
    }
    // 当天高亮块：独立矩形（创建在日期 label 之前 → label 绘制在其上层），
    // 跨月/跨天时整体搬移位置，比改 label 自身背景灵活（背景高度与文字行高解耦）
    u.cal_hl = lv_obj_create(u.page[1]);
    lv_obj_set_size(u.cal_hl, 55, 34);
    lv_obj_set_style_bg_color(u.cal_hl, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(u.cal_hl, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(u.cal_hl, 0, 0);
    lv_obj_set_style_radius(u.cal_hl, 0, 0);
    lv_obj_set_style_pad_all(u.cal_hl, 0, 0);
    lv_obj_add_flag(u.cal_hl, LV_OBJ_FLAG_HIDDEN);
    // 网格：日期用 16px 黑体（与最初版本一致的粗细），定宽 57 居中，行内垂直居中
    for (int r = 0; r < CAL_ROWS; r++) {
        for (int c = 0; c < CAL_COLS; c++) {
            int cell = r * CAL_COLS + c;
            u.lbl_cal_day[cell] = make_label(u.page[1], F_CN, 57 * c, 34 + r * 40 + 10, "");
            lv_obj_set_width(u.lbl_cal_day[cell], 57);
            lv_obj_set_style_text_align(u.lbl_cal_day[cell], LV_TEXT_ALIGN_CENTER, 0);
        }
    }

    // ===== 定时器 =====
    lv_timer_create(ui_tick, APP_UI_TICK_MS, NULL);      // 1s 数据节拍
    lv_timer_create(icon_anim, 500, NULL);               // 图标动画
    lv_timer_create(key_poll, 100, NULL);                // 按键轮询（切页跟手）
    ui_tick(NULL);   // 先立即刷一帧，避免上电显示全占位符
}
