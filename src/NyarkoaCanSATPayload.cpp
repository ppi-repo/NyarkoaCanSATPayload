#include "NyarkoaCanSATPayload.h"

// ── Constructor
// ────────────────────────────────────────────────────────────────

NyarkoaCanSATPayload::NyarkoaCanSATPayload()
    : _rxIdx(0),
      _provider(nullptr),
      _providerCmd("DATA"),
      _intervalMs(0),
      _lastSendMs(0) {
  memset(_rxBuf, 0, sizeof(_rxBuf));
}

// ── Public API
// ─────────────────────────────────────────────────────────────────

void NyarkoaCanSATPayload::begin(uint32_t baud) { Serial.begin(baud); }

void NyarkoaCanSATPayload::update() {
  // ── Periodic auto-transmission ──────────────────────────────────────────
  // Checked before UART polling so a GET arriving in the same loop() tick
  // does not delay a due transmission by another full interval.
  if (_provider && _intervalMs > 0) {
    uint32_t now = millis();
    if (now - _lastSendMs >= _intervalMs) {
      _lastSendMs = now;
      _sendRegisteredPayload();
    }
  }

  // Non-blocking: drain all available bytes, accumulate into _rxBuf.
  // Dispatch on newline or carriage return; silently discard overflow
  // characters.
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      _rxBuf[_rxIdx] = '\0';
      if (_rxIdx > 0) {
        _dispatch(_rxBuf);
      }
      _rxIdx = 0;
    } else if (_rxIdx < (uint8_t)(sizeof(_rxBuf) - 1)) {
      _rxBuf[_rxIdx++] = c;
    }
    // Characters beyond the buffer are silently discarded — overflow
    // protection.
  }
}

void NyarkoaCanSATPayload::send(const PayloadField* fields, uint8_t count,
                                const char* cmd) {
  if (!fields) return;
  if (count > NYARKOA_MAX_FIELDS) count = NYARKOA_MAX_FIELDS;

  char buf[NYARKOA_JSON_BUF_SIZE];
  _buildJson(buf, NYARKOA_JSON_BUF_SIZE, fields, count, cmd);
  _transmitChunked(buf);
}

void NyarkoaCanSATPayload::registerPayload(PayloadProvider provider,
                                           uint32_t intervalMs) {
  _provider = provider;
  _providerCmd = "PAYLOAD";
  _intervalMs = intervalMs;
  _lastSendMs = millis();  // start the interval clock now, not from boot
}

// ── JSON serialization
// ─────────────────────────────────────────────────────────

uint8_t NyarkoaCanSATPayload::_buildJson(char* buf, uint8_t bufSize,
                                         const PayloadField* fields,
                                         uint8_t count, const char* cmd) {
  uint8_t pos = 0;
  buf[pos++] = '{';

  // Inject "CMD":"<value>" as the first field.
  // pos > 1 is used throughout to detect whether a field has already been
  // written — avoids a separate bool on the stack.
  if (cmd && (uint8_t)(pos + 8) <= (uint8_t)(bufSize - 2)) {
    memcpy(buf + pos, "\"CMD\":", 6);
    pos += 6;
    pos += _writeStr(buf + pos, (uint8_t)(bufSize - pos - 2), cmd);
  }

  for (uint8_t i = 0; i < count; i++) {
    if (pos > 1) {
      if (pos >= (uint8_t)(bufSize - 2)) break;
      buf[pos++] = ',';
    }

    if ((uint8_t)(pos + 6) > (uint8_t)(bufSize - 2)) break;
    buf[pos++] = '"';
    buf[pos++] = fields[i].key.code[0];
    buf[pos++] = fields[i].key.code[1];
    buf[pos++] = fields[i].key.code[2];
    buf[pos++] = '"';
    buf[pos++] = ':';

    uint8_t room = (uint8_t)(bufSize - pos - 2);
    uint8_t written = 0;

    switch (fields[i].val.type) {
      case VAL_FLOAT:
        written = _writeFloat(buf + pos, room, fields[i].val.f);
        break;
      case VAL_INT:
        written = _writeInt(buf + pos, room, fields[i].val.i);
        break;
      case VAL_BOOL:
        written = _writeBool(buf + pos, room, fields[i].val.b);
        break;
      case VAL_CHAR:
        written = _writeChar(buf + pos, room, fields[i].val.c);
        break;
      case VAL_STR:
        written = _writeStr(buf + pos, room, fields[i].val.s);
        break;
    }
    pos += written;
  }

  buf[pos++] = '}';
  buf[pos] = '\0';
  return pos;
}

// ── Chunked framing transmission
// ───────────────────────────────────────────────
//
// Protocol:
//   Single packet (data ≤ 54 B):  <BEG>data<END>
//   First chunk:                  <BEG>data~
//   Middle chunk(s):              ~data~
//   Last chunk:                   ~data<END>
//
// Chunk size = 64 bytes (matches ATMega328 UART TX buffer).
// Constants NYARKOA_*_MAX define max data bytes per chunk type.

