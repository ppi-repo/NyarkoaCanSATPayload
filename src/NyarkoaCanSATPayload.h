#ifndef NYARKOA_CANSAT_PAYLOAD_H
#define NYARKOA_CANSAT_PAYLOAD_H

#include <Arduino.h>
#include <avr/wdt.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

// Arduino.h's F() macro for PROGMEM strings is intentionally preserved.
// The float PayloadVal helper is named D() (decimal) to avoid any conflict.

// ── Protocol and buffer constants
// ─────────────────────────────────────────────

#define NYARKOA_MAX_FIELDS 8  // maximum fields per send() call

// Chunk framing uses a 64-byte packet boundary (matches ATMega328 UART TX
// buffer). Framing tokens: <BEG>=5 bytes, <END>=5 bytes, ~=1 byte.
#define NYARKOA_CHUNK_SIZE 64
#define NYARKOA_SINGLE_MAX \
  54  // 64 - len("<BEG>") - len("<END>")  single packet
#define NYARKOA_FIRST_MAX \
  58  // 64 - len("<BEG>") - len("~")     first chunk data
#define NYARKOA_MIDDLE_MAX \
  62  // 64 - len("~")     - len("~")     middle chunk data
#define NYARKOA_LAST_MAX 58  // 64 - len("~")     - len("<END>") last chunk data

// Worst-case JSON for 8 fields: ~25 bytes/field × 8 + 2 braces + null = ~202
// bytes
#define NYARKOA_JSON_BUF_SIZE 210

// ── Compile-time key validation
// ────────────────────────────────────────────────

