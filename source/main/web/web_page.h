// web_page.h —— 网页管理后台页面（内嵌 HTML，由 web_server.cpp 在 GET / 时返回）
// 纯静态 + 少量 JS：加载时拉 /api/config 回填表单，保存时 POST JSON 到 /api/config。
// 通过手机/电脑浏览器访问（配网模式 http://192.168.4.1 ，STA 模式 http://屏幕上显示的IP）
#pragma once

static const char WEB_PAGE_HTML[] = R"rawliteral(<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>桌面信息屏 · 管理后台</title>
<style>
  *{box-sizing:border-box;margin:0;padding:0}
  body{font-family:system-ui,-apple-system,"Microsoft YaHei",sans-serif;background:#f0f2f5;color:#222;padding:16px;max-width:560px;margin:0 auto}
  h1{font-size:20px;margin-bottom:4px}
  #st{font-size:13px;color:#666;margin-bottom:14px}
  .card{background:#fff;border-radius:10px;padding:14px 16px;margin-bottom:12px;box-shadow:0 1px 3px rgba(0,0,0,.08)}
  .card h2{font-size:15px;margin-bottom:10px;color:#1a56db}
  label{display:block;font-size:13px;color:#555;margin:8px 0 3px}
  input[type=text],input[type=password],select{width:100%;padding:8px 10px;border:1px solid #ccc;border-radius:6px;font-size:14px}
  input:focus,select:focus{outline:none;border-color:#1a56db}
  .hint{font-size:12px;color:#999;margin-top:3px}
  .row{display:flex;gap:10px}.row>div{flex:1}
  .btn{width:100%;padding:12px;border:none;border-radius:8px;font-size:15px;margin-top:14px;cursor:pointer}
  .save{background:#1a56db;color:#fff}.save:disabled{background:#9db8e8}
  .reset{background:#fff;color:#c0392b;border:1px solid #c0392b;margin-top:8px}
  #mask{position:fixed;inset:0;background:rgba(0,0,0,.55);display:none;align-items:center;justify-content:center;flex-direction:column;color:#fff;font-size:16px;text-align:center;line-height:1.8}
  .sec-hide{display:none}
  .chk{display:flex;align-items:center;gap:8px;margin:10px 0}
  .chk input{width:18px;height:18px}
</style>
</head>
<body>
<h1>🖥️ 桌面信息屏 · 管理后台</h1>
<div id="st">正在读取设备状态…</div>

<div class="card">
  <h2>WiFi 连接</h2>
  <label>WiFi 名称（SSID，2.4GHz）</label>
  <input type="text" id="wifi_ssid" maxlength="32">
  <label>WiFi 密码</label>
  <input type="password" id="wifi_pass" maxlength="63">
  <div class="hint">留空 SSID 并保存 = 清除 WiFi，重启后进入配网热点模式</div>
</div>

<div class="card">
  <h2>天气</h2>
  <label>数据源</label>
  <select id="wx_provider">
    <option value="1">Open-Meteo（免 key，推荐）</option>
    <option value="2">OpenWeather（需 key）</option>
    <option value="3">和风天气（需 key）</option>
  </select>
  <div class="row">
    <div><label>纬度</label><input type="text" id="wx_lat"></div>
    <div><label>经度</label><input type="text" id="wx_lon"></div>
  </div>
  <label>城市显示名</label>
  <input type="text" id="wx_city" maxlength="10">
  <div class="hint">⚠️ 屏幕为子集字库：城市名超出支持字符会显示空白（默认支持：南京）</div>
  <div id="sec_owm">
    <label>OpenWeather API Key</label><input type="text" id="wx_owm_key">
    <label>OpenWeather 城市参数（q=）</label><input type="text" id="wx_owm_city">
  </div>
  <div id="sec_qw">
    <label>和风 API Key</label><input type="text" id="wx_qw_key">
    <label>和风 LocationID</label><input type="text" id="wx_qw_loc">
  </div>
</div>

<div class="card">
  <h2>大模型用量（火山方舟 Coding Plan）</h2>
  <div class="chk"><input type="checkbox" id="ark_enable"><label for="ark_enable" style="margin:0">启用直连查询（V4 签名，10 分钟刷新）</label></div>
  <label>Access Key（AK）</label><input type="text" id="ark_ak">
  <label>Secret Key（SK）</label><input type="password" id="ark_sk">
  <div class="hint">密钥只保存在设备本地 NVS；建议使用只读权限的 IAM 子用户密钥</div>
  <div class="row">
    <div><label>兜底 5小时（%）</label><input type="text" id="q0"></div>
    <div><label>兜底 7天（%）</label><input type="text" id="q1"></div>
    <div><label>兜底 30天（%）</label><input type="text" id="q2"></div>
  </div>
  <div class="hint">兜底值仅在启动瞬间或接口长期失败时显示</div>
</div>

<button class="btn save" id="btn_save" onclick="saveCfg()">💾 保存并重启生效</button>
<button class="btn reset" onclick="resetCfg()">恢复编译期默认配置并重启</button>

<div id="mask"><div id="mask_txt">正在保存…</div></div>

<script>
const F = ['wifi_ssid','wifi_pass','wx_lat','wx_lon','wx_city','wx_owm_key','wx_owm_city','wx_qw_key','wx_qw_loc','ark_ak','ark_sk'];

function fmtIp(m){ return m=='ap' ? '配网热点模式 · http://192.168.4.1' : '工作模式 · IP ' + m; }

async function load(){
  try{
    const st = await (await fetch('/api/status')).json();
    document.getElementById('st').textContent = fmtIp(st.mode) + ' · 热点/SSID: ' + (st.ssid||'--');
  }catch(e){ document.getElementById('st').textContent = '状态获取失败'; }
  try{
    const c = await (await fetch('/api/config')).json();
    F.forEach(k => document.getElementById(k).value = c[k] ?? '');
    document.getElementById('wx_provider').value = c.wx_provider ?? 1;
    document.getElementById('ark_enable').checked = !!c.ark_enable;
    (c.quota_fb||[]).forEach((v,i)=>{ const e=document.getElementById('q'+i); if(e) e.value=v; });
    toggleProv();
  }catch(e){ alert('配置读取失败'); }
}

function toggleProv(){
  const p = document.getElementById('wx_provider').value;
  document.getElementById('sec_owm').className = (p==='2')?'':'sec-hide';
  document.getElementById('sec_qw').className  = (p==='3')?'':'sec-hide';
}
document.getElementById('wx_provider').addEventListener('change', toggleProv);

function mask(t){ const m=document.getElementById('mask'); document.getElementById('mask_txt').textContent=t; m.style.display='flex'; }

async function saveCfg(){
  const ssid = document.getElementById('wifi_ssid').value.trim();
  if (ssid === '' && !confirm('WiFi 名称为空：设备重启后将进入配网热点模式，确定？')) return;
  document.getElementById('btn_save').disabled = true;
  const body = {
    wifi_ssid: ssid, wifi_pass: document.getElementById('wifi_pass').value,
    wx_provider: parseInt(document.getElementById('wx_provider').value),
    wx_lat: document.getElementById('wx_lat').value, wx_lon: document.getElementById('wx_lon').value,
    wx_city: document.getElementById('wx_city').value,
    wx_owm_key: document.getElementById('wx_owm_key').value, wx_owm_city: document.getElementById('wx_owm_city').value,
    wx_qw_key: document.getElementById('wx_qw_key').value, wx_qw_loc: document.getElementById('wx_qw_loc').value,
    ark_enable: document.getElementById('ark_enable').checked ? 1 : 0,
    ark_ak: document.getElementById('ark_ak').value, ark_sk: document.getElementById('ark_sk').value,
    quota_fb: [0,1,2].map(i => parseInt(document.getElementById('q'+i).value || '100'))
  };
  try{
    const r = await (await fetch('/api/config', {method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify(body)})).json();
    if (r.ok){ mask('✅ 已保存到设备<br>正在重启，请稍候 10 秒后刷新本页<br><small>STA 模式请改用屏幕上显示的新 IP 访问</small>'); }
    else { alert('保存失败：' + (r.msg||'')); document.getElementById('btn_save').disabled = false; }
  }catch(e){ alert('保存请求失败：' + e); document.getElementById('btn_save').disabled = false; }
}

async function resetCfg(){
  if (!confirm('确定恢复编译期默认配置？网页保存过的所有配置将被清除。')) return;
  mask('正在恢复默认并重启…');
  try{ await fetch('/api/reset', {method:'POST'}); }catch(e){}
}

load();
</script>
</body>
</html>)rawliteral";
