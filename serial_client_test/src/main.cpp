// =============================================================================
// Standalone serial client (NO ROS) -- Teensy 4.1 comms baseline
// -----------------------------------------------------------------------------
// Purpose: prove the host <-> Teensy link, measure latency/throughput, and
// exercise the same data directions the micro-ROS node uses, WITHOUT the
// rclc/agent machinery in the way. Once this is solid, the micro-ROS version
// (../bcc_board_reference_ros2) and the eventual Ethernet/UDP transport are
// easy to trust.
//
// Wire protocol: newline-terminated ASCII. One command per line, one reply
// per line. Tokens are space-separated. Easy to drive from a serial monitor
// or the companion host/serial_client.py.
//
//   Host -> Teensy                 Teensy -> Host (reply)
//   ----------------------------   ----------------------------------------
//   PING <seq>                     PONG <seq> <micros>
//   ID?                            ID teensy41 serial-client v1
//   GALVO <x> <y>                  ACK GALVO <x> <y>   (x,y in [0,4095])
//   TRIG <ms>                      ACK TRIG <ms>
//   STREAM <hz>                    ACK STREAM <hz>     (0 = stop)
//   BENCH <n>                      <n telemetry lines> then BENCH DONE <n>
//   <anything else>                ERR <echo>
//
//   Async telemetry (when streaming) :
//   TLM <seq> <millis> <clutch> <galvo_x> <galvo_y> <trig>
//
// No external libraries are required so this always compiles. Hardware hooks
// (DAC, clutch pin, trigger pin) are intentionally stubbed/optional and marked
// with TODO so you can wire in the real peripherals incrementally.
// =============================================================================

#include <Arduino.h>

// ---- pin map (matches ../bcc_board_reference_ros2/include/pin_defs.h) -------
#define TRIG_PIN   33
#define CLUTCH_PIN 38

// ---- protocol / framing -----------------------------------------------------
#define LINE_BUF_SIZE 128

static char    line_buf[LINE_BUF_SIZE];
static uint8_t line_len = 0;

// ---- state mirrored from the ROS node --------------------------------------
static uint16_t galvo_pos[2] = {2048, 2048};  // 12-bit, centered
static bool     trigger_active = false;
static uint32_t trig_end_ms = 0;

// ---- telemetry / streaming --------------------------------------------------
static uint32_t stream_period_us = 0;   // 0 = streaming off
static uint32_t last_stream_us   = 0;
static uint32_t tlm_seq          = 0;

// -----------------------------------------------------------------------------
// Apply a galvo position. TODO: replace with dac.setChannelValue(...) once the
// MCP4728 is wired in (see ../bcc_board_reference_ros2 utils.h).
// -----------------------------------------------------------------------------
static void apply_galvo(uint16_t x, uint16_t y) {
  galvo_pos[0] = x;
  galvo_pos[1] = y;
}

// Read the clutch input. INPUT pin per the ROS node; safe to read even if
// nothing is connected (will float -- wire a pulldown for real use).
static inline uint8_t read_clutch() {
  return (uint8_t)digitalRead(CLUTCH_PIN);
}

// Emit one telemetry line.
static void send_telemetry() {
  Serial.print("TLM ");
  Serial.print(tlm_seq++);      Serial.print(' ');
  Serial.print(millis());       Serial.print(' ');
  Serial.print(read_clutch());  Serial.print(' ');
  Serial.print(galvo_pos[0]);   Serial.print(' ');
  Serial.print(galvo_pos[1]);   Serial.print(' ');
  Serial.println(trigger_active ? 1 : 0);
}

