/*
 * MultiChannelPayload.ino — NyarkoaCanSATPayload library example
 *
 * Demonstrates filling all NYARKOA_MAX_FIELDS (8) channels and using every
 * value-type helper: D(), I(), B(), C(), S().
 *
 * Two operating modes are selected by a digital pin inside the provider:
 *   Mode A (pin LOW)  — environmental data  (temperature, pressure, altitude,
 * humidity) Mode B (pin HIGH) — motion / status data (acceleration, heading,
 * fix, mode label)
 *
 * The provider fills the PayloadField array differently per mode each time it
 * is called. The library invokes the provider automatically every intervalMs
 * milliseconds, and also on demand when the comm board sends a "GET" command.
 * No delay() is needed; the comm board handles chunk reassembly transparently.
 *
 * ── Key Codebook ─────────────────────────────────────────────────────────────
 *  Key  │ Value type │ Resolved label (comm board codebook)
 * ──────┼────────────┼──────────────────────────────────────────────────────────
 *  TP1  │ decimal    │ Temperature Sensor 1 (°C)
 *  TP2  │ decimal    │ Temperature Sensor 2 (°C)
 *  PRS  │ decimal    │ Atmospheric Pressure (Pa)
 *  ALT  │ decimal    │ Barometric Altitude (m)
 *  HUM  │ decimal    │ Relative Humidity (%)
 *  FIX  │ bool       │ GPS Fix Acquired
 *  MOD  │ char       │ Operating Mode Code ('A' = environment, 'B' = motion)
 *  STS  │ string     │ System Status Label ("OK", "WARN", "ERR")
 * ──────┴────────────┴──────────────────────────────────────────────────────────
 *
 * Hardware
 * ────────
 * MODE_PIN (pin 7) — pulled LOW for Mode A, HIGH for Mode B.
 * Sensors simulated with analogRead(); replace with real sensor calls.
 */

#include <NyarkoaCanSATPayload.h>

static const uint8_t MODE_PIN = 7;

NyarkoaCanSATPayload payload;

// ── Payload provider callback
// ───────────────────────────────────────────────── Called by the library on
// every auto-transmission tick and on every "GET". Mode is sampled fresh each
// call so a pin change takes effect immediately on the next transmission
// without any restart.
void buildPayload(PayloadField* fields, uint8_t* count) {
  bool modeB = (digitalRead(MODE_PIN) == HIGH);

  if (!modeB) {
    // ── Mode A: Environmental data ────────────────────────────────────────
    // D(v) — decimal (float) sensor readings
    // B(v) — bool flag
    // C(v) — single char mode code
    // S(v) — string status label (pointer must outlive the library's send call;
    //         string literals have indefinite lifetime — safe to use here)
    float tp1 = (analogRead(A0) / 1023.0f) * 85.0f;  // °C, 0–85 range
    float tp2 = (analogRead(A1) / 1023.0f) * 85.0f;
    float prs = 95000.0f + (analogRead(A2) / 1023.0f) * 10000.0f;  // Pa
    float alt = (analogRead(A3) / 1023.0f) * 3000.0f;              // m
    float hum = (analogRead(A4) / 1023.0f) * 100.0f;               // %
    bool fix = (analogRead(A5) > 512);

    fields[0] = {"TP1", D(tp1)};   // float  → {"TP1":23.50}
    fields[1] = {"TP2", D(tp2)};   // float  → {"TP2":24.10}
    fields[2] = {"PRS", D(prs)};   // float  → {"PRS":101325.00}
    fields[3] = {"ALT", D(alt)};   // float  → {"ALT":142.30}
    fields[4] = {"HUM", D(hum)};   // float  → {"HUM":61.20}
    fields[5] = {"FIX", B(fix)};   // bool   → {"FIX":true}
    fields[6] = {"MOD", C('A')};   // char   → {"MOD":"A"}
    fields[7] = {"STS", S("OK")};  // string → {"STS":"OK"}

  } else {
    // ── Mode B: Motion / status data ─────────────────────────────────────
    // I(v) — int16_t integer readings (e.g. raw ADC counts, heading degrees)
    int16_t ax = (int16_t)(analogRead(A0) - 512);  // X acceleration (raw)
    int16_t ay = (int16_t)(analogRead(A1) - 512);  // Y acceleration (raw)
    int16_t az = (int16_t)(analogRead(A2) - 512);  // Z acceleration (raw)
    int16_t hdg = (int16_t)((analogRead(A3) / 1023.0f) * 360.0f);  // degrees
    float spd = (analogRead(A4) / 1023.0f) * 50.0f;                // m/s
    bool fix = (analogRead(A5) > 512);

    fields[0] = {"TP1", I(ax)};      // int    → {"TP1":12}   (repurposed slot)
    fields[1] = {"TP2", I(ay)};      // int    → {"TP2":-34}
    fields[2] = {"PRS", I(az)};      // int    → {"PRS":5}
    fields[3] = {"ALT", I(hdg)};     // int    → {"ALT":270}
    fields[4] = {"HUM", D(spd)};     // float  → {"HUM":3.50}
    fields[5] = {"FIX", B(fix)};     // bool   → {"FIX":false}
    fields[6] = {"MOD", C('B')};     // char   → {"MOD":"B"}
    fields[7] = {"STS", S("WARN")};  // string → {"STS":"WARN"}
  }

  *count = NYARKOA_MAX_FIELDS;
}

void setup() {
  pinMode(MODE_PIN, INPUT);
  payload.begin(115200);

  // Register the provider once. The library will:
  //   • call buildPayload() and transmit automatically every 1000 ms
  //   • also respond to "GET" from the comm board at any time, independently
  //     of the auto-transmission interval
  payload.registerPayload(buildPayload, 1000);
}

void loop() {
  // Handles periodic auto-transmission, and receives commands from the comm
  // board (PING, RESET, SYSINFO, GET) — all non-blocking, no delay() needed.
  payload.update();
}
