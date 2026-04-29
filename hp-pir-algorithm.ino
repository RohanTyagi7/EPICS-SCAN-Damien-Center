#include <stdexcept>
#include <queue>
#include <esp_sleep.h>
#include <WiFi.h>
#include <WebServer.h>
#include "MyLD2410.h"

// =====================================================================
// WIFI CONFIGURATION — change these to someone's network credentials
// =====================================================================
const char* WIFI_SSID     = "eduroam";
const char* EAP_IDENTITY  = "tyagi50@purdue.edu"; // purdue email
const char* EAP_USERNAME  = "tyagi50@purdue.edu"; // purdue email
const char* EAP_PASSWORD  = ""; // purdue password (DO NOT PUSH WITH A PASSWORD; you could use a github secret)
// =====================================================================

#if defined(ARDUINO_SAMD_NANO_33_IOT) || defined(ARDUINO_AVR_LEONARDO)
  #define sensorSerial Serial1
#elif defined(ARDUINO_XIAO_ESP32C3) || defined(ARDUINO_XIAO_ESP32C6)
  #define sensorSerial Serial0
#elif defined(ESP32)
  #define sensorSerial Serial1
  #if defined(ARDUINO_ESP32S3_DEV)
    #define RX_PIN 18
    #define TX_PIN 17
  #else
    #define RX_PIN 16
    #define TX_PIN 17
  #endif
#else
  #error "This sketch only works on ESP32, Arduino Nano 33IoT, and Arduino Leonardo (Pro-Micro)"
#endif

// #define SENSOR_DEBUG
#define ENHANCED_MODE
#define SERIAL_BAUD_RATE 115200

#ifdef SENSOR_DEBUG
  MyLD2410 sensor(sensorSerial, true);
#else
  MyLD2410 sensor(sensorSerial);
#endif

#define PIR_PIN       GPIO_NUM_14
#define LED_PIN       GPIO_NUM_13
#define MOSFET_PIN    GPIO_NUM_27
#define BUTTON_PIN    GPIO_NUM_18

#define WAITING  1
#define WATCHING 2

#define DISTANCE_SAMPLE_DELAY   1000
#define MOTION_TIMEOUT          90000
#define ROOM_EMPTY_TIMEOUT      5000
#define MOTION_VAL_THRESHOLD    100
#define MAX_DISTANCE_DATA_POINTS 10

// #define LOOP_DEBUG

unsigned long power_on_time;
unsigned long loop_start_time;
unsigned long loop_end_time;
unsigned long dt;
unsigned long distance_measurement_timer = 0;

unsigned long room_empty_time = 0;
unsigned long motion_time     = 0;
unsigned short system_state   = WAITING;
bool is_emergency_occurring   = false;

std::queue<unsigned long> distance_data;

WebServer server(80);

struct Snapshot {
  unsigned long uptime_ms     = 0;
  unsigned long motion_time   = 0;
  unsigned long room_empty    = 0;
  int           distance_raw  = -1;   // -1 = no presence
  unsigned long distance_est  = 0;
  int           motion_raw    = 0;
  int           motion_scaled = 0;
  bool          presence      = false;
  bool          moving        = false;
  bool          emergency     = false;
  int           state         = WAITING;
} snap;

