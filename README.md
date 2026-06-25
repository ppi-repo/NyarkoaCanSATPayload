# NyarkoaCanSATPayload

> Lightweight payload communication library for ATMega328-based CanSAT boards.
> Handles outbound JSON telemetry, chunked packet framing, inbound command
> dispatch, periodic auto-transmission, and system diagnostics — no third-party
> libraries required.

---

## Table of Contents

1. [Overview](#1-overview)
2. [Hardware Setup](#2-hardware-setup)
3. [Installation](#3-installation)
4. [Dependencies](#4-dependencies)
5. [Key Convention](#5-key-convention)
6. [Quick Start](#6-quick-start)
7. [API Reference](#7-api-reference)
   - [Types](#types)
   - [Value Helper Functions](#value-helper-functions)
   - [PayloadProvider Callback](#payloadprovider-callback)
   - [NyarkoaCanSATPayload Class](#nyarkoacansatpayload-class)
8. [Inbound Command Handling](#8-inbound-command-handling)
9. [Packet Protocol](#9-packet-protocol)
10. [SYSINFO](#10-sysinfo)
11. [Memory Usage](#11-memory-usage)
12. [Examples](#12-examples)
13. [keywords.txt](#13-keywordstxt)
14. [License](#14-license)

---

## 1. Overview

**NyarkoaCanSATPayload** manages all UART communication between an ATMega328P
payload board and the Nyarkoa CanSAT Core Communication Board.

Core features:

- **Pull-based payload registration** — define your sensor data once via
  `registerPayload()`; the library handles when and how it is transmitted.
- **Periodic auto-transmission** — the library fires your payload callback
  automatically every N milliseconds, driven by `update()` in `loop()`.
- **On-demand GET** — the comm board can request a fresh payload snapshot at
  any time by sending `GET`; the library responds immediately without
  disturbing the auto-transmission interval.
- **Chunked JSON framing** — payloads larger than 54 bytes are automatically
  split into 64-byte framed chunks (`<BEG>`/`~`/`<END>`) and reassembled by
  the comm board.
- **Compile-time key validation** — all 3-character field keys are validated
  at compile time via a `constexpr` constructor. Invalid keys (wrong length,
  lowercase, non-alphanumeric) cause a build error before the firmware ever
  runs.
- **Zero dependencies** — JSON serialization is built in. No ArduinoJson or
  any other third-party library is required.
- **Inbound command dispatch** — `PING`, `RESET`, `SYSINFO`, and `GET`
  commands are handled automatically inside `update()`.

---

## 2. Hardware Setup

| Pin | Function | Notes |
|-----|----------|-------|
| 0 (RX) | UART receive | Hardware Serial — receives commands from comm board |
| 1 (TX) | UART transmit | Hardware Serial — sends JSON telemetry to comm board |
| A6 | Battery voltage monitor | Analog-input only on ATMega328P; `analogRead()` only |
| A7 | 5 V rail voltage monitor | Analog-input only on ATMega328P; `analogRead()` only |

> **Warning:** Pins A6 and A7 on the ATMega328P are **analog-input only**.
> Never call `digitalRead()` or `digitalWrite()` on them. The library uses
> `analogRead()` exclusively for these pins.

Both A6 and A7 are connected directly to the ADC without a voltage divider
and must remain within the 0–5 V input range at all times.

---

## 3. Installation

### Method A — Install from `.zip` via Arduino IDE

1. Download or export the library folder as `NyarkoaCanSATPayload.zip`.
2. In the Arduino IDE: **Sketch → Include Library → Add .ZIP Library…**
3. Select the downloaded `.zip` file.
4. Restart the IDE if prompted.

### Method B — Manual folder copy

1. Copy the `NyarkoaCanSATPayload` folder (the one containing
   `library.properties`) into your Arduino `libraries/` directory:
   - **Windows:** `Documents\Arduino\libraries\`
   - **macOS / Linux:** `~/Arduino/libraries/`
2. Restart the Arduino IDE.

---

## 4. Dependencies

This library has **no third-party dependencies**. JSON serialization is built
in. AVR-libc headers (`avr/wdt.h`, `stdlib.h`, `string.h`, `math.h`) are
bundled with every Arduino IDE installation targeting AVR boards.

---

## 5. Key Convention

### 3-Character Alphanumeric Uppercase Codes

Every data field key must satisfy all three rules simultaneously:

1. **Exactly 3 characters** — no more, no fewer.
2. **Uppercase alphanumeric only** — characters `A–Z` and `0–9`. Lowercase
   letters, underscores, hyphens, and spaces are not permitted.
3. **Lookup codes, not labels** — the comm board resolves a code like `TP1`
   to `"Temperature Sensor 1 (°C)"` using its own codebook. The payload
   board never embeds units or descriptions in the key itself.

**Valid examples:** `TMP`, `PRS`, `ALT`, `HUM`, `TP1`, `TP2`, `AC1`, `LAT`,
`LNG`, `CO2`, `FIX`, `MOD`, `STS`, `BAT`, `SYS`

**Invalid examples:**

| Key | Reason |
|-----|--------|
| `temp` | lowercase characters |
| `TEMP` | 4 characters |
| `T1` | 2 characters |
| `T_1` | underscore not permitted |
| `"TMP"` | do not include quotes — pass the literal `"TMP"` as the initializer |

### Compile-Time Enforcement

Key validation is performed by the `FieldKey` `constexpr` constructor using
the constexpr-poison pattern. Any key string literal that violates the rules
causes a **compile error at the call site** — the firmware will not build.
There is no runtime check, no silent failure, and no SRAM overhead.

```cpp
PayloadField fields[] = {
    {"TMP", D(23.5)},   // ✓ valid — compiles
    {"temp", D(23.5)},  // ✗ COMPILE ERROR: lowercase not allowed
    {"TEMP", D(23.5)},  // ✗ COMPILE ERROR: 4 characters
    {"T1",  D(23.5)},   // ✗ COMPILE ERROR: 2 characters
};
```

### Codebook Separation

The payload board transmits raw 3-char codes. The comm board (or ground
station software) holds the codebook that maps codes to human-readable labels,
units, and display formatting. This separation keeps the payload firmware
small and allows the codebook to be updated on the ground side without
reflashing the payload board.

---

## 6. Quick Start

The recommended pattern is `registerPayload()` — define your sensor callback
once, let the library manage transmission timing entirely.

```cpp
#include <NyarkoaCanSATPayload.h>

NyarkoaCanSATPayload payload;

// Called by the library automatically every 1000 ms,
// and on demand whenever the comm board sends "GET".
void buildPayload(PayloadField* fields, uint8_t* count) {
    fields[0] = {"TMP", D(readTemperature())};  // float  → {"TMP":23.50}
    fields[1] = {"ALT", I(readAltitude())};     // int    → {"ALT":142}
    fields[2] = {"FIX", B(gps.hasFix())};       // bool   → {"FIX":true}
    *count = 3;
}

void setup() {
    payload.begin(115200);

    // Transmit every 1000 ms; also respond to "GET" at any time.
    payload.registerPayload(buildPayload, 1000);
}

void loop() {
    payload.update(); // all timing and command handling happens here
}
```

`loop()` needs nothing else. No `delay()`, no manual `send()` call, no
timestamp bookkeeping.

---

## 7. API Reference

### Types

---

#### `FieldKey`

```cpp
struct FieldKey {
    char code[4]; // 3-char key + null terminator
    constexpr FieldKey(const char* k);
};
```

A compile-time-enforced 3-character alphanumeric uppercase key. Constructed
implicitly from a string literal inside a `PayloadField` initializer — users
never construct `FieldKey` directly. An invalid key literal produces a compile
error.

---

#### `ValType`

```cpp
enum ValType : uint8_t {
    VAL_FLOAT,   // float       — JSON number,  e.g. 23.50
    VAL_INT,     // int16_t     — JSON integer, e.g. 142
    VAL_BOOL,    // uint8_t     — JSON boolean, e.g. true / false
    VAL_CHAR,    // char        — 1-char JSON string, e.g. "A"
    VAL_STR,     // const char* — JSON string,  e.g. "NOMINAL"
};
```

Tags the active member of the `PayloadVal` union. Stored as `uint8_t` to
minimise SRAM usage. Users never set `ValType` directly — use the helper
functions below.

---

#### `PayloadVal`

```cpp
struct PayloadVal {
    ValType type;
    union {
        float       f;   // VAL_FLOAT
        int16_t     i;   // VAL_INT
        uint8_t     b;   // VAL_BOOL  (0 = false, non-zero = true)
        char        c;   // VAL_CHAR  (single printable ASCII character)
        const char* s;   // VAL_STR   (pointer must outlive the send() call)
    };
};
```

A tagged union holding one value of any supported type. The union is 4 bytes
(dominated by `float` / pointer); the tag adds 1 byte. Construct exclusively
via the helper functions below — never set union members or the tag directly.

> **`VAL_STR` lifetime:** The `const char*` passed to `S()` must remain valid
> for the entire duration of the library's internal `send` call. String
> literals (e.g. `S("OK")`) have indefinite lifetime and are always safe.
> Do **not** pass a pointer to a stack-local `char` array that may go out of
> scope before the transmission completes.

---

#### `PayloadField`

```cpp
struct PayloadField {
    FieldKey   key; // 3-char key, compile-time enforced
    PayloadVal val; // tagged union value
};
```

The only data type the user works with directly. Initialise as an aggregate:

```cpp
PayloadField fields[] = {
    {"TMP", D(23.5)},      // float  → {"TMP":23.50}
    {"ALT", I(142)},       // int    → {"ALT":142}
    {"FIX", B(true)},      // bool   → {"FIX":true}
    {"MOD", C('A')},       // char   → {"MOD":"A"}
    {"STS", S("NOMINAL")}, // string → {"STS":"NOMINAL"}
};
```

---

### Value Helper Functions

Use these to construct `PayloadVal` values inside `PayloadField` initializers.
They are plain inline functions — zero flash overhead.

| Function | Stored type | JSON output example |
|----------|-------------|---------------------|
| `D(float v)` | `VAL_FLOAT` | `23.50` |
| `I(int16_t v)` | `VAL_INT` | `142` |
| `B(bool v)` | `VAL_BOOL` | `true` / `false` |
| `C(char v)` | `VAL_CHAR` | `"A"` |
| `S(const char* v)` | `VAL_STR` | `"NOMINAL"` |

> **Why `D()` and not `F()`?** The float helper is named `D()` (decimal) to
> preserve Arduino's built-in `F()` PROGMEM macro for string literals. Both
> can be used freely in the same sketch.

**Serialization edge cases handled automatically:**

| Input | Behaviour |
|-------|-----------|
| `NaN` or `Inf` passed to `D()` | Transmitted as `0.0` |
| Non-printable char passed to `C()` | Transmitted as `"?"` |
| `nullptr` passed to `S()` | Transmitted as `""` |

---

### PayloadProvider Callback

```cpp
typedef void (*PayloadProvider)(PayloadField* fields, uint8_t* count);
```

A user-defined function that fills a `PayloadField` array with the current
sensor snapshot. The library calls this function whenever a transmission is
due — either on the auto-transmission interval or in response to a `GET`
command.

**Signature contract:**

| Parameter | Direction | Description |
|-----------|-----------|-------------|
| `fields` | out | Pre-allocated array of `NYARKOA_MAX_FIELDS` (8) entries. Write fields by index starting at 0. |
| `count` | out | Set `*count` to the number of fields written before returning. The library reads only up to `*count` entries. |

**Rules:**
- Must not block. Read from globals or cached sensor values — never call
  `delay()` or wait on I²C/SPI inside the callback.
- Must write at least one field and set `*count ≥ 1`. If `*count` is left
  at 0, the library skips the transmission silently.
- Must not write more than `NYARKOA_MAX_FIELDS` (8) fields.

```cpp
void buildPayload(PayloadField* fields, uint8_t* count) {
    fields[0] = {"TMP", D(readTemp())};
    fields[1] = {"HUM", D(readHumidity())};
    fields[2] = {"FIX", B(gps.hasFix())};
    *count = 3;
}
```

---

### NyarkoaCanSATPayload Class

---

#### Constructor

```cpp
NyarkoaCanSATPayload();
```

Default constructor. No parameters. Initialises internal state; does not
touch hardware. Call `begin()` in `setup()` to initialise the UART.

---

#### `begin()`

```cpp
void begin(uint32_t baud = 9600);
```

Initialises hardware `Serial` at the given baud rate. Must be called once
in `setup()` before any transmission or reception can occur.

| Parameter | Default | Description |
|-----------|---------|-------------|
| `baud` | `9600` | UART baud rate. Must match the comm board configuration. |

---

#### `registerPayload()`

```cpp
void registerPayload(PayloadProvider provider,
                     uint32_t        intervalMs = 0);
```

Registers a payload provider callback and configures transmission behaviour.
Call once in `setup()` after `begin()`. Replaces any previously registered
provider. The `"CMD"` field in the transmitted JSON is automatically set to
`"PAYLOAD"`.

| Parameter | Default | Description |
|-----------|---------|-------------|
| `provider` | — | Pointer to the user's payload callback. Must not be `nullptr`. |
| `intervalMs` | `0` | Auto-transmission interval in milliseconds. `0` = GET-only mode (no automatic transmission). |

**Transmission modes:**

| `intervalMs` | Behaviour |
|--------------|-----------|
| `0` | GET-only — payload is sent only when the comm board sends `GET`. |
| `> 0` | Periodic — payload is sent automatically every `intervalMs` ms **and** on every `GET`. |

> **Interval seeding:** `_lastSendMs` is initialised to `millis()` at the
> moment `registerPayload()` is called. The first automatic transmission fires
> one full interval later — not immediately on the next `update()` call.

> **GET independence:** A `GET` command received between two periodic ticks
> does **not** reset the interval clock. The next automatic transmission fires
> at the originally scheduled time.

---

#### `update()`

```cpp
void update();
```

The library's main heartbeat. Must be called on **every iteration** of
`loop()` — missing calls will cause commands to be delayed or dropped and
periodic transmissions to drift.

Two things happen on each call:

1. **Periodic auto-transmission check** — if a provider is registered and
   `intervalMs > 0`, checks whether the interval has elapsed and fires a
   transmission if so.
2. **Non-blocking UART receive** — drains all available bytes from
   `Serial`, accumulates them into the internal 16-byte command buffer, and
   dispatches completed commands on newline or carriage return. See
   [Inbound Command Handling](#8-inbound-command-handling).

---

#### `send()`

```cpp
void send(const PayloadField* fields, uint8_t count, const char* cmd);
```

Serialises `fields[0..count-1]` to JSON and transmits immediately via the
chunked framing protocol. `count` is silently clamped to `NYARKOA_MAX_FIELDS`
(8).

> **Prefer `registerPayload()` for structured sensor data.** Use `send()` only
> for one-off or ad-hoc transmissions that fall outside the normal payload
> cycle.

| Parameter | Description |
|-----------|-------------|
| `fields` | Pointer to the first `PayloadField`. Must not be `nullptr`. |
| `count` | Number of fields to transmit. Clamped to 8. |
| `cmd` | String written as the `"CMD"` field in the JSON object. |

---

## 8. Inbound Command Handling

The library listens for newline-terminated ASCII commands sent by the comm
board. Commands are matched case-sensitively. Unknown commands are silently
ignored — no error response is sent.

| Command | Response | Notes |
|---------|----------|-------|
| `PING` | `PONG\n` | Connectivity check. |
| `RESET` | *(none — board resets)* | Triggers a watchdog-based software reset (~15 ms). |
| `SYSINFO` | JSON object (see [§10](#10-sysinfo)) | Battery voltage, rail voltage, free SRAM. |
| `GET` | Registered payload JSON | Invokes the provider and transmits immediately. Does not reset the auto-transmission interval. |

The inbound buffer is 16 bytes. Commands longer than 15 characters are
silently truncated — all built-in commands are well within this limit.

---

## 9. Packet Protocol

### Framing Tokens

| Token | Wire bytes | Role |
|-------|-----------|------|
| `<BEG>` | 5 | Start of message |
| `<END>` | 5 | End of message |
| `~` | 1 | Chunk continuation marker |

### Packet Formats

| Case | Wire format |
|------|-------------|
| Single packet (data ≤ 54 bytes) | `<BEG>data<END>` |
| First chunk | `<BEG>data~` |
| Middle chunk(s) | `~data~` |
| Last chunk | `~data<END>` |

The chunk size is **64 bytes** — one ATMega328P UART TX buffer flush.
Maximum data bytes per chunk type:

| Chunk type | Max data bytes | Calculation |
|------------|---------------|-------------|
| Single | 54 | 64 − 5 (`<BEG>`) − 5 (`<END>`) |
| First | 58 | 64 − 5 (`<BEG>`) − 1 (`~`) |
| Middle | 62 | 64 − 1 (`~`) − 1 (`~`) |
| Last | 58 | 64 − 1 (`~`) − 5 (`<END>`) |

All payload transmission — whether triggered by the interval, a `GET`, or a
direct `send()` call — passes through the same `_transmitChunked()` pipeline.
The comm board accumulates chunks, strips framing tokens, and concatenates
data regions to reconstruct the full JSON object.

### Worked Example — 90-byte JSON split into two chunks

Input JSON (90 bytes):
```
{"CMD":"PAYLOAD","TP1":23.50,"TP2":24.10,"PRS":101325.00,"ALT":142.30,"FIX":true}
```

Transmission:
```
Chunk 1 (first):  <BEG>{"CMD":"PAYLOAD","TP1":23.50,"TP2":24.10,"PRS":101325.00~
                  ←5→  ←────────────────────── 58 bytes ──────────────────────→←1→
                  Total wire bytes: 64

Chunk 2 (last):   ~,"ALT":142.30,"FIX":true}<END>
                  ←1→←────── 32 bytes ──────→←5→
                  Total wire bytes: 38
```

---

## 10. SYSINFO

The comm board can request system diagnostics at any time by sending
`SYSINFO\n`. The library responds with a JSON object transmitted through the
same chunked framing pipeline:

```json
{"CMD":"SYSINFO","BAT":3.84,"SYS":4.97,"RAM":312.00}
```

| Key | ADC source | Meaning |
|-----|-----------|---------|
| `BAT` | `analogRead(A6)` | Battery voltage (V) — direct ADC read, no divider |
| `SYS` | `analogRead(A7)` | 5 V rail voltage (V) — direct ADC read, no divider |
| `RAM` | Stack-probe | Free SRAM bytes, reported as float for JSON consistency |

The SYSINFO response does not interact with the registered payload provider
and does not affect the auto-transmission interval.

---

## 11. Memory Usage

### Class instance (SRAM)

| Member | Type | Size |
|--------|------|------|
| `_rxBuf[16]` | `char[16]` | 16 bytes |
| `_rxIdx` | `uint8_t` | 1 byte |
| `_provider` | function pointer | 2 bytes (AVR) |
| `_providerCmd` | `const char*` | 2 bytes (AVR) |
| `_intervalMs` | `uint32_t` | 4 bytes |
| `_lastSendMs` | `uint32_t` | 4 bytes |
| **Total** | | **29 bytes** |

### Per-field on the user's stack (inside the provider)

`PayloadField` = `FieldKey` (4 B) + `PayloadVal` tag (1 B) + union (4 B) =
**9 bytes per field**. Eight fields = 72 bytes.

### Peak stack usage during transmission

`char buf[210]` is allocated on the stack inside `_sendRegisteredPayload()`
and `send()` for the duration of the JSON build + transmit, then released.
Peak stack usage is approximately **210 bytes** above baseline. With 2 KB
total SRAM on the ATMega328P this is well within budget, but keep other
stack allocations modest.

### Flash

The library contributes approximately **1.2–1.8 KB of flash** depending on
which value types are used, compared to ≈3–4 KB when using ArduinoJson. The
custom serializer is the primary reason for this saving.

---

## 12. Examples

### BasicSensorPayload

`examples/BasicSensorPayload/BasicSensorPayload.ino`

Registers a 3-field payload provider and transmits automatically every second.
Uses `D()` (float), `I()` (int), and `B()` (bool) helpers. Sensor readings
are simulated with `analogRead()` — replace with real sensor library calls.
The recommended starting template for any single-sensor payload.

### MultiChannelPayload

`examples/MultiChannelPayload/MultiChannelPayload.ino`

Fills all 8 channels (`NYARKOA_MAX_FIELDS`) and demonstrates all five value
helpers: `D()`, `I()`, `B()`, `C()`, `S()`. Two operating modes
(environmental vs. motion data) are selected by a digital pin sampled inside
the provider on every call, so a mode change takes effect on the very next
transmission without a restart. Includes the full key codebook in a header
comment.

---

## 13. `keywords.txt`

The following identifiers are registered for Arduino IDE syntax highlighting:

**KEYWORD1 (types/classes):**
`NyarkoaCanSATPayload`, `PayloadField`, `PayloadVal`, `FieldKey`,
`ValType`, `PayloadProvider`

**KEYWORD2 (methods/functions):**
`begin`, `update`, `send`, `registerPayload`, `D`, `I`, `B`, `C`, `S`

**LITERAL1 (constants):**
`NYARKOA_MAX_FIELDS`, `NYARKOA_CHUNK_SIZE`, `NYARKOA_JSON_BUF_SIZE`,
`NYARKOA_SINGLE_MAX`, `NYARKOA_FIRST_MAX`, `NYARKOA_MIDDLE_MAX`,
`NYARKOA_LAST_MAX`, `VAL_FLOAT`, `VAL_INT`, `VAL_BOOL`, `VAL_CHAR`,
`VAL_STR`

---

## 14. License

MIT License — see [LICENSE](LICENSE) for full text.

Copyright © 2026 Nyarkoa CanSAT Team