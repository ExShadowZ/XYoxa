/* ============================================================
 *  MOXA-Web — веб-интерфейс для адаптера RS-232/422/485
 *  ESP-07 (ESP8266) + STM32
 *
 *  Связь со STM32:
 *    STM32 → ESP:  байтовый поток с префиксами 1:/2:/3:
 *                  1: = TCP (фиолетовый)
 *                  2: = RS  (синий)
 *                  3: = COM (жёлтый)
 *                  Префикс ставится при смене источника и после \n/\r
 *    ESP → STM32:  команды CMD=...\n
 * ============================================================ */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>

const char* AP_SSID        = "XYoxa";
const char* DEVICE_NAME    = "XYoxa v1.7";
const char* DEVICE_VERSION = "1.7.16";
const char* DEVICE_AUTHOR  = "ExShadow";

#define UART_BAUD_DEFAULT   921600
#define UART_RX_BUF         2048

ESP8266WebServer server(80);

/* ============================================================
 *  EEPROM
 * ============================================================ */
#define EEPROM_SIZE     512
#define EEPROM_MAGIC    0xA7

struct Settings {
    uint8_t  magic;
    uint8_t  dev1;
    uint8_t  dev2;
    uint32_t baudrate;
    uint8_t  databits;
    uint8_t  parity;
    uint8_t  stopbits;
    uint8_t  handshake;

    uint16_t port;
    uint8_t  mac[6];
    uint8_t  ip[4];
    uint8_t  sn[4];
    uint8_t  gw[4];
    uint8_t  dns[4];
    uint8_t  dhcp;

    uint8_t  theme;
    char     ap_password[32];
    uint8_t  ap_ip[4];
};

Settings settings;

void settings_defaults() {
    settings.magic     = EEPROM_MAGIC;
    settings.dev1      = 0;
    settings.dev2      = 3;
    settings.baudrate  = 115200;
    settings.databits  = 8;
    settings.parity    = 0;
    settings.stopbits  = 1;
    settings.handshake = 0;
    settings.port      = 5000;
    settings.mac[0]=0x00; settings.mac[1]=0x08; settings.mac[2]=0xdc;
    settings.mac[3]=0xab; settings.mac[4]=0xcd; settings.mac[5]=0xef;
    settings.ip[0]=192; settings.ip[1]=168; settings.ip[2]=100; settings.ip[3]=200;
    settings.sn[0]=255; settings.sn[1]=255; settings.sn[2]=255; settings.sn[3]=0;
    settings.gw[0]=192; settings.gw[1]=168; settings.gw[2]=100; settings.gw[3]=1;
    settings.dns[0]=8; settings.dns[1]=8; settings.dns[2]=8; settings.dns[3]=8;
    settings.dhcp      = 0;
    settings.theme     = 0;
    strncpy(settings.ap_password, "12345678", sizeof(settings.ap_password) - 1);
    settings.ap_password[sizeof(settings.ap_password) - 1] = '\0';
    settings.ap_ip[0]=192; settings.ap_ip[1]=168;
    settings.ap_ip[2]=4;   settings.ap_ip[3]=1;
}

void settings_load() {
    EEPROM.begin(EEPROM_SIZE);
    EEPROM.get(0, settings);
    if (settings.magic != EEPROM_MAGIC) {
        settings_defaults();
        EEPROM.put(0, settings);
        EEPROM.commit();
    }
}

void settings_save() {
    settings.magic = EEPROM_MAGIC;
    EEPROM.put(0, settings);
    EEPROM.commit();
}

/* ============================================================
 *  Буфер приёма от STM32 (для XYrminal)
 *
 *  Храним ГОТОВЫЕ строки терминала: prefix + text.
 *  Префикс — одно из: 0=TCP, 1=RX, 2=COM (внутреннее представление).
 * ============================================================ */
struct TermLine {
    uint8_t prefix;   /* 0=TCP, 1=RX, 2=COM */
    String  text;
};

#define TERM_MAX_LINES  200
TermLine term_lines[TERM_MAX_LINES];
int      term_line_count = 0;

/* Текущая строка в процессе сборки */
uint8_t  cur_prefix = 255;   /* 255 = ещё ничего не начато */
String   cur_text   = "";

/* Прототипы */
void term_flush_current();

/* ============================================================
 *  UART — STM32
 * ============================================================ */
bool stm_ping(uint32_t timeout_ms) {
    while (Serial.available()) Serial.read();
    Serial.print("CMD=CONNECT\n");

    String resp = "";
    unsigned long t = millis();
    while (millis() - t < timeout_ms) {
        while (Serial.available()) {
            char c = Serial.read();
            if (c == '\n') {
                resp.trim();
                return (resp == "OK");
            }
            if (c != '\r') resp += c;
        }
        delay(1);
    }
    return false;
}

String stm_command(const String &cmd, uint32_t timeout_ms) {
    while (Serial.available()) Serial.read();
    Serial.print(cmd);
    Serial.print("\n");

    String resp = "";
    unsigned long t = millis();
    while (millis() - t < timeout_ms) {
        while (Serial.available()) {
            char c = Serial.read();
            if (c == '\n') { resp.trim(); return resp; }
            if (c != '\r') resp += c;
        }
        delay(1);
    }
    return "";
}

/* ============================================================
 *  Терминал: парсер префиксов
 *
 *  Правила:
 *   - 1:/2:/3: в начале строки (после \n, \r или первого байта) —
 *     начало новой строки с префиксом TCP/RX/COM
 *   - \n или \r — завершает текущую строку терминала
 *   - остальные байты — в текст
 * ============================================================ */