// -----------------------------------------------------------------------------
// Command dispatch. `line` is a NUL-terminated, trimmed command line.
// -----------------------------------------------------------------------------
static void handle_line(char* line) {
  // PING <seq>  -> PONG <seq> <micros>  (micros lets the host measure RTT)
  if (strncmp(line, "PING", 4) == 0) {
    long seq = 0;
    sscanf(line + 4, "%ld", &seq);
    Serial.print("PONG ");
    Serial.print(seq);
    Serial.print(' ');
    Serial.println(micros());
    return;
  }

  if (strcmp(line, "ID?") == 0) {
    Serial.println("ID teensy41 serial-client v1");
    return;
  }

  // GALVO <x> <y>
  if (strncmp(line, "GALVO", 5) == 0) {
    int x = -1, y = -1;
    if (sscanf(line + 5, "%d %d", &x, &y) == 2 &&
        x >= 0 && x <= 4095 && y >= 0 && y <= 4095) {
      apply_galvo((uint16_t)x, (uint16_t)y);
      Serial.print("ACK GALVO ");
      Serial.print(x); Serial.print(' '); Serial.println(y);
    } else {
      Serial.print("ERR "); Serial.println(line);
    }
    return;
  }

  // TRIG <ms>  -- fire the trigger output for <ms> milliseconds (non-blocking)
  if (strncmp(line, "TRIG", 4) == 0) {
    long ms = 0;
    if (sscanf(line + 4, "%ld", &ms) == 1 && ms >= 0) {
      trigger_active = true;
      trig_end_ms = millis() + (uint32_t)ms;
      digitalWrite(TRIG_PIN, HIGH);
      Serial.print("ACK TRIG "); Serial.println(ms);
    } else {
      Serial.print("ERR "); Serial.println(line);
    }
    return;
  }

  // STREAM <hz>  -- 0 stops; otherwise start periodic TLM at <hz>
  if (strncmp(line, "STREAM", 6) == 0) {
    long hz = 0;
    sscanf(line + 6, "%ld", &hz);
    if (hz <= 0) {
      stream_period_us = 0;
    } else {
      if (hz > 100000) hz = 100000;        // sanity clamp
      stream_period_us = 1000000UL / (uint32_t)hz;
      last_stream_us = micros();
    }
    Serial.print("ACK STREAM "); Serial.println(hz);
    return;
  }

  // BENCH <n>  -- blast n telemetry lines as fast as possible (throughput test)
  if (strncmp(line, "BENCH", 5) == 0) {
    long n = 0;
    sscanf(line + 5, "%ld", &n);
    if (n < 0) n = 0;
    for (long i = 0; i < n; i++) send_telemetry();
    Serial.print("BENCH DONE "); Serial.println(n);
    return;
  }

  // Unknown command
  Serial.print("ERR "); Serial.println(line);
}

// -----------------------------------------------------------------------------
// Non-blocking line reader: accumulate bytes until '\n', then dispatch.
// Tolerates CR, ignores overlong lines.
// -----------------------------------------------------------------------------
static void poll_serial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      line_buf[line_len] = '\0';
      if (line_len > 0) handle_line(line_buf);
      line_len = 0;
    } else if (line_len < LINE_BUF_SIZE - 1) {
      line_buf[line_len++] = c;
    } else {
      // overflow: reset and report
      line_len = 0;
      Serial.println("ERR line-overflow");
    }
  }
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(TRIG_PIN, OUTPUT);
  digitalWrite(TRIG_PIN, LOW);
  pinMode(CLUTCH_PIN, INPUT);

  Serial.begin(115200);          // value ignored by Teensy USB CDC
  while (!Serial && millis() < 3000) { /* wait briefly for host */ }
  Serial.println("ID teensy41 serial-client v1");
}

void loop() {
  poll_serial();

  // Expire a one-shot trigger pulse.
  if (trigger_active && (int32_t)(millis() - trig_end_ms) >= 0) {
    trigger_active = false;
    digitalWrite(TRIG_PIN, LOW);
  }

  // Periodic telemetry when streaming is enabled.
  if (stream_period_us) {
    uint32_t now = micros();
    if (now - last_stream_us >= stream_period_us) {
      last_stream_us += stream_period_us;       // drift-free cadence
      send_telemetry();
      digitalWrite(LED_BUILTIN, tlm_seq & 1);   // heartbeat blink
    }
  }
}
