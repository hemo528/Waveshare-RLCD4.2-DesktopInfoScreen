# gen_font_chinese16.ps1 —— 重生成界面中文字库
# ⚠️ 入口必须是包根的 lv_font_conv.js（lib/cli.js 没有 main 入口，直跑=假成功什么都不做）
# 本脚本用 Get-Content -Raw | Invoke-Expression 方式运行（.ps1 直跑被执行策略挡）
$symbols = '·°星期一二三四五六日室内温湿度外天气晴多云阴雾小中大雨暴雷阵雪夹沙尘伴冰雹强轻冻烟霾浮更新无数据大模型用量剩余小时天周期已同步电量南京年月'

& 'C:\Program Files\nodejs\node.exe' 'C:\Espressif\tools\lvfont\node_modules\lv_font_conv\lv_font_conv.js' `
  --no-compress --bpp 1 --size 16 --font 'C:\Windows\Fonts\simhei.ttf' `
  -r 0x20-0x7E `
  --symbols $symbols `
  --format lvgl --lv-font-name chinese_16 `
  -o 'C:\Espressif\projects\DesktopInfoScreen\main\ui\font_chinese_16.c'

Write-Host "exit=$LASTEXITCODE"