void term_flush_current()
{
    if (cur_prefix == 255 && cur_text.length() == 0) return;

    if (term_line_count >= TERM_MAX_LINES) {
        /* Сдвигаем половину старых */
        int drop = TERM_MAX_LINES / 2;
        for (int i = 0; i < TERM_MAX_LINES - drop; i++) {
            term_lines[i] = term_lines[i + drop];
        }
        term_line_count -= drop;
    }

    TermLine &ln = term_lines[term_line_count++];
    ln.prefix = (cur_prefix == 255) ? 0 : cur_prefix;
    ln.text   = cur_text;

    cur_prefix = 255;
    cur_text   = "";
}

/* Проверяет, начинается ли новый байт с префикса 1:/2:/3: */
static int try_prefix(uint8_t c, uint8_t &out_prefix)
{
    if (c == '1') { out_prefix = 0; return 1; }
    if (c == '2') { out_prefix = 1; return 1; }
    if (c == '3') { out_prefix = 2; return 1; }
    return 0;
}

void term_feed(const uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        uint8_t c = data[i];

        /* Потенциальный префикс в начале строки */
        if (cur_prefix == 255) {
            uint8_t pfx;
            if (try_prefix(c, pfx)) {
                /* Проверяем следующий байт: должен быть ':' */
                if (i + 1 < len && data[i+1] == ':') {
                    cur_prefix = pfx;
                    i++;  /* пропускаем ':' */
                    continue;
                }
                /* иначе — это просто байт данных '1','2','3' */
            }
            /* Если cur_prefix всё ещё 255, ставим дефолт TCP (но по факту
               STM32 всегда ставит префикс) */
            cur_prefix = 0;
        }

        if (c == '\n' || c == '\r') {
            term_flush_current();
            continue;
        }

        cur_text += (char)c;
    }

    /* Автозавершение при отсутствии данных — по таймауту */
    if (cur_text.length() > 0) {
        static unsigned long last_add_ms = 0;
        if (millis() - last_add_ms > 50) {
            /* не рвём — просто запоминаем время */
        }
        last_add_ms = millis();
    }
}

/* ============================================================
 *  УТИЛИТЫ
 * ============================================================ */
String mac_to_str(const uint8_t *mac) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}

String ip_to_str(const uint8_t *ip) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    return String(buf);
}

bool str_to_mac(const String &s, uint8_t *mac) {
    int vals[6];
    int n = sscanf(s.c_str(), "%x:%x:%x:%x:%x:%x",
                   &vals[0], &vals[1], &vals[2], &vals[3], &vals[4], &vals[5]);
    if (n != 6) return false;
    for (int i = 0; i < 6; i++) {
        if (vals[i] < 0 || vals[i] > 255) return false;
        mac[i] = (uint8_t)vals[i];
    }
    return true;
}

bool str_to_ip(const String &s, uint8_t *ip) {
    int vals[4];
    int n = sscanf(s.c_str(), "%d.%d.%d.%d", &vals[0], &vals[1], &vals[2], &vals[3]);
    if (n != 4) return false;
    for (int i = 0; i < 4; i++) {
        if (vals[i] < 0 || vals[i] > 255) return false;
        ip[i] = (uint8_t)vals[i];
    }
    return true;
}

/* ============================================================
 *  CSS
 * ============================================================ */