// Returns true for uppercase letters A-Z and digits 0-9 only.
constexpr bool _isValidChar(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

// Declared but never defined. Calling this from a constexpr evaluation forces
// a compile-time error — the constexpr-poison pattern.
void _nyarkoa_invalid_key();

// Validates one character of a key. Non-constexpr branch poisons constexpr
// eval.
constexpr char _checkKeyChar(const char* k, int i) {
  return _isValidChar(k[i]) ? k[i] : (_nyarkoa_invalid_key(), k[i]);
}

// Validates key length is exactly 3 characters.
// Short-circuit evaluation prevents accessing k[3] when k[2]=='\0'.
constexpr bool _checkKeyLen(const char* k) {
  return (k[0] != '\0' && k[1] != '\0' && k[2] != '\0' && k[3] == '\0')
             ? true
             : (_nyarkoa_invalid_key(), false);
}

// ── FieldKey
// ───────────────────────────────────────────────────────────────────

// A compile-time-enforced 3-character alphanumeric uppercase key code.
// Invalid keys (wrong length, lowercase, non-alphanumeric) are rejected at
// compile time via the constexpr-poison pattern — no runtime overhead.
//
// Users never construct FieldKey directly; it is constructed implicitly from
// a string literal inside a PayloadField initializer:
//   PayloadField f = {"TMP", D(23.5)};
struct FieldKey {
  char code[4];  // 3 chars + null terminator

  constexpr FieldKey(const char* k)
      : code{(_checkKeyLen(k), _checkKeyChar(k, 0)),  // length validated here
             _checkKeyChar(k, 1), _checkKeyChar(k, 2), '\0'} {}
};

// ── ValType
// ────────────────────────────────────────────────────────────────────

// Tags the active union member in PayloadVal. Stored as uint8_t to save SRAM.
enum ValType : uint8_t {
  VAL_FLOAT,  // float       — JSON number,  e.g. 23.50
  VAL_INT,    // int16_t     — JSON integer, e.g. 142
  VAL_BOOL,   // uint8_t     — JSON boolean, e.g. true / false
  VAL_CHAR,   // char        — 1-char JSON string, e.g. "A"
  VAL_STR,    // const char* — JSON string,  e.g. "NOMINAL"
            //   IMPORTANT: the pointed-to string must outlive the send() call.
            //   Use string literals or static buffers — not stack temporaries.
};

// ── PayloadVal
// ─────────────────────────────────────────────────────────────────

// A tagged union holding one value of any supported type.
// The union is 4 bytes (dominated by float / pointer); tag adds 1 byte.
struct PayloadVal {
  ValType type;
  union {
    float f;        // VAL_FLOAT
    int16_t i;      // VAL_INT
    uint8_t b;      // VAL_BOOL  (0 = false, non-zero = true)
    char c;         // VAL_CHAR  (single printable ASCII character)
    const char* s;  // VAL_STR   (must outlive the send() call)
  };
};

// ── PayloadVal helper constructors
// ────────────────────────────────────────────
//
// Use these in PayloadField initializer lists instead of touching the union
// or enum directly. They are plain inline functions — zero flash overhead.
//
// D(v) — decimal (float)  I(v) — int16_t value   B(v) — bool value
// C(v) — single char      S(v) — const char* string (pointer lifetime: see
// VAL_STR)
//
// Note: D() is used instead of F() to preserve Arduino's F() PROGMEM macro.

inline PayloadVal D(float v) {
  PayloadVal p;
  p.type = VAL_FLOAT;
  p.f = v;
  return p;
}
inline PayloadVal I(int16_t v) {
  PayloadVal p;
  p.type = VAL_INT;
  p.i = v;
  return p;
}
inline PayloadVal B(bool v) {
  PayloadVal p;
  p.type = VAL_BOOL;
  p.b = (uint8_t)v;
  return p;
}
inline PayloadVal C(char v) {
  PayloadVal p;
  p.type = VAL_CHAR;
  p.c = v;
  return p;
}
inline PayloadVal S(const char* v) {
  PayloadVal p;
  p.type = VAL_STR;
  p.s = v;
  return p;
}

// ── PayloadField
// ───────────────────────────────────────────────────────────────

// The only data type the user works with directly.
// Aggregate-initialized with a 3-char key literal and a PayloadVal helper:
//   PayloadField fields[] = {
//       {"TMP", D(23.5)},   // float  → {"TMP":23.50}
//       {"ALT", I(142)},    // int    → {"ALT":142}
//       {"FIX", B(true)},   // bool   → {"FIX":true}
//       {"STS", C('A')},    // char   → {"STS":"A"}
//       {"MSG", S("OK")},   // string → {"MSG":"OK"}
//   };
struct PayloadField {
  FieldKey key;    // 3-char alphanumeric uppercase code, compile-time enforced
  PayloadVal val;  // tagged union value — no unit attached
};

// ── PayloadProvider callback
// ────────────────────────────────────────────────
//
// User-defined function that fills `fields` with current sensor data and sets
// `*count` to the number of fields written (max NYARKOA_MAX_FIELDS).
// Called by the library on GET requests and on periodic auto-transmission.
// Must not block — keep it as fast as reading sensor globals/variables.
//
// Example:
//   void myPayload(PayloadField* fields, uint8_t* count) {
//       fields[0] = {"TMP", D(readTemp())};
//       fields[1] = {"ALT", I(altitude)};
//       *count = 2;
//   }
typedef void (*PayloadProvider)(PayloadField* fields, uint8_t* count);

// ── NyarkoaCanSATPayload class
// ─────────────────────────────────────────────────

class NyarkoaCanSATPayload {
 public:
  NyarkoaCanSATPayload();

  // Call once in setup(). Initializes hardware Serial at the given baud rate.
  void begin(uint32_t baud = 9600);

  // Call every iteration of loop(). Non-blocking UART command polling.
  // Dispatches PING, RESET, SYSINFO, and GET commands from the comm board.
  // Also handles periodic auto-transmission when intervalMs > 0.
  void update();

  // Register a payload provider callback.
  //
  // provider   — function that fills a PayloadField array on demand.
  //              Must not be nullptr.
  // cmd        — JSON "CMD" tag used in every transmission (default "DATA").
  // intervalMs — if > 0, the payload is also transmitted automatically
  //              every intervalMs milliseconds via update(). Pass 0 for
  //              on-demand (GET-only) mode.
  //
  // Call once in setup(). Replaces any previously registered provider.
  void registerPayload(PayloadProvider provider, uint32_t intervalMs = 0);

  // Build and transmit a JSON payload over the chunked framing protocol.
  // Prefer registerPayload() for structured payload data; use send() only for
  // one-off or ad-hoc transmissions.
  // fields — array of PayloadField structs (keys + values)
  // count  — number of entries; silently clamped to NYARKOA_MAX_FIELDS (8)
  void send(const PayloadField* fields, uint8_t count, const char* cmd);

 private:
  char _rxBuf[16];  // inbound command accumulator
  uint8_t _rxIdx;

  // ── Registered payload state ──────────────────────────────────────────────
  PayloadProvider _provider;  // user callback; nullptr = not registered
  const char* _providerCmd;   // CMD tag for registered payload transmissions
  uint32_t _intervalMs;       // 0 = GET-only; >0 = also auto-transmit
  uint32_t _lastSendMs;       // millis() timestamp of last auto-transmission

  // ── Per-type JSON value writers ───────────────────────────────────────────
  // Each writes into buf[0..maxLen-1] and returns bytes written.
  // NaN/Inf → 0.0; non-printable char → '?'; null str → ""; truncates
  // gracefully.

  inline uint8_t _writeFloat(char* buf, uint8_t maxLen, float v) {
    if (isnan(v) || isinf(v)) v = 0.0f;
    char tmp[16];
    dtostrf(v, 1, 2, tmp);
    uint8_t len = (uint8_t)strlen(tmp);
    if (len > maxLen) len = maxLen;
    memcpy(buf, tmp, len);
    return len;
  }

  inline uint8_t _writeInt(char* buf, uint8_t maxLen, int16_t v) {
    char tmp[8];  // int16_t max is -32768 = 6 chars + sign + null
    itoa(v, tmp, 10);
    uint8_t len = (uint8_t)strlen(tmp);
    if (len > maxLen) len = maxLen;
    memcpy(buf, tmp, len);
    return len;
  }

  inline uint8_t _writeBool(char* buf, uint8_t maxLen, uint8_t v) {
    const char* lit = v ? "true" : "false";
    uint8_t len = v ? 4 : 5;
    if (len > maxLen) len = maxLen;
    memcpy(buf, lit, len);
    return len;
  }

  inline uint8_t _writeChar(char* buf, uint8_t maxLen, char v) {
    if (v < 0x20 || v > 0x7E) v = '?';
    if (maxLen < 3) return 0;
    buf[0] = '"';
    buf[1] = v;
    buf[2] = '"';
    return 3;
  }

  inline uint8_t _writeStr(char* buf, uint8_t maxLen, const char* v) {
    if (!v) v = "";
    uint8_t len = (uint8_t)strlen(v);
    uint8_t available = (maxLen >= 2) ? (maxLen - 2) : 0;
    if (len > available) len = available;
    buf[0] = '"';
    memcpy(buf + 1, v, len);
    buf[1 + len] = '"';
    return len + 2;
  }

  // ── Core private methods ──────────────────────────────────────────────────

  // Serializes fields into buf as a flat JSON object. Returns bytes written
  // (excluding null terminator). Truncates with valid JSON if buffer is too
  // small.
  uint8_t _buildJson(char* buf, uint8_t bufSize, const PayloadField* fields,
                     uint8_t count, const char* cmd);

  // Frames and transmits buf over Serial using <BEG>/<END>/~ chunking protocol.
  // All transmission goes through this method — never call Serial.print(json)
  // directly.
  void _transmitChunked(const char* buf);

  // Dispatches a received command string (PING / RESET / SYSINFO / GET).
  void _dispatch(const char* cmd);

  // Invokes _provider, builds JSON, and transmits via _transmitChunked().
  // No-op if no provider has been registered.
  void _sendRegisteredPayload();

  // Reads A6 and A7, builds SYSINFO JSON, and transmits via _transmitChunked().
  void _sendSysInfo();

  // Reads ADC pin and converts raw value to voltage (0–5 V, no divider).
  // Only call with analog-input pins; never call digitalRead/digitalWrite on
  // A6/A7.
  float _readVoltage(uint8_t pin);

  // Returns free SRAM bytes via stack-probe technique.
  int _freeSRAM();

  // Triggers a software reset via the watchdog timer (15 ms timeout).
  void _softReset();
};

#endif  // NYARKOA_CANSAT_PAYLOAD_H