// HTML dashboard
const char* DASHBOARD_HTML = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Presence Monitor</title>
<style>
  @import url('https://fonts.googleapis.com/css2?family=Share+Tech+Mono&family=Rajdhani:wght@300;500;700&display=swap');

  :root {
    --bg:       #080c10;
    --panel:    #0d1520;
    --border:   #1a2d42;
    --accent:   #00d4ff;
    --accent2:  #ff6b35;
    --ok:       #00ff88;
    --warn:     #ffcc00;
    --danger:   #ff2244;
    --text:     #c8dff0;
    --dim:      #4a6a88;
  }

  *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }

  body {
    background: var(--bg);
    color: var(--text);
    font-family: 'Rajdhani', sans-serif;
    font-weight: 300;
    min-height: 100vh;
    display: flex;
    flex-direction: column;
    align-items: center;
    padding: 24px 16px 40px;
  }

  /* scanline overlay */
  body::before {
    content: '';
    position: fixed; inset: 0; z-index: 999; pointer-events: none;
    background: repeating-linear-gradient(
      0deg,
      transparent,
      transparent 2px,
      rgba(0,0,0,.08) 2px,
      rgba(0,0,0,.08) 4px
    );
  }

  header {
    width: 100%; max-width: 900px;
    display: flex; align-items: center; justify-content: space-between;
    border-bottom: 1px solid var(--border);
    padding-bottom: 14px; margin-bottom: 28px;
  }

  .logo {
    font-family: 'Share Tech Mono', monospace;
    font-size: 1.1rem;
    color: var(--accent);
    letter-spacing: .15em;
    text-transform: uppercase;
  }

  .logo span { color: var(--dim); }

  #status-pill {
    font-family: 'Share Tech Mono', monospace;
    font-size: .75rem;
    letter-spacing: .1em;
    padding: 4px 14px;
    border-radius: 20px;
    border: 1px solid var(--border);
    background: var(--panel);
    transition: all .3s;
  }

  /* ── grid ── */
  .grid {
    width: 100%; max-width: 900px;
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(240px, 1fr));
    gap: 16px;
  }

  .card {
    background: var(--panel);
    border: 1px solid var(--border);
    border-radius: 4px;
    padding: 20px 22px;
    position: relative;
    overflow: hidden;
    transition: border-color .3s;
  }

  .card::after {
    content: '';
    position: absolute; top: 0; left: 0; right: 0;
    height: 2px;
    background: var(--accent);
    transform: scaleX(0);
    transform-origin: left;
    transition: transform .4s ease;
  }

  .card.active::after { transform: scaleX(1); }

  .card-label {
    font-size: .65rem;
    letter-spacing: .2em;
    text-transform: uppercase;
    color: var(--dim);
    margin-bottom: 10px;
    font-family: 'Share Tech Mono', monospace;
  }

  .card-value {
    font-family: 'Share Tech Mono', monospace;
    font-size: 2.2rem;
    line-height: 1;
    color: var(--accent);
    transition: color .3s;
  }

  .card-unit {
    font-size: .7rem;
    color: var(--dim);
    margin-top: 6px;
    font-family: 'Share Tech Mono', monospace;
  }

  /* state badge */
  .badge {
    display: inline-block;
    padding: 2px 10px;
    border-radius: 3px;
    font-size: .8rem;
    font-family: 'Share Tech Mono', monospace;
    letter-spacing: .08em;
    border: 1px solid;
  }

  /* bar gauge */
  .bar-wrap {
    margin-top: 12px;
    height: 6px;
    background: var(--border);
    border-radius: 3px;
    overflow: hidden;
  }
  .bar-fill {
    height: 100%;
    border-radius: 3px;
    background: var(--accent);
    transition: width .5s ease, background .3s;
    width: 0%;
  }

  /* emergency banner */
  #emergency-banner {
    display: none;
    width: 100%; max-width: 900px;
    background: var(--danger);
    color: #fff;
    font-family: 'Share Tech Mono', monospace;
    font-size: .9rem;
    letter-spacing: .15em;
    text-align: center;
    padding: 10px;
    border-radius: 4px;
    margin-bottom: 18px;
    animation: blink 1s step-start infinite;
  }

  @keyframes blink { 50% { opacity: .4; } }

  /* timeline strip */
  #timeline {
    width: 100%; max-width: 900px;
    margin-top: 20px;
    background: var(--panel);
    border: 1px solid var(--border);
    border-radius: 4px;
    padding: 16px 20px;
  }

  #timeline h3 {
    font-size: .65rem;
    letter-spacing: .2em;
    text-transform: uppercase;
    color: var(--dim);
    font-family: 'Share Tech Mono', monospace;
    margin-bottom: 12px;
  }

  #event-list {
    list-style: none;
    max-height: 140px;
    overflow-y: auto;
  }

  #event-list li {
    font-family: 'Share Tech Mono', monospace;
    font-size: .72rem;
    color: var(--text);
    padding: 3px 0;
    border-bottom: 1px solid var(--border);
    display: flex;
    gap: 12px;
  }

  #event-list li .ts { color: var(--dim); min-width: 80px; }

  footer {
    margin-top: 32px;
    font-family: 'Share Tech Mono', monospace;
    font-size: .65rem;
    color: var(--dim);
    letter-spacing: .1em;
  }

  #conn-dot {
    display: inline-block;
    width: 8px; height: 8px;
    border-radius: 50%;
    background: var(--danger);
    margin-right: 6px;
    vertical-align: middle;
    transition: background .3s;
  }

  #conn-dot.on { background: var(--ok); box-shadow: 0 0 6px var(--ok); }