const char COMMON_CSS[] PROGMEM = R"CSS(
<style>
  :root {
    --bg:      #1a1a1a;
    --card:    #2a2a2a;
    --border:  #444;
    --accent:  #2e7d32;
    --topbar:  #333;
    --topbar-h:#444;
  }
  body.theme-1 { --bg:#1a0d2e; --card:#2a1a4a; --border:#4a2a7a; --accent:#7c4dff; --topbar:#3a1a5a; --topbar-h:#4a2a7a; }
  body.theme-2 { --bg:#000;    --card:#111;    --border:#222;    --accent:#2e7d32; --topbar:#111;    --topbar-h:#222; }

  * { box-sizing: border-box; }
  body { font-family: -apple-system, Arial, sans-serif; margin: 0; padding: 16px;
         background: var(--bg); color: #e0e0e0; max-width: 720px; margin: 0 auto; }
  h1 { font-size: 20px; margin: 0 0 12px; }
  h2 { font-size: 16px; margin: 16px 0 8px; color: #bbb; }
  .topbar { display: flex; justify-content: space-between; align-items: center; margin-bottom: 16px; }
  .nav-btn { display: inline-block; padding: 8px 16px; background: var(--topbar); color: #fff;
             border-radius: 6px; text-decoration: none; font-size: 14px; border: 1px solid var(--border); }
  .nav-btn:hover { background: var(--topbar-h); }
  .nav-btn.home { background: #1565c0; border-color: #1976d2; }
  .big-btn { display: block; text-align: center; padding: 18px; margin-bottom: 10px;
             border-radius: 10px; text-decoration: none; color: white; font-size: 17px; font-weight: bold; }
  .big-btn.com { background: #1565c0; }
  .big-btn.eth { background: #2e7d32; }
  .big-btn.term{ background: #6a1b9a; }
  .big-btn.set { background: #455a64; }
  .status { display: inline-block; padding: 8px 16px; border-radius: 6px;
            font-weight: bold; background: var(--card); color: #aaa; font-size: 14px; }
  .status.ok { background: #1b5e20; color: #b9f6ca; }
  .status.err{ background: #b71c1c; color: #ffcdd2; }
  .status.warn{background: #e65100; color: #ffe0b2; }
  fieldset { border: 1px solid var(--border); border-radius: 8px; margin-bottom: 14px;
             padding: 10px 14px; background: var(--card); }
  legend { padding: 0 6px; color: #888; }
  label { display: block; margin: 8px 0 4px; font-size: 13px; color: #bbb; }
  select, input, textarea { width: 100%; padding: 8px; box-sizing: border-box;
                  background: var(--card); color: #eee; border: 1px solid var(--border);
                  border-radius: 6px; font-size: 14px; font-family: inherit; }
  .row { display: flex; gap: 10px; align-items: flex-end; }
  .row > * { flex: 1; }
  .row > button { flex: 0 0 auto; width: auto; padding: 8px 16px; margin: 0; }
  button { width: 100%; padding: 14px; font-size: 16px; font-weight: bold;
           border: none; border-radius: 8px; margin-top: 8px; cursor: pointer;
           background: var(--accent); color: white; }
  button.gray { background: #455a64; }
  button:active { opacity: 0.7; }
  .hidden { display: none; }
  .footer { margin-top: 32px; padding-top: 16px; border-top: 1px solid var(--border);
            font-size: 12px; color: #666; text-align: center; line-height: 1.6; }
  .term {
    background: #000; font-family: 'Consolas','Monaco',monospace;
    font-size: 13px; padding: 12px; border-radius: 6px; height: 400px;
    overflow-y: auto; white-space: pre-wrap; word-break: break-all;
    border: 1px solid var(--border);
  }
  .term .ln  { display: block; }
  .pfx-tcp { color: #b388ff; }   /* фиолетовый */
  .pfx-rx  { color: #448aff; }   /* синий */
  .pfx-com { color: #ffca28; }   /* жёлтый */
  .txt     { color: #0f0; }      /* зелёный */
  .msg { padding: 10px; border-radius: 6px; margin-top: 12px; font-size: 14px; }
  .msg.ok  { background: #1b5e20; color: #b9f6ca; }
  .msg.err { background: #b71c1c; color: #ffcdd2; }
  .hint { font-size: 11px; color: #666; margin-top: 2px; }
  .theme-row { display: flex; gap: 10px; margin-top: 8px; }
  .theme-btn { flex: 1; padding: 14px; border-radius: 8px; cursor: pointer;
    border: 3px solid transparent; color: #fff; font-weight: bold;
    text-align: center; user-select: none; }
  .theme-btn.t0 { background: #2a2a2a; }
  .theme-btn.t1 { background: #2a1a4a; }
  .theme-btn.t2 { background: #111; border-color: #333; }
  .theme-btn.active { border-color: #2e7d32; box-shadow: 0 0 8px rgba(46,125,50,0.7); }
</style>
)CSS";

/* ============================================================
 *  HTML — ГЛАВНАЯ
 * ============================================================ */
const char PAGE_INDEX[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="ru">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>MOXA — Главная</title>
%CSS%
</head>
<body class="theme-%THEME%">
  <div class="topbar"><h1>XYOXA Adapter</h1></div>

  <div id="status" class="status">Проверка связи...</div>
  <button class="gray" onclick="ping()">🔄 Проверить связь</button>

  <h2>Разделы</h2>
  <a href="/com"      class="big-btn com">🔌 COM</a>
  <a href="/ethernet" class="big-btn eth">🌐 Ethernet</a>
  <a href="/terminal" class="big-btn term">💻 XYrminal</a>
  <a href="/settings" class="big-btn set">⚙️ Настройки</a>

  <div class="footer">
    <div><b>%DEVNAME%</b></div>
    <div>Версия прошивки: %VERSION%</div>
    <div>Автор: %AUTHOR%</div>
  </div>

<script>
function setStatus(txt, cls) {
  const s = document.getElementById('status');
  s.textContent = txt;
  s.className = 'status' + (cls ? ' ' + cls : '');
}
function ping() {
  setStatus('Проверка связи...', 'warn');
  fetch('/api/ping').then(r => r.text()).then(ans => {
    ans = ans.trim();
    if (ans === 'OK') setStatus('Соединение со STM32 установлено', 'ok');
    else              setStatus('STM32 не отвечает', 'err');
  }).catch(() => setStatus('Ошибка связи с ESP', 'err'));
}
ping();
</script>
</body>
</html>
)HTML";

/* ============================================================
 *  HTML — COM
 * ============================================================ */
const char PAGE_COM[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="ru">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>MOXA — COM</title>
%CSS%
</head>
<body class="theme-%THEME%">
  <div class="topbar"><h1>COM</h1><a href="/" class="nav-btn home">🏠 На главную</a></div>

  <fieldset>
    <legend>Интерфейс</legend>
    <label>Тип</label>
    <select id="dev2">
      <option value="2">UART</option>
      <option value="3">RS-232</option>
      <option value="5">RS-422</option>
      <option value="6">RS-485 4W</option>
      <option value="4">RS-485 2W</option>
    </select>
  </fieldset>

  <fieldset>
    <legend>Параметры UART</legend>
    <label>Baud rate</label>
    <select id="baud_preset" onchange="onBaudPreset()">
      <option value="300">300</option><option value="1200">1200</option>
      <option value="2400">2400</option><option value="4800">4800</option>
      <option value="9600">9600</option><option value="19200">19200</option>
      <option value="38400">38400</option><option value="57600">57600</option>
      <option value="115200">115200</option><option value="230400">230400</option>
      <option value="460800">460800</option><option value="921600">921600</option>
      <option value="custom">Своя...</option>
    </select>
    <input id="baud_custom" type="number" min="300" max="3000000"
           placeholder="Своя скорость" class="hidden" style="margin-top:6px;">
    <label>Data bits</label>
    <select id="databits">
      <option value="5">5</option><option value="6">6</option>
      <option value="7">7</option><option value="8" selected>8</option>
      <option value="9">9</option>
    </select>
    <label>Parity</label>
    <select id="parity">
      <option value="0">None</option><option value="1">Even</option><option value="2">Odd</option>
    </select>
    <label>Stop bits</label>
    <select id="stopbits"><option value="1">1</option><option value="2">2</option></select>
    <label>Handshaking</label>
    <select id="handshake">
      <option value="0">None</option><option value="1">RTS/CTS</option><option value="2">XON/XOFF</option>
    </select>
  </fieldset>

  <button onclick="saveSettings()">💾 Отправить настройки</button>
  <div id="msg" class="msg hidden"></div>

<script>
function showMsg(txt, cls) {
  const m = document.getElementById('msg');
  m.textContent = txt; m.className = 'msg ' + cls; m.classList.remove('hidden');
  setTimeout(() => m.classList.add('hidden'), 4000);
}
function onBaudPreset() {
  const v = document.getElementById('baud_preset').value;
  document.getElementById('baud_custom').classList.toggle('hidden', v !== 'custom');
}
function getBaud() {
  const p = document.getElementById('baud_preset').value;
  return (p === 'custom') ? document.getElementById('baud_custom').value : p;
}
function saveSettings() {
  const data = {
    dev2: document.getElementById('dev2').value,
    baud: getBaud(),
    databits: document.getElementById('databits').value,
    parity: document.getElementById('parity').value,
    stopbits: document.getElementById('stopbits').value,
    handshake: document.getElementById('handshake').value
  };
  fetch('/api/com', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify(data)})
    .then(r => r.text()).then(ans => {
      if (ans.trim() === 'OK') showMsg('✅ Настройки сохранены','ok');
      else                     showMsg('❌ Ошибка: ' + ans,'err');
    }).catch(() => showMsg('❌ Ошибка связи с ESP','err'));
}
function loadSettings() {
  fetch('/api/com').then(r => r.json()).then(d => {
    document.getElementById('dev2').value      = d.dev2;
    document.getElementById('databits').value  = d.databits;
    document.getElementById('parity').value    = d.parity;
    document.getElementById('stopbits').value  = d.stopbits;
    document.getElementById('handshake').value = d.handshake;
    const preset = document.getElementById('baud_preset');
    let found = false;
    for (let i = 0; i < preset.options.length; i++) {
      if (preset.options[i].value == d.baud && preset.options[i].value !== 'custom') {
        preset.value = d.baud; found = true; break;
      }
    }
    if (!found) {
      preset.value = 'custom';
      document.getElementById('baud_custom').value = d.baud;
      document.getElementById('baud_custom').classList.remove('hidden');
    }
  });
}
loadSettings();
</script>
</body>
</html>
)HTML";

/* ============================================================
 *  HTML — ETHERNET
 * ============================================================ */
const char PAGE_ETHERNET[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="ru">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>MOXA — Ethernet</title>
%CSS%
</head>
<body class="theme-%THEME%">
  <div class="topbar"><h1>Ethernet</h1><a href="/" class="nav-btn home">🏠 На главную</a></div>

  <fieldset>
    <legend>Интерфейс</legend>
    <label>Тип</label>
    <select id="dev2">
      <option value="2">UART</option>
      <option value="3">RS-232</option>
      <option value="5">RS-422</option>
      <option value="6">RS-485 4W</option>
      <option value="4">RS-485 2W</option>
      <option value="0">COM</option>
    </select>
  </fieldset>

  <fieldset>
    <legend>Параметры UART</legend>
    <label>Baud rate</label>
    <select id="baud_preset" onchange="onBaudPreset()">
      <option value="300">300</option><option value="1200">1200</option>
      <option value="2400">2400</option><option value="4800">4800</option>
      <option value="9600">9600</option><option value="19200">19200</option>
      <option value="38400">38400</option><option value="57600">57600</option>
      <option value="115200">115200</option><option value="230400">230400</option>
      <option value="460800">460800</option><option value="921600">921600</option>
      <option value="custom">Своя...</option>
    </select>
    <input id="baud_custom" type="number" class="hidden" style="margin-top:6px;">
    <label>Data bits</label>
    <select id="databits">
      <option value="5">5</option><option value="6">6</option>
      <option value="7">7</option><option value="8" selected>8</option>
      <option value="9">9</option>
    </select>
    <label>Parity</label>
    <select id="parity">
      <option value="0">None</option><option value="1">Even</option><option value="2">Odd</option>
    </select>
    <label>Stop bits</label>
    <select id="stopbits"><option value="1">1</option><option value="2">2</option></select>
    <label>Handshaking</label>
    <select id="handshake">
      <option value="0">None</option><option value="1">RTS/CTS</option><option value="2">XON/XOFF</option>
    </select>
  </fieldset>

  <fieldset>
    <legend>Сетевые настройки</legend>
    <label>TCP-порт</label><input id="port" type="number" value="5000">
    <label>MAC</label><input id="mac" type="text" value="00:08:dc:ab:cd:ef">
    <label>IP</label><input id="ip" type="text" value="192.168.100.200">
    <label>SN</label><input id="sn" type="text" value="255.255.255.0">
    <label>GW</label><input id="gw" type="text" value="192.168.100.1">
    <label>DNS</label><input id="dns" type="text" value="8.8.8.8">
    <label>DHCP</label>
    <select id="dhcp" disabled><option value="0" selected>Static</option></select>
  </fieldset>

  <button onclick="saveSettings()">💾 Отправить настройки</button>
  <div id="msg" class="msg hidden"></div>

<script>
function showMsg(txt, cls) {
  const m = document.getElementById('msg');
  m.textContent = txt; m.className = 'msg ' + cls; m.classList.remove('hidden');
  setTimeout(() => m.classList.add('hidden'), 4000);
}
function onBaudPreset() {
  const v = document.getElementById('baud_preset').value;
  document.getElementById('baud_custom').classList.toggle('hidden', v !== 'custom');
}
function getBaud() {
  const p = document.getElementById('baud_preset').value;
  return (p === 'custom') ? document.getElementById('baud_custom').value : p;
}
function saveSettings() {
  const data = {
    dev2: document.getElementById('dev2').value,
    baud: getBaud(),
    databits: document.getElementById('databits').value,
    parity: document.getElementById('parity').value,
    stopbits: document.getElementById('stopbits').value,
    handshake: document.getElementById('handshake').value,
    port: document.getElementById('port').value,
    mac: document.getElementById('mac').value,
    ip: document.getElementById('ip').value,
    sn: document.getElementById('sn').value,
    gw: document.getElementById('gw').value,
    dns: document.getElementById('dns').value
  };
  fetch('/api/ethernet', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify(data)})
    .then(r => r.text()).then(ans => {
      if (ans.trim() === 'OK') showMsg('✅ Настройки сохранены','ok');
      else                     showMsg('❌ Ошибка: ' + ans,'err');
    }).catch(() => showMsg('❌ Ошибка связи с ESP','err'));
}
function loadSettings() {
  fetch('/api/ethernet').then(r => r.json()).then(d => {
    document.getElementById('dev2').value      = d.dev2;
    document.getElementById('databits').value  = d.databits;
    document.getElementById('parity').value    = d.parity;
    document.getElementById('stopbits').value  = d.stopbits;
    document.getElementById('handshake').value = d.handshake;
    document.getElementById('port').value      = d.port;
    document.getElementById('mac').value       = d.mac;
    document.getElementById('ip').value        = d.ip;
    document.getElementById('sn').value        = d.sn;
    document.getElementById('gw').value        = d.gw;
    document.getElementById('dns').value       = d.dns;
    const preset = document.getElementById('baud_preset');
    let found = false;
    for (let i = 0; i < preset.options.length; i++) {
      if (preset.options[i].value == d.baud && preset.options[i].value !== 'custom') {
        preset.value = d.baud; found = true; break;
      }
    }
    if (!found) {
      preset.value = 'custom';
      document.getElementById('baud_custom').value = d.baud;
      document.getElementById('baud_custom').classList.remove('hidden');
    }
  });
}
loadSettings();
</script>
</body>
</html>
)HTML";

/* ============================================================
 *  HTML — XYrminal
 * ============================================================ */
const char PAGE_TERMINAL[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="ru">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>MOXA — XYrminal</title>
%CSS%
</head>
<body class="theme-%THEME%">
  <div class="topbar"><h1>XYrminal</h1><a href="/" class="nav-btn home">🏠 На главную</a></div>

  <div id="term" class="term"></div>

  <div style="display:flex; gap:8px; margin-top:8px;">
    <button class="gray" onclick="clearTerm()" style="flex:1;">🗑️ Очистить</button>
    <button class="gray" onclick="toggleAutoScroll()" id="btnScroll" style="flex:1;">⏸ Пауза</button>
  </div>

<script>
let autoScroll = true;
let lastIdx = 0;
const term = document.getElementById('term');

function toggleAutoScroll() {
  autoScroll = !autoScroll;
  document.getElementById('btnScroll').textContent = autoScroll ? '⏸ Пауза' : '▶ Автоскролл';
}
function clearTerm() { term.innerHTML = ''; }

function prefixClass(p) {
  if (p === 0) return 'pfx-tcp';
  if (p === 1) return 'pfx-rx';
  if (p === 2) return 'pfx-com';
  return 'txt';
}
function prefixText(p) {
  if (p === 0) return 'TCP:';
  if (p === 1) return 'RX:';
  if (p === 2) return 'COM:';
  return '';
}

function poll() {
  fetch('/api/term').then(r => r.json()).then(d => {
    if (d.reset) { term.innerHTML = ''; lastIdx = 0; }
    if (!d.lines || d.lines.length === 0) return;
    for (const ln of d.lines) {
      const div = document.createElement('div');
      div.className = 'ln';
      const sP = document.createElement('span');
      sP.className = prefixClass(ln.prefix);
      sP.textContent = prefixText(ln.prefix);
      const sT = document.createElement('span');
      sT.className = 'txt';
      sT.textContent = ln.text;
      div.appendChild(sP);
      div.appendChild(sT);
      term.appendChild(div);
    }
    if (autoScroll) term.scrollTop = term.scrollHeight;
  }).catch(() => {});
}
setInterval(poll, 500);
</script>
</body>
</html>
)HTML";

/* ============================================================
 *  HTML — НАСТРОЙКИ
 * ============================================================ */
const char PAGE_SETTINGS[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="ru">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>MOXA — Настройки</title>
%CSS%
</head>
<body class="theme-%THEME%">
  <div class="topbar"><h1>Настройки</h1><a href="/" class="nav-btn home">🏠 На главную</a></div>

  <fieldset>
    <legend>Тема</legend>
    <div class="theme-row">
      <div class="theme-btn t0" data-theme="0" onclick="setTheme(0)">Серая</div>
      <div class="theme-btn t1" data-theme="1" onclick="setTheme(1)">Фиолетовая</div>
      <div class="theme-btn t2" data-theme="2" onclick="setTheme(2)">Чёрная</div>
    </div>
    <div id="msgTheme" class="msg hidden"></div>
  </fieldset>

  <fieldset>
    <legend>Пароль от сети ESP</legend>
    <div class="row">
      <input id="ap_pass" type="text" placeholder="минимум 8 символов">
      <button onclick="changePassword()">Изменить</button>
    </div>
    <div class="hint">SSID: XYoxa (не меняется)</div>
    <div id="msgPass" class="msg hidden"></div>
  </fieldset>

  <fieldset>
    <legend>IP адрес ESP</legend>
    <div class="row">
      <input id="ap_ip" type="text" placeholder="192.168.4.1">
      <button onclick="changeIP()">Изменить</button>
    </div>
    <div class="hint">Текущий IP: <span id="cur_ip"></span></div>
    <div id="msgIP" class="msg hidden"></div>
  </fieldset>

  <div class="footer">
    <div><b>%DEVNAME%</b></div>
    <div>Версия прошивки: %VERSION%</div>
    <div>Автор: %AUTHOR%</div>
  </div>

<script>
function showMsg(id, txt, cls) {
  const m = document.getElementById(id);
  m.textContent = txt; m.className = 'msg ' + cls; m.classList.remove('hidden');
  setTimeout(() => m.classList.add('hidden'), 4000);
}
function applyThemeClass(t) {
  document.body.className = 'theme-' + t;
  document.querySelectorAll('.theme-btn').forEach(b => b.classList.toggle('active', b.dataset.theme == t));
}
function setTheme(t) {
  applyThemeClass(t);
  fetch('/api/settings', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({theme:t})})
    .then(r => r.text()).then(ans => {
      if (ans.trim() === 'OK') showMsg('msgTheme','✅ Тема сохранена','ok');
      else                     showMsg('msgTheme','❌ Ошибка: ' + ans,'err');
    }).catch(() => showMsg('msgTheme','❌ Ошибка связи с ESP','err'));
}
function changePassword() {
  const p = document.getElementById('ap_pass').value;
  if (p.length < 8) { showMsg('msgPass','❌ минимум 8 символов','err'); return; }
  fetch('/api/settings', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({ap_password:p})})
    .then(r => r.text()).then(ans => {
      if (ans.trim() === 'OK') showMsg('msgPass','✅ Пароль изменён. Перезагрузка...','ok');
      else                     showMsg('msgPass','❌ Ошибка: ' + ans,'err');
    }).catch(() => showMsg('msgPass','❌ Ошибка связи с ESP','err'));
}
function changeIP() {
  const ip = document.getElementById('ap_ip').value.trim();
  if (!/^\d+\.\d+\.\d+\.\d+$/.test(ip)) { showMsg('msgIP','❌ Неверный формат','err'); return; }
  fetch('/api/settings', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({ap_ip:ip})})
    .then(r => r.text()).then(ans => {
      if (ans.trim() === 'OK') showMsg('msgIP','✅ IP установлен. Перезагрузка...','ok');
      else                     showMsg('msgIP','❌ Ошибка: ' + ans,'err');
    }).catch(() => showMsg('msgIP','❌ Ошибка связи с ESP','err'));
}
function loadSettings() {
  fetch('/api/settings').then(r => r.json()).then(d => {
    applyThemeClass(d.theme);
    document.getElementById('ap_pass').value = d.ap_password;
    document.getElementById('ap_ip').value   = d.ap_ip;
    document.getElementById('cur_ip').textContent = d.ap_ip;
  });
}
loadSettings();
</script>
</body>
</html>
)HTML";

/* ============================================================
 *  СБОРКА HTML
 * ============================================================ */
String build_page(const char* tpl) {
    String html = FPSTR(tpl);
    html.replace("%CSS%",     FPSTR(COMMON_CSS));
    html.replace("%DEVNAME%", DEVICE_NAME);
    html.replace("%VERSION%", DEVICE_VERSION);
    html.replace("%AUTHOR%",  DEVICE_AUTHOR);
    html.replace("%THEME%",   String(settings.theme));
    return html;
}

/* ============================================================
 *  HTTP — СТРАНИЦЫ
 * ============================================================ */
void handleRoot()         { server.send(200, "text/html; charset=utf-8", build_page(PAGE_INDEX));    }
void handleComPage()      { server.send(200, "text/html; charset=utf-8", build_page(PAGE_COM));      }
void handleEthernetPage() { server.send(200, "text/html; charset=utf-8", build_page(PAGE_ETHERNET)); }
void handleTerminalPage() { server.send(200, "text/html; charset=utf-8", build_page(PAGE_TERMINAL)); }
void handleSettingsPage() { server.send(200, "text/html; charset=utf-8", build_page(PAGE_SETTINGS)); }

/* ============================================================
 *  HTTP API — PING / TERM
 * ============================================================ */
void handleApiPing() {
    bool ok = stm_ping(800);
    server.send(200, "text/plain", ok ? "OK" : "DOWN");
}

/* Отдаём НОВЫЕ строки терминала от lastIdx, потом lastIdx = count */
int term_last_sent = 0;

void handleApiTerm() {
    String out = "{\"reset\":false,\"lines\":[";
    int start = term_last_sent;
    for (int i = start; i < term_line_count; i++) {
        if (i > start) out += ",";
        out += "{\"prefix\":" + String(term_lines[i].prefix) + ",";
        out += "\"text\":\"";
        /* Экранируем кавычки и бэкслеши */
        for (size_t j = 0; j < term_lines[i].text.length(); j++) {
            char c = term_lines[i].text[j];
            if (c == '\\') out += "\\\\";
            else if (c == '"') out += "\\\"";
            else if (c == '\n') out += "\\n";
            else if (c == '\r') out += "\\r";
            else out += c;
        }
        out += "\"}";
    }
    out += "]}";
    term_last_sent = term_line_count;
    server.send(200, "application/json", out);
}

/* ============================================================
 *  JSON-ПАРСЕР
 * ============================================================ */
bool json_extract(const String &body, const String &key, String &out) {
    int i = body.indexOf("\"" + key + "\"");
    if (i < 0) return false;
    i = body.indexOf(':', i);
    if (i < 0) return false;
    i++;
    while (i < (int)body.length() && (body[i] == ' ' || body[i] == '"')) i++;
    int j = i;
    while (j < (int)body.length() && body[j] != ',' && body[j] != '}' && body[j] != '"') j++;
    out = body.substring(i, j);
    out.trim();
    return true;
}

/* ============================================================
 *  КОМАНДЫ
 * ============================================================ */
String build_cmd_com() {
    String cmd = "CMD=SET";
    cmd += ",DEV1=0";
    cmd += ",DEV2="      + String(settings.dev2);
    cmd += ",BAUD="      + String(settings.baudrate);
    cmd += ",DATABITS="  + String(settings.databits);
    cmd += ",PARITY="    + String(settings.parity);
    cmd += ",STOPBITS="  + String(settings.stopbits);
    cmd += ",HANDSHAKE=" + String(settings.handshake);
    return cmd;
}

String build_cmd_ethernet() {
    String cmd = "CMD=SET";
    cmd += ",DEV1=1";
    cmd += ",DEV2="      + String(settings.dev2);
    cmd += ",BAUD="      + String(settings.baudrate);
    cmd += ",DATABITS="  + String(settings.databits);
    cmd += ",PARITY="    + String(settings.parity);
    cmd += ",STOPBITS="  + String(settings.stopbits);
    cmd += ",HANDSHAKE=" + String(settings.handshake);
    cmd += ",PORT="      + String(settings.port);
    cmd += ",MAC="       + mac_to_str(settings.mac);
    cmd += ",IP="        + ip_to_str(settings.ip);
    cmd += ",SN="        + ip_to_str(settings.sn);
    cmd += ",GW="        + ip_to_str(settings.gw);
    cmd += ",DNS="       + ip_to_str(settings.dns);
    cmd += ",DHCP="      + String(settings.dhcp);
    return cmd;
}

/* ============================================================
 *  HTTP API — COM / ETHERNET
 * ============================================================ */
void handleApiComGet() {
    String json = "{";
    json += "\"dev2\":"      + String(settings.dev2) + ",";
    json += "\"baud\":"      + String(settings.baudrate) + ",";
    json += "\"databits\":"  + String(settings.databits) + ",";
    json += "\"parity\":"    + String(settings.parity) + ",";
    json += "\"stopbits\":"  + String(settings.stopbits) + ",";
    json += "\"handshake\":" + String(settings.handshake);
    json += "}";
    server.send(200, "application/json", json);
}

void handleApiComPost() {
    if (!server.hasArg("plain")) { server.send(400, "text/plain", "no body"); return; }
    String body = server.arg("plain");
    String v;
    if (json_extract(body, "dev2", v))      settings.dev2      = v.toInt();
    if (json_extract(body, "baud", v))      settings.baudrate  = strtoul(v.c_str(), NULL, 10);
    if (json_extract(body, "databits", v))  settings.databits  = v.toInt();
    if (json_extract(body, "parity", v))    settings.parity    = v.toInt();
    if (json_extract(body, "stopbits", v))  settings.stopbits  = v.toInt();
    if (json_extract(body, "handshake", v)) settings.handshake = v.toInt();
    settings.dev1 = 0;
    settings_save();
    String resp = stm_command(build_cmd_com(), 2000);
    server.send(200, "text/plain", resp.length() ? resp : "TIMEOUT");
}

void handleApiEthernetGet() {
    String json = "{";
    json += "\"dev2\":"      + String(settings.dev2) + ",";
    json += "\"baud\":"      + String(settings.baudrate) + ",";
    json += "\"databits\":"  + String(settings.databits) + ",";
    json += "\"parity\":"    + String(settings.parity) + ",";
    json += "\"stopbits\":"  + String(settings.stopbits) + ",";
    json += "\"handshake\":" + String(settings.handshake) + ",";
    json += "\"port\":"      + String(settings.port) + ",";
    json += "\"mac\":\""     + mac_to_str(settings.mac) + "\",";
    json += "\"ip\":\""      + ip_to_str(settings.ip) + "\",";
    json += "\"sn\":\""      + ip_to_str(settings.sn) + "\",";
    json += "\"gw\":\""      + ip_to_str(settings.gw) + "\",";
    json += "\"dns\":\""     + ip_to_str(settings.dns) + "\",";
    json += "\"dhcp\":"      + String(settings.dhcp);
    json += "}";
    server.send(200, "application/json", json);
}

void handleApiEthernetPost() {
    if (!server.hasArg("plain")) { server.send(400, "text/plain", "no body"); return; }
    String body = server.arg("plain");
    String v;
    if (json_extract(body, "dev2", v))      settings.dev2      = v.toInt();
    if (json_extract(body, "baud", v))      settings.baudrate  = strtoul(v.c_str(), NULL, 10);
    if (json_extract(body, "databits", v))  settings.databits  = v.toInt();
    if (json_extract(body, "parity", v))    settings.parity    = v.toInt();
    if (json_extract(body, "stopbits", v))  settings.stopbits  = v.toInt();
    if (json_extract(body, "handshake", v)) settings.handshake = v.toInt();
    if (json_extract(body, "port", v))      settings.port      = v.toInt();
    if (json_extract(body, "mac", v)) { uint8_t t[6]; if (str_to_mac(v,t)) memcpy(settings.mac,t,6); }
    if (json_extract(body, "ip", v))  { uint8_t t[4]; if (str_to_ip (v,t)) memcpy(settings.ip, t,4); }
    if (json_extract(body, "sn", v))  { uint8_t t[4]; if (str_to_ip (v,t)) memcpy(settings.sn, t,4); }
    if (json_extract(body, "gw", v))  { uint8_t t[4]; if (str_to_ip (v,t)) memcpy(settings.gw, t,4); }
    if (json_extract(body, "dns", v)) { uint8_t t[4]; if (str_to_ip (v,t)) memcpy(settings.dns,t,4); }
    settings.dhcp = 0;
    settings.dev1 = 1;
    settings_save();
    String resp = stm_command(build_cmd_ethernet(), 2000);
    server.send(200, "text/plain", resp.length() ? resp : "TIMEOUT");
}

/* ============================================================
 *  HTTP API — SETTINGS
 * ============================================================ */
void handleApiSettingsGet() {
    String json = "{";
    json += "\"theme\":"       + String(settings.theme) + ",";
    json += "\"ap_password\":\"" + String(settings.ap_password) + "\",";
    json += "\"ap_ip\":\""     + ip_to_str(settings.ap_ip) + "\"";
    json += "}";
    server.send(200, "application/json", json);
}

void handleApiSettingsPost() {
    if (!server.hasArg("plain")) { server.send(400, "text/plain", "no body"); return; }
    String body = server.arg("plain");
    String v;
    bool need_restart = false;

    if (json_extract(body, "theme", v)) {
        settings.theme = v.toInt();
        if (settings.theme > 2) settings.theme = 0;
    }
    if (json_extract(body, "ap_password", v)) {
        if (v.length() < 8) { server.send(200, "text/plain", "PASS_TOO_SHORT"); return; }
        strncpy(settings.ap_password, v.c_str(), sizeof(settings.ap_password) - 1);
        settings.ap_password[sizeof(settings.ap_password) - 1] = '\0';
        need_restart = true;
    }
    if (json_extract(body, "ap_ip", v)) {
        uint8_t t[4];
        if (!str_to_ip(v, t)) { server.send(200, "text/plain", "IP_BAD_FORMAT"); return; }
        memcpy(settings.ap_ip, t, 4);
        need_restart = true;
    }
    settings_save();

    if (need_restart) {
        server.send(200, "text/plain", "OK");
        delay(2000);
        ESP.restart();
    } else {
        server.send(200, "text/plain", "OK");
    }
}

/* ============================================================
 *  SETUP
 * ============================================================ */
void setup() {
    Serial.begin(UART_BAUD_DEFAULT);
    Serial.setRxBufferSize(UART_RX_BUF);
    Serial.println();

    settings_load();

    IPAddress ap_ip(settings.ap_ip[0], settings.ap_ip[1],
                    settings.ap_ip[2], settings.ap_ip[3]);
    IPAddress ap_gw = ap_ip;
    IPAddress ap_sn(255, 255, 255, 0);

    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(ap_ip, ap_gw, ap_sn);
    WiFi.softAP(AP_SSID, settings.ap_password);

    server.on("/",          handleRoot);
    server.on("/com",       handleComPage);
    server.on("/ethernet",  handleEthernetPage);
    server.on("/terminal",  handleTerminalPage);
    server.on("/settings",  handleSettingsPage);

    server.on("/api/ping",     HTTP_GET,  handleApiPing);
    server.on("/api/com",      HTTP_GET,  handleApiComGet);
    server.on("/api/com",      HTTP_POST, handleApiComPost);
    server.on("/api/ethernet", HTTP_GET,  handleApiEthernetGet);
    server.on("/api/ethernet", HTTP_POST, handleApiEthernetPost);
    server.on("/api/settings", HTTP_GET,  handleApiSettingsGet);
    server.on("/api/settings", HTTP_POST, handleApiSettingsPost);
    server.on("/api/term",     HTTP_GET,  handleApiTerm);

    server.begin();

    delay(500);
    stm_ping(800);

    delay(2000);
    String cmd = (settings.dev1 == 0) ? build_cmd_com() : build_cmd_ethernet();
    stm_command(cmd, 2000);
}

/* ============================================================
 *  LOOP
 * ============================================================ */
void loop() {
    server.handleClient();

    /* Приём от STM32 → парсер префиксов → term_lines */
    static uint8_t rxbuf[256];
    int n = 0;
    while (Serial.available() && n < (int)sizeof(rxbuf)) {
        rxbuf[n++] = (uint8_t)Serial.read();
    }
    if (n > 0) {
        term_feed(rxbuf, (uint16_t)n);
    }
}