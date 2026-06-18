# serial_client_test — standalone serial baseline (no ROS)

A dependency-free comms baseline for the Teensy 4.1 peripheral. Same data
directions as the micro-ROS node in [`../bcc_board_reference_ros2`](../bcc_board_reference_ros2),
but over a plain newline-ASCII protocol so you can validate the link, latency,
and throughput **before** the rclc/agent layer is in the picture.

```
serial_client_test/
├── platformio.ini      Teensy 4.1 build (no external libs)
├── src/main.cpp        firmware: line protocol + telemetry streaming
└── host/
    ├── serial_client.py   host CLI: ping / galvo / trig / monitor / bench / repl
    └── requirements.txt   pyserial
```

## Wire protocol (newline-terminated ASCII)

| Host → Teensy        | Teensy → Host reply                 | Notes                       |
|----------------------|-------------------------------------|-----------------------------|
| `PING <seq>`         | `PONG <seq> <micros>`               | latency probe               |
| `ID?`                | `ID teensy41 serial-client v1`      |                             |
| `GALVO <x> <y>`      | `ACK GALVO <x> <y>`                 | x,y in `[0,4095]`           |
| `TRIG <ms>`          | `ACK TRIG <ms>`                     | non-blocking one-shot pulse |
| `STREAM <hz>`        | `ACK STREAM <hz>`                   | `0` stops                   |
| `BENCH <n>`          | n × `TLM …` then `BENCH DONE <n>`   | throughput test             |

Async telemetry while streaming:
`TLM <seq> <millis> <clutch> <galvo_x> <galvo_y> <trig>`

## Flash the firmware

```powershell
cd serial_client_test
pio run -t upload          # builds + uploads to the Teensy 4.1
```

## Run the host client

```powershell
cd serial_client_test/host
pip install -r requirements.txt

python serial_client.py --port COM5 id
python serial_client.py --port COM5 ping --count 1000
python serial_client.py --port COM5 galvo 1024 3072
python serial_client.py --port COM5 trig 50
python serial_client.py --port COM5 monitor --hz 200
python serial_client.py --port COM5 bench --lines 200000
python serial_client.py --port COM5 repl
```

Find the COM port in Device Manager (or `pio device list`). On Teensy USB CDC
the baud value is ignored — the link runs at full USB speed regardless.

## Measured baseline (Teensy 4.1, USB CDC, COM4)

| Test            | Result                                              |
|-----------------|-----------------------------------------------------|
| `ping --count 1000` | RTT min/avg 0.22 / 0.27 ms, p99 0.44 ms, jitter 0.045 ms |
| `bench --lines 100000` | 100000 lines, 3.3 MB in 0.37 s → ~268k lines/s, ~8.4 MB/s |

Note: the host drains the port in bulk chunks, not line-by-line. Teensy USB CDC
drops queued TX data after an internal timeout if the host reader falls behind,
so a slow byte-by-byte reader can lose the tail of a high-rate stream and hang.
`bench` and any high-rate `monitor` consumer must keep up.

## Hardware hooks

The firmware compiles with no peripherals attached. To drive the real BCC
hardware, wire in the MCP4728 DAC and replace the `TODO` in `apply_galvo()`
(see `../bcc_board_reference_ros2/include/utils.h`). `TRIG_PIN` (33) and
`CLUTCH_PIN` (38) already match the ROS node's pin map.

## Next steps

1. **This** — plain serial baseline (done).
2. micro-ROS over serial — already in `../bcc_board_reference_ros2`.
3. micro-ROS over Ethernet/UDP — swap the transport to the QNEthernet kit and
   point the Teensy at a `micro_ros_agent` UDP endpoint.
```