</style>
</head>
<body>

<header>
  <div class="logo">PRESENCE<span>/</span>MONITOR</div>
  <div id="status-pill"><span id="conn-dot"></span><span id="conn-txt">CONNECTING…</span></div>
</header>

<div id="emergency-banner">⚠ EMERGENCY TRIGGERED ⚠</div>

<div class="grid">

  <div class="card" id="card-state">
    <div class="card-label">System State</div>
    <div class="card-value" id="val-state">—</div>
  </div>

  <div class="card" id="card-presence">
    <div class="card-label">Presence</div>
    <div class="card-value" id="val-presence">—</div>
  </div>

  <div class="card" id="card-dist-raw">
    <div class="card-label">Sensor Distance</div>
    <div class="card-value" id="val-dist-raw">—</div>
    <div class="card-unit">cm</div>
  </div>

  <div class="card" id="card-dist-est">
    <div class="card-label">Estimated Distance</div>
    <div class="card-value" id="val-dist-est">—</div>
    <div class="card-unit">cm (min of last 10)</div>
  </div>

  <div class="card" id="card-motion-raw">
    <div class="card-label">Motion Signal (raw)</div>
    <div class="card-value" id="val-motion-raw">—</div>
    <div class="bar-wrap"><div class="bar-fill" id="bar-motion-raw"></div></div>
  </div>

  <div class="card" id="card-motion-scaled">
    <div class="card-label">Motion Signal (scaled)</div>
    <div class="card-value" id="val-motion-scaled">—</div>
    <div class="bar-wrap"><div class="bar-fill" id="bar-motion-scaled"></div></div>
  </div>

  <div class="card" id="card-motion-time">
    <div class="card-label">No-Motion Timer</div>
    <div class="card-value" id="val-motion-time">—</div>
    <div class="card-unit">ms (timeout: 90000)</div>
    <div class="bar-wrap"><div class="bar-fill" id="bar-motion-time"></div></div>
  </div>

  <div class="card" id="card-empty-time">
    <div class="card-label">Room Empty Timer</div>
    <div class="card-value" id="val-empty-time">—</div>
    <div class="card-unit">ms (timeout: 5000)</div>
    <div class="bar-wrap"><div class="bar-fill" id="bar-empty-time"></div></div>
  </div>

  <div class="card" id="card-uptime">
    <div class="card-label">Uptime</div>
    <div class="card-value" id="val-uptime">—</div>
    <div class="card-unit">ms</div>
  </div>

</div>

<div id="timeline">
  <h3>Event Log</h3>
  <ul id="event-list"></ul>
</div>

<footer>polling every 1 s · ESP32 onboard server</footer>

<script>
const $ = id => document.getElementById(id);
const events = [];

let prevSnap = {};

function fmtMs(ms) {
  const s = Math.floor(ms / 1000);
  const m = Math.floor(s / 60);
  if (m > 0) return m + 'm ' + (s % 60) + 's';
  return s + 's (' + ms + 'ms)';
}

function pct(val, max) { return Math.min(100, (val / max) * 100).toFixed(1) + '%'; }

function setBar(id, val, max, dangerPct) {
  const el = $('bar-' + id);
  const p = Math.min(100, (val / max) * 100);
  el.style.width = p + '%';
  el.style.background = p >= dangerPct ? 'var(--danger)' : p >= dangerPct * .6 ? 'var(--warn)' : 'var(--accent)';
}

function addEvent(ts, msg, color) {
  events.unshift({ ts, msg, color });
  if (events.length > 40) events.pop();
  const list = $('event-list');
  list.innerHTML = events.map(e =>
    `<li><span class="ts">${fmtMs(e.ts)}</span><span style="color:${e.color}">${e.msg}</span></li>`
  ).join('');
}

