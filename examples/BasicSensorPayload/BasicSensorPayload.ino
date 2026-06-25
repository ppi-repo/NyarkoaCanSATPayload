/*
 * BasicSensorPayload.ino — NyarkoaCanSATPayload library example
 * Author: Eric Obeng (Profesir), Erictronics Systems
 * Date: 2026-06-01
 *
 * Demonstrates the registerPayload() pattern: the library owns when the payload
 * is transmitted — either automatically every intervalMs milliseconds, or on
 * demand when the comm board sends a "GET" command. No delay() needed.
 *
 * Three of the five value-type helpers are used:
 *   D(v) — decimal (float) value (e.g. sensor reading)
 *   I(v) — int16_t value (e.g. integer count or status code)
 *   B(v) — bool value (e.g. GPS fix flag)
 *
 * Key convention
 * ──────────────
 * Every key must be exactly 3 characters, uppercase alphanumeric (A–Z, 0–9).
 * The comm board resolves keys using its own codebook:
 *   TMP → "Temperature (°C)"
 *   HUM → "Relative Humidity (%)"
 *   FIX → "GPS Fix Acquired"
 *
 * An invalid key (wrong length, lowercase, non-alphanumeric) is caught at
 * compile time — the code will not build. No runtime check is needed.
 *
 * Hardware
 * ────────
 * ATMega328P payload board connected to the Nyarkoa Core Comm Board via
 * hardware UART (pins 0 RX / 1 TX). Sensors simulated with analogRead here.
 */

#include <NyarkoaCanSATPayload.h>

NyarkoaCanSATPayload payload;

// ── Payload provider callback
// ───────────────────────────────────────────────── Called by the library
// whenever the comm board sends "GET", or automatically every intervalMs
// milliseconds (set in registerPayload below). Keep this fast — read from
// globals or sensor libraries, never block here.
void buildPayload(PayloadField* fields, uint8_t* count) {
  // Replace analogRead() expressions with your actual sensor library calls.
  float temperature = (analogRead(A0) / 1023.0f) * 100.0f;  // simulated °C
  float humidity = (analogRead(A1) / 1023.0f) * 100.0f;     // simulated %
  bool gpsFix = (analogRead(A2) > 512);  // simulated fix flag

  // D(v) constructs a VAL_FLOAT entry → serialized as e.g. 23.50
  // B(v) constructs a VAL_BOOL  entry → serialized as true / false
  // Note: D() is used (not F()) so Arduino's F() PROGMEM macro stays available.
  fields[0] = {"TMP", D(temperature)};  // float → {"TMP":23.50}
  fields[1] = {"HUM", D(humidity)};     // float → {"HUM":61.20}
  fields[2] = {"FIX", B(gpsFix)};       // bool  → {"FIX":true}
  *count = 3;
}

void setup() {
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