void NyarkoaCanSATPayload::_transmitChunked(const char* buf) {
  uint8_t len = (uint8_t)strlen(buf);

  // Fits in a single framed packet
  if (len <= NYARKOA_SINGLE_MAX) {
    Serial.print("<BEG>");
    Serial.print(buf);
    Serial.print("<END>");
    return;
  }

  uint8_t pos = 0;
  bool firstChunk = true;

  while (pos < len) {
    uint8_t remaining = len - pos;

    if (firstChunk) {
      uint8_t chunkLen = (remaining < NYARKOA_FIRST_MAX)
                             ? remaining
                             : (uint8_t)NYARKOA_FIRST_MAX;
      Serial.print("<BEG>");
      Serial.write(buf + pos, chunkLen);
      Serial.print("~");
      pos += chunkLen;
      firstChunk = false;

    } else if (remaining <= NYARKOA_LAST_MAX) {
      // Final chunk — all remaining data fits
      Serial.print("~");
      Serial.write(buf + pos, remaining);
      Serial.print("<END>");
      break;

    } else {
      // Middle chunk
      uint8_t chunkLen = (remaining < NYARKOA_MIDDLE_MAX)
                             ? remaining
                             : (uint8_t)NYARKOA_MIDDLE_MAX;
      Serial.print("~");
      Serial.write(buf + pos, chunkLen);
      Serial.print("~");
      pos += chunkLen;
    }
  }
}

// ── Inbound command handling
// ───────────────────────────────────────────────────

void NyarkoaCanSATPayload::_dispatch(const char* cmd) {
  if (strcmp(cmd, "PING") == 0) {
    Serial.print("PONG\n");

  } else if (strcmp(cmd, "RESET") == 0) {
    _softReset();

  } else if (strcmp(cmd, "SYSINFO") == 0) {
    _sendSysInfo();

  } else if (strcmp(cmd, "GET") == 0) {
    // ESP32 is requesting the current registered payload snapshot.
    // _lastSendMs is intentionally NOT updated here — a GET between two
    // periodic ticks does not reset the auto-transmission interval.
    _sendRegisteredPayload();
  }
  // Unknown commands are silently ignored — no error response.
}

// ── SYSINFO response
// ───────────────────────────────────────────────────────────
//
// Keys: BAT = battery voltage (A6), SYS = 5V rail voltage (A7), RAM = free
// SRAM. Transmitted through the same _transmitChunked pipeline as payload data.

void NyarkoaCanSATPayload::_sendSysInfo() {
  PayloadField info[3] = {
      {"BAT", D(_readVoltage(A6))},
      {"SYS", D(_readVoltage(A7))},
      {"RAM", D((float)_freeSRAM())},
  };
  char buf[NYARKOA_JSON_BUF_SIZE];
  _buildJson(buf, NYARKOA_JSON_BUF_SIZE, info, 3, "SYSINFO");
  _transmitChunked(buf);
}

// ── Registered payload dispatch
// ────────────────────────────────────────────────

void NyarkoaCanSATPayload::_sendRegisteredPayload() {
  if (!_provider) return;

  // FieldKey has no default constructor (its constexpr ctor requires a const
  // char* argument), so a plain PayloadField[N] declaration is ill-formed.
  // Allocate raw storage instead and cast — the provider is responsible for
  // writing every slot it reports via *count, so no slot is ever read
  // uninitialised.
  uint8_t raw[sizeof(PayloadField) * NYARKOA_MAX_FIELDS];
  PayloadField* fields = reinterpret_cast<PayloadField*>(raw);
  uint8_t count = 0;
  _provider(fields, &count);

  if (count == 0) return;
  if (count > NYARKOA_MAX_FIELDS) count = NYARKOA_MAX_FIELDS;

  char buf[NYARKOA_JSON_BUF_SIZE];
  _buildJson(buf, NYARKOA_JSON_BUF_SIZE, fields, count, _providerCmd);
  _transmitChunked(buf);
}

// ── Hardware helpers
// ───────────────────────────────────────────────────────────

float NyarkoaCanSATPayload::_readVoltage(uint8_t pin) {
  // A6 and A7 on ATMega328P are analog-input-only pins.
  // analogRead() is the only valid access method — digitalRead/Write must never
  // be called on these pins.
  int raw = analogRead(pin);
  return (raw / 1023.0f) * 5.0f;
}

int NyarkoaCanSATPayload::_freeSRAM() {
  extern int __heap_start, *__brkval;
  int v;
  return (int)&v - (__brkval == 0 ? (int)&__heap_start : (int)__brkval);
}

void NyarkoaCanSATPayload::_softReset() {
  wdt_enable(WDTO_15MS);
  while (true) {
  }  // spin until watchdog fires (~15 ms)
}