function detectChanges(d) {
  const p = prevSnap;
  if (p.state !== undefined && p.state !== d.state) {
    addEvent(d.uptime_ms, 'State → ' + (d.state === 1 ? 'WAITING' : 'WATCHING'),
      d.state === 2 ? 'var(--ok)' : 'var(--dim)');
  }
  if (p.emergency !== undefined && !p.emergency && d.emergency) {
    addEvent(d.uptime_ms, '⚠ EMERGENCY triggered', 'var(--danger)');
  }
  if (p.emergency !== undefined && p.emergency && !d.emergency) {
    addEvent(d.uptime_ms, 'Emergency cleared', 'var(--ok)');
  }
  if (p.presence !== undefined && p.presence !== d.presence) {
    addEvent(d.uptime_ms, 'Presence → ' + (d.presence ? 'DETECTED' : 'NONE'),
      d.presence ? 'var(--accent)' : 'var(--dim)');
  }
  prevSnap = { ...d };
}

async function poll() {
  try {
    const r = await fetch('/data');
    if (!r.ok) throw new Error('bad response');
    const d = await r.json();

    $('conn-dot').className = 'on';
    $('conn-txt').textContent = 'LIVE';

    detectChanges(d);

    // State
    const stateEl = $('val-state');
    if (d.state === 1) {
      stateEl.textContent = 'WAITING';
      stateEl.style.color = 'var(--dim)';
    } else {
      stateEl.textContent = 'WATCHING';
      stateEl.style.color = 'var(--ok)';
    }

    // Presence
    const presEl = $('val-presence');
    if (!d.presence) {
      presEl.textContent = 'NONE';
      presEl.style.color = 'var(--dim)';
    } else if (d.moving) {
      presEl.textContent = 'MOVING';
      presEl.style.color = 'var(--accent)';
    } else {
      presEl.textContent = 'STATIC';
      presEl.style.color = 'var(--warn)';
    }

    // Emergency banner
    $('emergency-banner').style.display = d.emergency ? 'block' : 'none';

    // Distances
    $('val-dist-raw').textContent  = d.distance_raw  >= 0 ? d.distance_raw  : '—';
    $('val-dist-est').textContent  = d.distance_est;

    // Motion bars
    $('val-motion-raw').textContent    = d.motion_raw;
    $('val-motion-scaled').textContent = d.motion_scaled;
    setBar('motion-raw',    d.motion_raw,    100, 90);
    setBar('motion-scaled', d.motion_scaled, 100, 90);

    // Timers
    $('val-motion-time').textContent = d.motion_time;
    $('val-empty-time').textContent  = d.room_empty;
    $('val-uptime').textContent      = d.uptime_ms;
    setBar('motion-time', d.motion_time, 90000, 80);
    setBar('empty-time',  d.room_empty,   5000, 80);

    // Card active glow
    ['card-presence', 'card-dist-raw', 'card-motion-raw', 'card-motion-scaled'].forEach(id => {
      document.getElementById(id).classList.toggle('active', d.presence);
    });

  } catch(e) {
    $('conn-dot').className = '';
    $('conn-txt').textContent = 'OFFLINE';
  }
}

poll();
setInterval(poll, 1000);
</script>
</body>
</html>
)rawliteral";

void init_presence_sensor();
void power_off_hp();
void power_on_hp();
void update_values(unsigned long dt);
unsigned long calc_estimated_distance();
void enter_deep_sleep();
unsigned char scale_motion(unsigned int motion_val, unsigned long estimated_distance);
void update_snapshot();
void handle_root();
void handle_data();
void init_wifi();

void init_wifi() {
  Serial.print("Connecting to Eduroam WiFi: ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true); // Clear previous settings just in case
  
  // WPA2 Enterprise connection 
  // (SSID, Authentication Method, Identity, Username, Password)
  WiFi.begin(WIFI_SSID, WPA2_AUTH_PEAP, EAP_IDENTITY, EAP_USERNAME, EAP_PASSWORD);

  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    
    // Increased timeout to 30 seconds (Enterprise auth takes longer)
    if (millis() - t > 30000) {
      Serial.println("\nWiFi connection failed. Continuing without server.");
      return;
    }
  }

  Serial.println();
  Serial.print("Connected! Dashboard → http://");
  Serial.println(WiFi.localIP());

  server.on("/",     HTTP_GET, handle_root);
  server.on("/data", HTTP_GET, handle_data);
  server.begin();
  Serial.println("HTTP server started.");
}

void handle_root() {
  server.send(200, "text/html", DASHBOARD_HTML);
}

