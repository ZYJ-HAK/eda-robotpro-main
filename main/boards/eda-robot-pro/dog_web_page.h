#ifndef _DOG_WEB_PAGE_H_
#define _DOG_WEB_PAGE_H_

/*
 * 内置控制页。整页内联，无外部 CSS/JS/字体依赖 ——
 * 手机连上热点就能用，不需要任何外网资源。
 *
 * 分隔符用 )DOGPAGE" 是为了让正文里的 ) 与 " 不会提前终止原始字符串。
 */

static const char kDogWebPage[] = R"DOGPAGE(<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#101418">
<title>机器狗控制台</title>
<style>
:root{--bg:#101418;--s:#191f24;--s2:#232b31;--ln:#2c353d;--tx:#e3e6e8;
--dim:#9aa6b0;--ok:#6fd3b4;--warn:#ffc66d;--err:#ff8a80}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
body{margin:0;background:var(--bg);color:var(--tx);font:15px/1.5 -apple-system,
"PingFang SC","Microsoft YaHei",sans-serif;max-width:560px;margin:0 auto;
padding:0 12px 24px}
header{display:flex;justify-content:space-between;align-items:center;
padding:14px 4px;border-bottom:1px solid var(--ln);position:sticky;top:0;
background:rgba(16,20,24,.94);backdrop-filter:blur(8px);z-index:9}
h1{font-size:17px;margin:0}
.pill{font-size:12px;padding:4px 10px;border-radius:99px;background:#2a333b;
color:var(--dim)}
.pill.on{background:rgba(111,211,180,.15);color:var(--ok)}
.pill.busy{background:rgba(255,198,109,.15);color:var(--warn)}
.pill.err{background:rgba(255,138,128,.15);color:var(--err)}
.card{background:var(--s);border-radius:12px;padding:14px;margin-top:12px}
h2{font-size:12px;color:var(--dim);margin:0 0 10px;font-weight:600;
letter-spacing:.04em}
.pad{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;
grid-template-areas:". f ." "l s r" ". b .";touch-action:manipulation}
.pad button{height:64px;border:1px solid var(--ln);border-radius:12px;
background:var(--s2);color:var(--tx);font-size:15px;font-weight:600;
font-family:inherit}
.pad button:active{background:rgba(111,211,180,.25)}
#f{grid-area:f}#l{grid-area:l}#r{grid-area:r}#b{grid-area:b}
#s{grid-area:s;color:var(--err);border-color:rgba(255,138,128,.4)}
.row{display:flex;gap:8px;flex-wrap:wrap;margin-top:10px}
.row button{flex:1;min-width:72px;height:46px;border-radius:10px;
border:1px solid var(--ln);background:var(--s2);color:var(--tx);
font-size:14px;font-family:inherit}
.row button:active{opacity:.7}
.lbl{display:flex;justify-content:space-between;align-items:center;
font-size:14px;margin:10px 0 4px;color:var(--dim)}
input[type=range]{width:100%;height:34px;accent-color:var(--ok)}
input[type=text]{width:100%;padding:10px 12px;border-radius:10px;
border:1px solid var(--ln);background:var(--s2);color:var(--tx);font-size:16px}
.kv{display:flex;justify-content:space-between;font-size:14px;padding:3px 0}
.kv span{color:var(--dim)}.kv b{font-variant-numeric:tabular-nums}
#log{font-family:ui-monospace,Menlo,monospace;font-size:12px;color:var(--dim);
max-height:120px;overflow:auto;white-space:pre-wrap;margin:0}
.hint{font-size:12px;color:var(--dim);margin:10px 0 0;line-height:1.6}
</style>
</head>
<body>
<header><h1>机器狗控制台</h1><span id="pill" class="pill">连接中</span></header>

<div class="card">
  <h2>运动</h2>
  <div class="pad">
    <button id="f" data-act="fwd">前进</button>
    <button id="l" data-act="left">左转</button>
    <button id="s">停止</button>
    <button id="r" data-act="right">右转</button>
    <button id="b" data-act="back">后退</button>
  </div>
  <div class="lbl"><span>步数</span><b id="stepsV">2</b></div>
  <input id="steps" type="range" min="1" max="4" step="1" value="2">
  <div class="lbl"><span>周期（毫秒，越大越慢）</span><b id="periodV">1000</b></div>
  <input id="period" type="range" min="500" max="2000" step="100" value="1000">
  <p class="hint">每次点击执行一次离散动作，不会持续行走。连点会被安全间隔挡住，
  这是为防止队列堆积导致狗一路走到底。</p>
</div>

<div class="card">
  <h2>姿态</h2>
  <div class="row">
    <button data-pose="sit">坐下</button>
    <button data-pose="stand">站起</button>
    <button data-pose="stretch">伸展</button>
    <button data-pose="shake">抖身</button>
    <button data-pose="home">归位</button>
  </div>
</div>

