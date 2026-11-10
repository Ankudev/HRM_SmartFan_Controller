#include "App.h"

#include <DNSServer.h>
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
.toast{position:fixed;bottom:16px;left:50%;transform:translateX(-50%);background:#0b1120;border:1px solid var(--acc);padding:10px 18px;border-radius:20px;font-size:.9rem;display:none;z-index:99}
</style>
</head>
<body>
<h1>HRM SmartFan v2</h1>

<div class="card">
  <h2>Podgląd i sterowanie</h2>
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
  <h2>Wybór pasa tętna (BLE)</h2>
  <button class="primary" onclick="setMode('pair')">🔍 Skanuj pasy HR w pobliżu</button>
  <div class="cand-list" id="candList"></div>
</div>

<div class="card">
  <h2>Strefy tętna (AUTO)</h2>
  <div class="grid-z">
    <span>Strefa</span><span style="text-align:center">Od BPM</span><span style="text-align:center">Moc %</span>
    <span>Z1</span><input type="number" id="t0"><input type="number" id="d0">
    <span>Z2</span><input type="number" id="t1"><input type="number" id="d1">
    <span>Z3</span><input type="number" id="t2"><input type="number" id="d2">
    <span>Z4</span><input type="number" id="t3"><input type="number" id="d3">
    <span>Z5</span><input type="number" id="t4"><input type="number" id="d4">
  </div>
  <div class="grid-p">
    <span>Histereza (1-15 BPM)</span><input type="number" id="hyst">
    <span>Filtr HR (0-10000 ms)</span><input type="number" id="filt">
    <span>Wzrost (1-100 %/s)</span><input type="number" id="rise">
    <span>Spadek (1-100 %/s)</span><input type="number" id="fall">
    <span>Domyślny MANUAL (%)</span><input type="number" id="manDef">
  </div>
  <button class="primary" onclick="saveCfg()">💾 Zapisz ustawienia w pamięci</button>
  <button class="secondary" onclick="resetDef()">Przywróć domyślne</button>
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
    if(!cfgLoaded&&s.cfg){fillCfg(s.cfg);if(s.mode!==2){slider.value=s.cfg.man;document.getElementById('manVal').innerText=s.cfg.man+'%';}cfgLoaded=true;}
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

    char buf[1600]{};
    std::snprintf(
        buf, sizeof(buf),
        "{\"mode\":%u,\"reason\":%u,\"bpm\":%u,\"demand\":%u,\"fresh\":%s,\"manual\":%u,\"radio\":%u,"
        "\"cfg\":{\"t\":[%u,%u,%u,%u,%u],\"d\":[%u,%u,%u,%u,%u],\"hyst\":%u,\"filt\":%u,\"rise\":%u,\"fall\":%u,\"man\":%u},"
        "\"cands\":[",
        unsigned(state.mode), unsigned(state.reason), unsigned(state.bpm), unsigned(state.demand),
        state.fresh ? "true" : "false", unsigned(state.manual), unsigned(radio.state),
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

void redirectToPortal() {
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
}

} // namespace

void webBegin() {
    WiFi.persistent(false);
    WiFi.mode(WIFI_AP);
    WiFi.setSleep(true);
    WiFi.softAPConfig(apIp, apIp, netMask);
    if (!WiFi.softAP("SmartFan")) return;
    dns.setErrorReplyCode(DNSReplyCode::NoError);
    dns.start(dnsPort, "*", apIp);

    server.on("/", HTTP_GET, []() { server.send_P(200, "text/html", indexHtml); });
    server.on("/api/state", HTTP_GET, handleState);
    server.on("/api/mode", HTTP_POST, handleMode);
    server.on("/api/manual", HTTP_POST, handleManual);
    server.on("/api/pair", HTTP_POST, handlePair);
    server.on("/api/config", HTTP_POST, handleConfig);
    server.on("/generate_204", HTTP_GET, redirectToPortal);
    server.on("/hotspot-detect.html", HTTP_GET, redirectToPortal);
    server.on("/ncsi.txt", HTTP_GET, redirectToPortal);
    server.onNotFound(redirectToPortal);
    server.begin();
    started = true;
}

void webUpdate() {
    if (!started) return;
    dns.processNextRequest();
    server.handleClient();
}

} // namespace fan::app

