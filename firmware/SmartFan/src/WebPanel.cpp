#include "App.h"

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace fan::app {
namespace {

constexpr uint16_t dnsPort = 53;
const IPAddress apIp(192, 168, 4, 1);
const IPAddress netMask(255, 255, 255, 0);

DNSServer dns;
WebServer server(80);
bool started = false;
bool mdnsStarted = false;
uint32_t forgetSeen = 0;
String staSsid;
String staPass;
String apPass;

const char indexHtml[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="pl">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>HRM SmartFan v2</title>
<style>
:root{--bg:#0f172a;--card:#1e293b;--fg:#f8fafc;--sub:#94a3b8;--acc:#38bdf8;--ok:#22c55e;--warn:#f59e0b;--err:#ef4444}
*{box-sizing:border-box;margin:0;padding:0;font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif}
body{background:var(--bg);color:var(--fg);padding:12px;max-width:480px;margin:0 auto 40px}
h1{font-size:1.25rem;text-align:center;margin:6px 0 12px;color:var(--acc)}
.card{background:var(--card);border-radius:16px;padding:16px;margin-bottom:14px;box-shadow:0 4px 12px rgba(0,0,0,.3)}
.card h2{font-size:.95rem;color:var(--sub);text-transform:uppercase;letter-spacing:.05em;margin-bottom:12px}
.metrics{display:grid;grid-template-columns:1fr 1fr;gap:10px;text-align:center;margin-bottom:12px}
.metric{background:#0b1120;border-radius:12px;padding:12px}
.metric .val{font-size:2rem;font-weight:700;line-height:1.1}
.metric .lbl{font-size:.78rem;color:var(--sub);margin-top:4px}
.status-bar{text-align:center;font-size:.9rem;padding:8px;border-radius:10px;background:#0b1120;margin-bottom:12px;color:var(--warn)}
.modes{display:grid;grid-template-columns:1fr 1fr 1fr;gap:8px;margin-bottom:12px}
button{border:none;border-radius:12px;padding:12px 10px;font-size:.95rem;font-weight:600;color:#fff;background:#334155;cursor:pointer;transition:.15s}
button:active{transform:scale(.97)}
button.off.active{background:var(--err)}
button.auto.active{background:var(--ok)}
button.man.active{background:var(--acc);color:#0f172a}
button.primary{background:var(--acc);color:#0f172a;width:100%;margin-top:8px}
button.secondary{background:#334155;width:100%;margin-top:8px}
.slider-row{display:flex;align-items:center;gap:8px;margin-top:6px}
.slider-row input[type=range]{flex:1;accent-color:var(--acc);height:28px}
.slider-row button{padding:10px 14px}
.cand-list{margin-top:10px;display:flex;flex-direction:column;gap:8px}
.cand{display:flex;justify-content:space-between;align-items:center;background:#0b1120;padding:10px 12px;border-radius:10px}
.cand .info{font-size:.88rem}
.cand .mac{font-size:.75rem;color:var(--sub)}
.cand button{padding:8px 12px;font-size:.85rem;background:var(--ok)}
.grid-z{display:grid;grid-template-columns:auto 1fr 1fr;gap:8px;align-items:center;margin-bottom:10px;font-size:.88rem}
.grid-z span{color:var(--sub)}
input[type=number]{width:100%;background:#0b1120;border:1px solid #334155;border-radius:8px;padding:8px;color:#fff;font-size:.95rem;text-align:center}
.grid-p{display:grid;grid-template-columns:1fr 110px;gap:8px;align-items:center;font-size:.88rem;margin-top:8px}
.hdr{display:flex;justify-content:space-between;align-items:center;margin-bottom:12px}
.hdr h2{margin-bottom:0}
.q{display:inline-flex;align-items:center;justify-content:center;width:22px;height:22px;border-radius:50%;background:#334155;color:var(--acc);font-size:.8rem;font-weight:700;cursor:pointer;margin-left:6px;user-select:none}
.q:active{background:var(--acc);color:#0f172a}
.modal-bg{position:fixed;inset:0;background:rgba(2,6,23,.78);display:none;align-items:center;justify-content:center;padding:16px;z-index:100}
.modal{background:var(--card);border:1px solid #334155;border-radius:16px;padding:18px;max-width:400px;width:100%;box-shadow:0 10px 30px rgba(0,0,0,.6)}
.modal h3{font-size:1.02rem;color:var(--acc);margin-bottom:8px}
.modal p{font-size:.88rem;line-height:1.48;color:var(--fg);margin-bottom:14px}
.toast{position:fixed;bottom:16px;left:50%;transform:translateX(-50%);background:#0b1120;border:1px solid var(--acc);padding:10px 18px;border-radius:20px;font-size:.9rem;display:none;z-index:99}
</style>
</head>
<body>
<h1>HRM SmartFan v2</h1>

<div class="card">
  <div class="hdr"><h2>Podgląd i sterowanie</h2><span class="q" onclick="help('modes')">?</span></div>
  <div class="metrics">
    <div class="metric"><div class="val" id="bpm">--</div><div class="lbl" id="radioTxt">Pas niepołączony</div></div>
    <div class="metric"><div class="val" id="demand">0%</div><div class="lbl" id="targetTxt">Nastawa</div></div>
  </div>
  <div class="status-bar" id="reasonTxt">Ładowanie...</div>
  <div class="modes">
    <button class="off" id="btnOff" onclick="setMode('off')">OFF</button>
    <button class="auto" id="btnAuto" onclick="setMode('auto')">AUTO (HR)</button>
    <button class="man" id="btnMan" onclick="setMode('manual')">MANUAL</button>
  </div>
  <div class="slider-row">
    <button onclick="stepMan(-5)">-5%</button>
    <input type="range" id="manSlider" min="0" max="100" step="5" value="45" onchange="sendMan(this.value)" oninput="document.getElementById('manVal').innerText=this.value+'%'">
    <button onclick="stepMan(5)">+5%</button>
  </div>
  <div style="text-align:center;font-size:.8rem;color:var(--sub);margin-top:4px">Bieg ręczny: <b id="manVal">45%</b></div>
</div>

<div class="card">
  <div class="hdr"><h2>Wybór pasa tętna (BLE)</h2><span class="q" onclick="help('ble')">?</span></div>
  <button class="primary" onclick="setMode('pair')">🔍 Skanuj pasy HR w pobliżu</button>
  <div class="cand-list" id="candList"></div>
</div>

<div class="card">
  <div class="hdr"><h2>Strefy tętna (AUTO)</h2><span class="q" onclick="help('zones')">?</span></div>
  <div class="grid-z">
    <span>Strefa</span><span style="text-align:center">Od BPM</span><span style="text-align:center">Moc %</span>
    <span>Z1</span><input type="number" id="t0"><input type="number" id="d0">
    <span>Z2</span><input type="number" id="t1"><input type="number" id="d1">
    <span>Z3</span><input type="number" id="t2"><input type="number" id="d2">
    <span>Z4</span><input type="number" id="t3"><input type="number" id="d3">
    <span>Z5</span><input type="number" id="t4"><input type="number" id="d4">
  </div>
  <div class="grid-p">
    <span>Histereza (1 do 15 BPM)<span class="q" onclick="help('hyst')">?</span></span><input type="number" id="hyst">
    <span>Filtr HR (0 do 10000 ms)<span class="q" onclick="help('filt')">?</span></span><input type="number" id="filt">
    <span>Wzrost (1 do 100 %/s)<span class="q" onclick="help('rise')">?</span></span><input type="number" id="rise">
    <span>Spadek (1 do 100 %/s)<span class="q" onclick="help('fall')">?</span></span><input type="number" id="fall">
    <span>Domyślny MANUAL (%)<span class="q" onclick="help('man')">?</span></span><input type="number" id="manDef">
  </div>
  <button class="primary" onclick="saveCfg()">💾 Zapisz ustawienia w pamięci</button>
  <button class="secondary" onclick="resetDef()">Przywróć domyślne</button>
</div>

<details class="card">
  <summary style="cursor:pointer;font-size:.9rem;color:var(--sub);font-weight:600;outline:none;display:flex;justify-content:space-between;align-items:center">
    <span>⚙️ Połączenie z domowym Wi-Fi (opcjonalne)</span>
    <span class="q" onclick="event.preventDefault();event.stopPropagation();help('wifi')">?</span>
  </summary>
  <div style="margin-top:12px">
    <p style="font-size:.82rem;color:var(--sub);line-height:1.42;margin-bottom:10px">
      Połącz sterownik ze swoim domowym routerem, jeśli podczas treningu używasz na telefonie aplikacji wymagających internetu lub sieci domowej (np. Zwift Companion, Spotify, YouTube). Gdy sterownik połączy się z routerem, otworzysz ten panel w domowym Wi-Fi pod adresem <b style="color:var(--fg)">http://smartfan.local</b>.
    </p>
    <div id="wifiStatus" style="font-size:.82rem;color:var(--acc);margin-bottom:10px">Tryb lokalny AP: SmartFan (192.168.4.1)</div>
    <button class="primary" onclick="scanWifi()">📡 Wyszukaj sieci Wi-Fi w pobliżu</button>
    <div class="cand-list" id="wifiList"></div>
    <div id="wifiForm" style="display:none;margin-top:12px;background:#0b1120;padding:12px;border-radius:10px">
      <div style="font-size:.88rem;margin-bottom:8px">Wybrana sieć: <b id="selSsid" style="color:var(--acc)"></b></div>
      <input type="password" id="wPass" placeholder="Wpisz hasło do tej sieci Wi-Fi" style="width:100%;background:#1e293b;border:1px solid #334155;border-radius:8px;padding:10px;color:#fff;font-size:.95rem">
      <button class="primary" onclick="saveWifi()">Połącz z wybraną siecią</button>
    </div>
    <button class="secondary" id="btnWifiDisc" style="display:none" onclick="clearWifi()">Rozłącz domowe Wi-Fi</button>
  </div>
</details>

<div class="modal-bg" id="modal" onclick="closeHelp()">
  <div class="modal" onclick="event.stopPropagation()">
    <h3 id="mTitle"></h3>
    <p id="mBody"></p>
    <button class="primary" onclick="closeHelp()">Rozumiem</button>
  </div>
</div>

<div class="toast" id="toast"></div>

<script>
const reasons=["Wyłączony","Sterowanie aktywne","Czekam na 2 próbki HR","Brak świeżego HR","Brak kontaktu pasa ze skórą","Brak połączenia z pasem","Profil AC niezatwierdzony","Ustaw konfigurację","Czekam na synchronizację Z-C","Błąd napędu / usterka","Tryb wyboru pasa HR","Tryb konfiguracji"];
const radios=["Start","Pas niepołączony","Łączenie z pasem...","Pas połączony","Szukam urządzeń BLE...","Wybierz pas z listy","Sprawdzam pas (załóż na klatkę)...","Brak pasów w zasięgu","Nie zweryfikowano pasa","Zapisano pas HR!","Konfiguracja BLE","Błąd radia BLE","Usunięto parowania","Błąd usuwania par"];
let cfgLoaded=false,manDragging=false;
const slider=document.getElementById('manSlider');
slider.addEventListener('touchstart',()=>manDragging=true);
slider.addEventListener('touchend',()=>manDragging=false);
slider.addEventListener('mousedown',()=>manDragging=true);
slider.addEventListener('mouseup',()=>manDragging=false);

const helps={
  modes:["Tryby pracy wentylatora","OFF wyłącza wentylator. W trybie AUTO (HR) sterownik sam dobiera obroty śmigła do Twojego aktualnego tętna z opaski. W trybie MANUAL ustawiasz stałą moc nawiewu suwakiem, nawet bez założonego pasa tętna."],
  ble:["Jak sparować pas tętna?","Załóż pas na klatkę piersiową i lekko zwilż elektrody, żeby zaczął nadawać. Jeśli pas jest połączony z telefonem (np. w nRF Connect lub aplikacji producenta), rozłącz go tam na chwilę. Kliknij Skanuj pasy HR i wybierz swój pas z listy. Sterownik zapamięta go na stałe i na kolejnych treningach połączy się z nim sam."],
  zones:["Strefy tętna (Z1 do Z5)","Wpisz tutaj progi tętna (Od BPM) i odpowiadającą im moc wentylatora (Moc %). Poniżej progu Z1 wentylator stoi wyłączony (0%). Gdy Twoje tętno przekroczy kolejny próg, nawiew płynnie przyspieszy do przypisanej wartości."],
  hyst:["Co to jest histereza?","Histereza zapobiega ciągłemu przełączaniu biegów w górę i w dół, gdy Twoje tętno waha się na granicy strefy. Przykład: jeśli próg Z2 wynosi 125 BPM, a histereza to 3 BPM, wentylator wejdzie na drugi bieg przy 125 BPM, ale zwolni dopiero wtedy, gdy tętno spadnie poniżej 122 BPM."],
  filt:["Jak działa filtr HR?","Filtr wygładza chwilowe skoki odczytu z pasa tętna. Działa asymetrycznie: gdy zaczynasz podjazd lub sprint i tętno rośnie, reaguje czterokrotnie szybciej, a po zakończeniu wysiłku opada wolniej, żeby dłużej chłodzić rozgrzany organizm."],
  rise:["Tempo wzrostu (%/s)","Określa, o ile procent na sekundę rośnie moc wentylatora przy wchodzeniu na wyższy bieg. Dzięki temu silnik przyspiesza płynnie i bez szarpnięć."],
  fall:["Tempo spadku (%/s)","Określa, o ile procent na sekundę maleją obroty wentylatora, gdy Twoje tętno spadnie do niższej strefy."],
  man:["Domyślny bieg MANUAL (%)","To początkowa moc nawiewu, z jaką ruszy wentylator, gdy włączysz tryb MANUAL fizycznym przyciskiem na obudowie sterownika."],
  wifi:["Po co łączyć sterownik z domowym Wi-Fi?","Domyślnie telefon łączy się bezpośrednio z siecią wentylatora (SmartFan), która nie ma dostępu do internetu. Jeśli w trakcie jazdy używasz na telefonie aplikacji wymagających internetu lub sieci domowej (np. Zwift Companion, Spotify czy YouTube), wybierz z listy swoje domowe Wi-Fi i wpisz hasło. Od tej pory nie musisz już przełączać Wi-Fi w telefonie, a ten panel otworzysz w domowej sieci pod adresem http://smartfan.local (lub po adresie IP pokazanym na pasku statusu)."]
};
function help(k){
  const h=helps[k];if(!h)return;
  document.getElementById('mTitle').innerText=h[0];
  document.getElementById('mBody').innerText=h[1];
  document.getElementById('modal').style.display='flex';
}
function closeHelp(){document.getElementById('modal').style.display='none';}

function msg(t){const e=document.getElementById('toast');e.innerText=t;e.style.display='block';setTimeout(()=>e.style.display='none',2600);}
async function post(u,b){try{const r=await fetch(u,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b});const j=await r.json();if(!r.ok)msg(j.error||'Błąd');else if(j.msg)msg(j.msg);poll();}catch(e){msg('Błąd połączenia');}}
function setMode(m){post('/api/mode','mode='+m);}
function sendMan(v){post('/api/manual','value='+v);}
function stepMan(s){let v=parseInt(slider.value||0)+s;v=Math.max(0,Math.min(100,v));slider.value=v;document.getElementById('manVal').innerText=v+'%';sendMan(v);}
function pickCand(i){post('/api/pair','index='+i);}

function fillCfg(c){
  for(let i=0;i<5;i++){document.getElementById('t'+i).value=c.t[i];document.getElementById('d'+i).value=c.d[i];}
  document.getElementById('hyst').value=c.hyst;
  document.getElementById('filt').value=c.filt;
  document.getElementById('rise').value=c.rise;
  document.getElementById('fall').value=c.fall;
  document.getElementById('manDef').value=c.man;
}
function resetDef(){fillCfg({t:[105,125,145,165,200],d:[30,45,60,75,95],hyst:3,filt:2000,rise:15,fall:8,man:45});msg('Wpisano domyślne – kliknij Zapisz');}
function saveCfg(){
  const p=[];
  for(let i=0;i<5;i++)p.push('t'+i+'='+document.getElementById('t'+i).value,'d'+i+'='+document.getElementById('d'+i).value);
  p.push('hyst='+document.getElementById('hyst').value,'filt='+document.getElementById('filt').value,'rise='+document.getElementById('rise').value,'fall='+document.getElementById('fall').value,'man='+document.getElementById('manDef').value);
  post('/api/config',p.join('&'));
}
let pickedSsid='';
async function scanWifi(){
  const wl=document.getElementById('wifiList');
  wl.innerHTML='<div style="text-align:center;color:var(--sub);padding:8px">Szukam sieci Wi-Fi (ok. 2 s)...</div>';
  try{
    const r=await fetch('/api/wifi_scan');
    const nets=await r.json();
    if(!nets.length){wl.innerHTML='<div style="text-align:center;color:var(--warn);padding:8px">Nie znaleziono sieci 2,4 GHz w zasięgu.</div>';return;}
    wl.innerHTML=nets.map(n=>`<div class="cand"><div class="info"><div>📶 <b>${n.ssid}</b></div><div class="mac">Sygnał: ${n.rssi} dBm</div></div><button onclick="selectWifi(${JSON.stringify(n.ssid).replace(/"/g,'&quot;')})">Wybierz</button></div>`).join('');
  }catch(e){wl.innerHTML='<div style="text-align:center;color:var(--warn);padding:8px">Błąd skanowania Wi-Fi</div>';}
}
function selectWifi(s){
  pickedSsid=s;
  document.getElementById('selSsid').innerText=s;
  document.getElementById('wifiForm').style.display='block';
  document.getElementById('wPass').focus();
}
function saveWifi(){
  if(!pickedSsid)return;
  const s=encodeURIComponent(pickedSsid);
  const p=encodeURIComponent(document.getElementById('wPass').value);
  post('/api/wifi','ssid='+s+'&pass='+p);
  document.getElementById('wifiForm').style.display='none';
  document.getElementById('wifiList').innerHTML='';
}
function clearWifi(){
  post('/api/wifi','ssid=&pass=');
}

async function poll(){
  try{
    const r=await fetch('/api/state');
    const s=await r.json();
    document.getElementById('bpm').innerText=s.fresh?(s.bpm+' BPM'):'-- BPM';
    document.getElementById('demand').innerText=s.demand+'%';
    document.getElementById('targetTxt').innerText=s.mode===2?('Cel ręczny '+s.manual+'%'):'Nastawa';
    document.getElementById('reasonTxt').innerText=reasons[s.reason]||'';
    document.getElementById('radioTxt').innerText=radios[s.radio]||'';
    document.getElementById('btnOff').classList.toggle('active',s.mode===0);
    document.getElementById('btnAuto').classList.toggle('active',s.mode===1);
    document.getElementById('btnMan').classList.toggle('active',s.mode===2);
    if(!manDragging&&s.mode===2){slider.value=s.manual;document.getElementById('manVal').innerText=s.manual+'%';}
    if(!cfgLoaded&&s.cfg){
      fillCfg(s.cfg);
      if(s.mode!==2){slider.value=s.cfg.man;document.getElementById('manVal').innerText=s.cfg.man+'%';}
      cfgLoaded=true;
    }
    if(s.wifi){
      const ws=document.getElementById('wifiStatus');
      ws.innerText=s.wifi.ip?('Połączono z '+s.wifi.ssid+' -> http://smartfan.local ('+s.wifi.ip+')'):(s.wifi.ssid?('Łączenie z '+s.wifi.ssid+'... (AP: 192.168.4.1)'):'Tryb lokalny AP: SmartFan (192.168.4.1)');
      document.getElementById('btnWifiDisc').style.display=s.wifi.ssid?'block':'none';
    }
    const cl=document.getElementById('candList');
    if(s.mode===3&&s.cands&&s.cands.length){
      cl.innerHTML=s.cands.map((c,i)=>`<div class="cand"><div class="info"><div>${c.hr?'❤️ ':''}<b>${c.name}</b></div><div class="mac">${c.mac}</div></div><button onclick="pickCand(${i})">Połącz</button></div>`).join('');
    }else if(s.mode===3&&s.radio===4){
      cl.innerHTML='<div style="text-align:center;color:var(--sub);padding:8px">Trwa skanowanie BLE (2,5 s)...</div>';
    }else if(s.mode===3&&s.radio===7){
      cl.innerHTML='<div style="text-align:center;color:var(--warn);padding:8px">Nie znaleziono pasów. Upewnij się, że pas jest założony i niepołączony z zegarkiem/telefonem.</div>';
    }else{cl.innerHTML='';}
  }catch(e){}
}
setInterval(poll,1000);poll();
</script>
</body>
</html>)rawliteral";

void appendJsonEscaped(char* out, size_t cap, const char* in) {
    size_t len = std::strlen(out);
    for (size_t i = 0; in[i] && len + 2 < cap; ++i) {
        const char c = in[i];
        if (c == '"' || c == '\\') {
            if (len + 3 >= cap) break;
            out[len++] = '\\';
        }
        out[len++] = (c >= 32 && c <= 126) ? c : '?';
    }
    out[len] = '\0';
}

void handleState() {
    Snapshot state{};
    RadioView radio{};
    ConfigView cfg{};
    if (controlView) xQueuePeek(controlView, &state, 0);
    if (radioView) xQueuePeek(radioView, &radio, 0);
    if (activeConfigView) xQueuePeek(activeConfigView, &cfg, 0);
    if (!cfg.valid) cfg.config = Config{};

    char safeSsid[36]{};
    appendJsonEscaped(safeSsid, sizeof(safeSsid), staSsid.c_str());
    const String staIp = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : String("");

    char buf[1800]{};
    std::snprintf(
        buf, sizeof(buf),
        "{\"mode\":%u,\"reason\":%u,\"bpm\":%u,\"demand\":%u,\"fresh\":%s,\"manual\":%u,\"radio\":%u,"
        "\"wifi\":{\"ssid\":\"%s\",\"ip\":\"%s\",\"apSec\":%s},"
        "\"cfg\":{\"t\":[%u,%u,%u,%u,%u],\"d\":[%u,%u,%u,%u,%u],\"hyst\":%u,\"filt\":%u,\"rise\":%u,\"fall\":%u,\"man\":%u},"
        "\"cands\":[",
        unsigned(state.mode), unsigned(state.reason), unsigned(state.bpm), unsigned(state.demand),
        state.fresh ? "true" : "false", unsigned(state.manual), unsigned(radio.state),
        safeSsid, staIp.c_str(), apPass.length() >= 8 ? "true" : "false",
        unsigned(cfg.config.threshold[0]), unsigned(cfg.config.threshold[1]), unsigned(cfg.config.threshold[2]),
        unsigned(cfg.config.threshold[3]), unsigned(cfg.config.threshold[4]),
        unsigned(cfg.config.demand[0]), unsigned(cfg.config.demand[1]), unsigned(cfg.config.demand[2]),
        unsigned(cfg.config.demand[3]), unsigned(cfg.config.demand[4]),
        unsigned(cfg.config.hysteresis), unsigned(cfg.config.filterMs), unsigned(cfg.config.risePerSecond),
        unsigned(cfg.config.fallPerSecond), unsigned(cfg.config.manual));

    const uint8_t count = radio.count <= 12 ? radio.count : 12;
    for (uint8_t i = 0; i < count; ++i) {
        char item[110]{};
        char safeName[32]{};
        appendJsonEscaped(safeName, sizeof(safeName), radio.items[i].label);
        std::snprintf(item, sizeof(item), "%s{\"name\":\"%s\",\"mac\":\"%s\",\"hr\":%s}",
                      i ? "," : "", safeName, radio.items[i].address,
                      radio.items[i].heartRate ? "true" : "false");
        std::strncat(buf, item, sizeof(buf) - std::strlen(buf) - 1);
    }
    std::strncat(buf, "]}", sizeof(buf) - std::strlen(buf) - 1);
    server.send(200, "application/json", buf);
}

void handleMode() {
    const String m = server.arg("mode");
    UiCommand cmd{UiAction::SetMode, Mode::Off, 0};
    if (m == "auto") cmd.mode = Mode::Auto;
    else if (m == "manual") cmd.mode = Mode::Manual;
    else if (m == "pair") cmd.mode = Mode::Pair;
    else if (m == "config") cmd.mode = Mode::Config;
    else cmd.mode = Mode::Off;
    if (uiCommand) xQueueOverwrite(uiCommand, &cmd);
    server.send(200, "application/json", "{\"ok\":true}");
}

void handleManual() {
    const int val = server.arg("value").toInt();
    const uint8_t clamped = val < 0 ? 0 : (val > 100 ? 100 : uint8_t(val));
    UiCommand cmd{UiAction::SetManual, Mode::Manual, clamped};
    if (uiCommand) xQueueOverwrite(uiCommand, &cmd);
    server.send(200, "application/json", "{\"ok\":true}");
}

void handlePair() {
    const int idx = server.arg("index").toInt();
    if (idx < 0 || idx >= 12) {
        server.send(400, "application/json", "{\"error\":\"Błędny indeks pasa\"}");
        return;
    }
    UiCommand cmd{UiAction::PairSelect, Mode::Pair, uint8_t(idx)};
    if (uiCommand) xQueueOverwrite(uiCommand, &cmd);
    server.send(200, "application/json", "{\"msg\":\"Sprawdzam wybrany pas HR...\"}");
}

bool parseUint16(const String& s, uint16_t& out) {
    if (s.length() == 0) return false;
    char* end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 10);
    if (!end || *end != '\0' || v < 0 || v > 65535) return false;
    out = uint16_t(v);
    return true;
}

void handleConfig() {
    Config cfg{};
    bool ok = true;
    for (size_t i = 0; i < zoneCount; ++i) {
        ok = ok && parseUint16(server.arg("t" + String(i)), cfg.threshold[i]);
        ok = ok && parseUint16(server.arg("d" + String(i)), cfg.demand[i]);
    }
    ok = ok && parseUint16(server.arg("hyst"), cfg.hysteresis);
    ok = ok && parseUint16(server.arg("filt"), cfg.filterMs);
    ok = ok && parseUint16(server.arg("rise"), cfg.risePerSecond);
    ok = ok && parseUint16(server.arg("fall"), cfg.fallPerSecond);
    ok = ok && parseUint16(server.arg("man"), cfg.manual);

    if (!ok || !valid(cfg)) {
        server.send(400, "application/json",
                    "{\"error\":\"Nieprawidłowe zakresy (progi muszą rosnąć z odstępem > histereza, moce 30-100%)\"}");
        return;
    }
    if (configSaveRequest) xQueueOverwrite(configSaveRequest, &cfg);
    server.send(200, "application/json", "{\"msg\":\"Zapisano ustawienia w pamięci!\"}");
}

void applyWifiConfig() {
    WiFi.persistent(false);
    WiFi.mode(staSsid.length() > 0 ? WIFI_AP_STA : WIFI_AP);
    WiFi.setSleep(true);
    WiFi.softAPConfig(apIp, apIp, netMask);
    if (apPass.length() >= 8) WiFi.softAP("SmartFan", apPass.c_str());
    else WiFi.softAP("SmartFan");
    if (staSsid.length() > 0) {
        WiFi.setHostname("smartfan");
        WiFi.begin(staSsid.c_str(), staPass.c_str());
    } else {
        WiFi.disconnect(false);
    }
}

void handleWifiScan() {
    WiFi.mode(WIFI_AP_STA);
    const int n = WiFi.scanNetworks(false, false, false, 120);
    char buf[1200] = "[";
    int added = 0;
    for (int i = 0; i < n && added < 10; ++i) {
        const String ssid = WiFi.SSID(i);
        if (ssid.length() == 0 || ssid == "SmartFan") continue;
        bool duplicate = false;
        for (int j = 0; j < i; ++j) {
            if (WiFi.SSID(j) == ssid) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;
        char safe[36]{};
        appendJsonEscaped(safe, sizeof(safe), ssid.c_str());
        char item[96]{};
        std::snprintf(item, sizeof(item), "%s{\"ssid\":\"%s\",\"rssi\":%d}",
                      added ? "," : "", safe, int(WiFi.RSSI(i)));
        std::strncat(buf, item, sizeof(buf) - std::strlen(buf) - 1);
        ++added;
    }
    WiFi.scanDelete();
    std::strncat(buf, "]", sizeof(buf) - std::strlen(buf) - 1);
    server.send(200, "application/json", buf);
}

void handleWifi() {
    const String newSsid = server.arg("ssid");
    const String newPass = server.arg("pass");
    Preferences prefs;
    if (!prefs.begin("smartfan_wifi", false)) {
        server.send(500, "application/json", "{\"error\":\"Błąd zapisu NVS\"}");
        return;
    }
    staSsid = newSsid;
    staPass = newPass;
    prefs.putString("sta_ssid", staSsid);
    prefs.putString("sta_pass", staPass);
    prefs.end();

    server.send(200, "application/json",
                staSsid.length() > 0 ? "{\"msg\":\"Zapisano! Łączenie z siecią (http://smartfan.local)...\"}"
                                     : "{\"msg\":\"Rozłączono domowe Wi-Fi.\"}");
    delay(80);
    mdnsStarted = false;
    applyWifiConfig();
}

void redirectToPortal() {
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
}

} // namespace

void webBegin() {
    Preferences prefs;
    if (prefs.begin("smartfan_wifi", true)) {
        staSsid = prefs.getString("sta_ssid", "");
        staPass = prefs.getString("sta_pass", "");
        apPass = prefs.getString("ap_pass", "");
        prefs.end();
    }
    applyWifiConfig();
    dns.setErrorReplyCode(DNSReplyCode::NoError);
    dns.start(dnsPort, "*", apIp);

    server.on("/", HTTP_GET, []() { server.send_P(200, "text/html", indexHtml); });
    server.on("/api/state", HTTP_GET, handleState);
    server.on("/api/mode", HTTP_POST, handleMode);
    server.on("/api/manual", HTTP_POST, handleManual);
    server.on("/api/pair", HTTP_POST, handlePair);
    server.on("/api/config", HTTP_POST, handleConfig);
    server.on("/api/wifi_scan", HTTP_GET, handleWifiScan);
    server.on("/api/wifi", HTTP_POST, handleWifi);
    server.on("/generate_204", HTTP_GET, redirectToPortal);
    server.on("/hotspot-detect.html", HTTP_GET, redirectToPortal);
    server.on("/ncsi.txt", HTTP_GET, redirectToPortal);
    server.onNotFound(redirectToPortal);
    server.begin();
    started = true;
}

void webUpdate() {
    if (!started) return;
    if (!mdnsStarted && WiFi.status() == WL_CONNECTED) {
        if (MDNS.begin("smartfan")) {
            MDNS.addService("http", "tcp", 80);
            mdnsStarted = true;
        }
    }
    Request req{};
    if (radioRequest && xQueuePeek(radioRequest, &req, 0) == pdTRUE && req.forget != forgetSeen) {
        forgetSeen = req.forget;
        if (apPass.length() > 0) {
            apPass = "";
            Preferences prefs;
            if (prefs.begin("smartfan_wifi", false)) {
                prefs.putString("ap_pass", "");
                prefs.end();
            }
            applyWifiConfig();
        }
    }
    dns.processNextRequest();
    server.handleClient();
}

} // namespace fan::app