void handle_data() {
  update_snapshot();

  char buf[256];
  snprintf(buf, sizeof(buf),
    "{\"uptime_ms\":%lu,\"state\":%d,\"presence\":%s,\"moving\":%s,"
    "\"distance_raw\":%d,\"distance_est\":%lu,"
    "\"motion_raw\":%d,\"motion_scaled\":%d,"
    "\"motion_time\":%lu,\"room_empty\":%lu,\"emergency\":%s}",
    snap.uptime_ms, snap.state,
    snap.presence  ? "true" : "false",
    snap.moving    ? "true" : "false",
    snap.distance_raw, snap.distance_est,
    snap.motion_raw, snap.motion_scaled,
    snap.motion_time, snap.room_empty,
    snap.emergency ? "true" : "false"
  );

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", buf);
}

void update_snapshot() {
  snap.uptime_ms    = millis() - power_on_time;
  snap.state        = system_state;
  snap.motion_time  = motion_time;
  snap.room_empty   = room_empty_time;
  snap.emergency    = is_emergency_occurring;
  snap.presence     = sensor.presenceDetected();
  snap.moving       = snap.presence && sensor.movingTargetDetected();
  snap.distance_raw = snap.presence ? (int)sensor.detectedDistance() : -1;
  snap.distance_est = calc_estimated_distance();
  snap.motion_raw   = (snap.presence && snap.moving) ? sensor.movingTargetSignal() : 0;
  snap.motion_scaled= scale_motion(snap.motion_raw, snap.distance_est);
}

void init_presence_sensor() {
  digitalWrite(MOSFET_PIN, HIGH);

  #if defined(ARDUINO_XIAO_ESP32C3) || defined(ARDUINO_XIAO_ESP32C6) || defined(ARDUINO_SAMD_NANO_33_IOT) || defined(ARDUINO_AVR_LEONARDO)
    sensorSerial.begin(LD2410_BAUD_RATE);
  #else
    sensorSerial.begin(LD2410_BAUD_RATE, SERIAL_8N1, RX_PIN, TX_PIN);
  #endif

  delay(300);

  while (!sensor.begin()) {
    Serial.println("Failed to communicate with the sensor.");
    delay(1200);
  }

  #ifdef ENHANCED_MODE
    sensor.enhancedMode();
  #else
    sensor.enhancedMode(false);
  #endif
}

void setup() {
  power_on_time = millis();
  Serial.begin(SERIAL_BAUD_RATE);

  pinMode(PIR_PIN,    INPUT);
  pinMode(BUTTON_PIN, INPUT);
  pinMode(LED_PIN,    OUTPUT);
  pinMode(MOSFET_PIN, OUTPUT);
  pinMode(GPIO_NUM_21, OUTPUT);
  digitalWrite(GPIO_NUM_21, HIGH);
  digitalWrite(LED_PIN, LOW);

  Serial.println(__FILE__);

  init_wifi();
  init_presence_sensor();

  distance_data.push(0);

  Serial.println("Collecting data in 5...");
  for (int i = 4; i >= 1; i--) { delay(1000); Serial.println(i); }
  delay(1000);

  loop_start_time = millis();
}