<div class="card">
  <h2>状态</h2>
  <div class="kv"><span>动作</span><b id="kState">—</b></div>
  <div class="kv"><span>热点</span><b id="kSsid">—</b></div>
  <div class="kv"><span>地址</span><b id="kIp">—</b></div>
  <div class="kv"><span>已连客户端</span><b id="kCli">—</b></div>
  <div class="kv"><span>运行时长</span><b id="kUp">—</b></div>
  <div class="kv"><span>可用内存</span><b id="kHeap">—</b></div>
</div>

<div class="card">
  <h2>访问令牌</h2>
  <input id="token" type="text" placeholder="4 位数字" autocomplete="off">
  <p class="hint">默认 1234，与设备启动日志中的一致。修改后立即生效并存在本机浏览器。</p>
</div>

<div class="card">
  <h2>日志</h2>
  <pre id="log"></pre>
</div>

<script>
(function(){
  var TOKEN_KEY='dogctl.token';
  var tokenEl=document.getElementById('token');
  var logEl=document.getElementById('log');
  var pill=document.getElementById('pill');
  var stepsEl=document.getElementById('steps');
  var periodEl=document.getElementById('period');
  var lastMotion=0;

  tokenEl.value=localStorage.getItem(TOKEN_KEY)||'1234';
  tokenEl.addEventListener('change',function(){
    localStorage.setItem(TOKEN_KEY,tokenEl.value.trim());
  });

  function log(msg){
    var t=new Date().toLocaleTimeString();
    logEl.textContent=('['+t+'] '+msg+'\n'+logEl.textContent).slice(0,2000);
  }

  function request(path,method){
    return fetch(path,{
      method:method||'GET',
      headers:{'X-Dog-Token':tokenEl.value.trim()},
      cache:'no-store'
    }).then(function(r){
      return r.json().catch(function(){return {ok:false,msg:'响应不是 JSON'};})
        .then(function(body){ body.status=r.status; return body; });
    });
  }

  function call(path){
    return request(path,'POST').then(function(b){
      if(!b.ok){
        if(b.status===401){log('令牌不正确，请填写设备日志里的 4 位数字');}
        else if(b.status===409){log('上一条动作还没做完');}
        else{log('失败：'+(b.msg||('HTTP '+b.status)));}
        return false;
      }
      return true;
    }).catch(function(e){log('请求失败：'+e.message);return false;});
  }

  function motion(act){
    var now=Date.now();
    if(now-lastMotion<600){log('点击太快，已忽略');return;}
    lastMotion=now;
    var q='/api/motion?act='+act+'&steps='+stepsEl.value+'&period='+periodEl.value;
    call(q).then(function(ok){ if(ok){log('动作：'+act+' '+stepsEl.value+' 步');} });
  }

  document.querySelectorAll('[data-act]').forEach(function(btn){
    btn.addEventListener('click',function(){motion(btn.dataset.act);});
  });
  document.getElementById('s').addEventListener('click',function(){
    call('/api/stop').then(function(ok){ if(ok){log('已停止并归位');} });
  });
  document.querySelectorAll('[data-pose]').forEach(function(btn){
    btn.addEventListener('click',function(){
      var p=btn.dataset.pose;
      call('/api/motion?act='+p+'&steps=1&period=1000')
        .then(function(ok){ if(ok){log('姿态：'+p);} });
    });
  });

  stepsEl.addEventListener('input',function(){
    document.getElementById('stepsV').textContent=stepsEl.value;
  });
  periodEl.addEventListener('input',function(){
    document.getElementById('periodV').textContent=periodEl.value;
  });

  function fmtUp(s){
    if(s<60)return s+' 秒';
    if(s<3600)return Math.floor(s/60)+' 分 '+(s%60)+' 秒';
    return Math.floor(s/3600)+' 时 '+Math.floor(s%3600/60)+' 分';
  }

  function refresh(){
    request('/api/status').then(function(b){
      if(!b.ok){
        if(b.status===401){pill.className='pill err';pill.textContent='令牌错误';}
        else{pill.className='pill err';pill.textContent='连接异常';}
        return;
      }
      document.getElementById('kState').textContent=b.busy?'执行中':'空闲';
      document.getElementById('kSsid').textContent=b.ssid||'—';
      document.getElementById('kIp').textContent=b.ip||'—';
      document.getElementById('kCli').textContent=b.clients;
      document.getElementById('kUp').textContent=fmtUp(b.uptime_s);
      document.getElementById('kHeap').textContent=(b.heap/1024).toFixed(0)+' KB';
      if(b.busy){pill.className='pill busy';pill.textContent='动作执行中';}
      else{pill.className='pill on';pill.textContent='就绪';}
      stepsEl.max=String(b.limit.steps_max);
    }).catch(function(){
      pill.className='pill err';pill.textContent='设备离线';
    });
  }

  refresh();
  setInterval(refresh,1000);
  log('页面已就绪');
})();
</script>
</body>
</html>)DOGPAGE";

#endif  // _DOG_WEB_PAGE_H_