void loop() {
  server.handleClient();

  if (digitalRead(BUTTON_PIN) == HIGH) {
    is_emergency_occurring = true;
  }

  if (is_emergency_occurring) {
    digitalWrite(LED_PIN, HIGH);
    Serial.print("Emergency triggered at ");
    Serial.print(millis() - power_on_time);
    Serial.println(" ms.");

    while (digitalRead(BUTTON_PIN) == HIGH) {}
    while (digitalRead(BUTTON_PIN) == LOW)  { server.handleClient(); }
    delay(50);
    while (digitalRead(BUTTON_PIN) == HIGH) {}

    digitalWrite(LED_PIN, LOW);
    is_emergency_occurring = false;
    room_empty_time = 0;
    motion_time     = 0;
  }

  if (system_state == WAITING) {
    if (digitalRead(PIR_PIN) == HIGH) {
      system_state    = WATCHING;
      room_empty_time = 0;
      motion_time     = 0;
      power_on_hp();
      loop_start_time = millis();
      dt = 0;
    } else {
      delay(100);
    }
  }

  if (system_state == WATCHING && room_empty_time > ROOM_EMPTY_TIMEOUT) {
    system_state = WAITING;
    power_off_hp();
    enter_deep_sleep();
  } else if (system_state == WATCHING) {
    update_values(dt);
    if (motion_time > MOTION_TIMEOUT) {
      is_emergency_occurring = true;
    }
  }

  #ifdef LOOP_DEBUG
    Serial.print(millis() - power_on_time);
    Serial.print(" | ");
    switch (system_state) {
      case WAITING:  Serial.print("WAITING");  break;
      case WATCHING: Serial.print("WATCHING"); break;
    }
    Serial.print(" | "); Serial.print(motion_time);
    Serial.print(" | "); Serial.print(room_empty_time);
    Serial.print(" | ");
    sensor.presenceDetected() ? Serial.print(sensor.detectedDistance()) : Serial.print("null");
    Serial.print(" | "); Serial.print(calc_estimated_distance());
    Serial.print(" | ");
    sensor.presenceDetected() && sensor.movingTargetDetected()
      ? Serial.print(sensor.movingTargetSignal()) : Serial.print("0");
    Serial.print(" | ");
    sensor.presenceDetected() && sensor.movingTargetDetected()
      ? Serial.println(scale_motion(sensor.movingTargetSignal(), calc_estimated_distance()))
      : Serial.println("0");
  #endif

  loop_end_time   = millis();
  dt              = loop_end_time - loop_start_time;
  loop_start_time = loop_end_time;
  distance_measurement_timer += dt;
}

void power_off_hp() {
  digitalWrite(MOSFET_PIN, LOW);
  Serial.print("HP power off at "); Serial.print(millis() - power_on_time); Serial.println(" ms.");
}

void power_on_hp() {
  digitalWrite(MOSFET_PIN, HIGH);
  delay(100);
  init_presence_sensor();
  Serial.print("HP power on at "); Serial.print(millis() - power_on_time); Serial.println(" ms.");
}

void update_values(unsigned long dt) {
  unsigned char motion_val = 0;

  while (sensor.check() != MyLD2410::Response::DATA) {
    server.handleClient();
    if (digitalRead(BUTTON_PIN) == HIGH) {
      is_emergency_occurring = true;
    }
  }

  if (!sensor.presenceDetected()) {
    room_empty_time += dt;
  } else if (sensor.presenceDetected() && sensor.movingTargetDetected()) {
    motion_val = sensor.movingTargetSignal();
    motion_val = scale_motion(motion_val, calc_estimated_distance());
  } else {
    motion_val      = 0;
    room_empty_time = 0;
  }

  if (sensor.presenceDetected()) {
    room_empty_time = 0;
    if (distance_measurement_timer > DISTANCE_SAMPLE_DELAY) {
      distance_measurement_timer = 0;
      if (distance_data.size() >= MAX_DISTANCE_DATA_POINTS) distance_data.pop();
      distance_data.push(sensor.detectedDistance());
    }
  }

  if (motion_val >= MOTION_VAL_THRESHOLD) {
    motion_time = 0;
  } else {
    motion_time += dt;
  }
}

unsigned long calc_estimated_distance() {
  unsigned long min_val = (unsigned long)-1;
  std::queue<unsigned long> copy = distance_data;
  while (!copy.empty()) {
    if (copy.front() < min_val) min_val = copy.front();
    copy.pop();
  }
  return min_val;
}

void enter_deep_sleep() {
  server.stop();
  esp_sleep_wakeup_cause_t reason = esp_sleep_get_wakeup_cause();
  Serial.println("Previous wakeup reason: " + String(reason));
  esp_sleep_enable_ext0_wakeup(PIR_PIN, HIGH);
  Serial.println("Entering deep sleep…");
  esp_deep_sleep_start();
}

unsigned char scale_motion(unsigned int motion_val, unsigned long estimated_distance) {
  estimated_distance *= 0.0328f;
  float scale;
  if      (estimated_distance < 4)  scale = 1.0f;
  else if (estimated_distance < 7)  scale = 1.0f/3 * (estimated_distance - 4) + 1;
  else if (estimated_distance < 9)  scale = 1.0f/4 * (estimated_distance - 7) + 2;
  else                              scale = 2.5f;

  motion_val *= scale;
  if (motion_val > 100) motion_val = 100;
  return (unsigned char)motion_val;
}
