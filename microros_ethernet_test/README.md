# microros_ethernet_test — high-performance micro-ROS over Ethernet/UDP

A Teensy 4.1 micro-ROS node tuned for **>500 Hz** publishing to ROS2 (Jazzy) over
UDP. Custom transport on **QNEthernet** (lwIP), **best-effort** QoS, and a lean
direct-publish loop. No usbipd needed at runtime — the agent talks over the LAN.

| Topic                  | Type                    | Dir           | QoS         | Notes                                   |
|------------------------|-------------------------|---------------|-------------|-----------------------------------------|
| `teensy/galvo_state`   | `geometry_msgs/Point32` | Teensy → ROS2 | best-effort | x,y pattern, z = seq; published when mode 0 |
| `teensy/galvo_state_rel`| `geometry_msgs/Point32`| Teensy → ROS2 | reliable    | same payload; published when mode 1     |
| `teensy/galvo_full`    | `teensy_msgs/GalvoState`| Teensy → ROS2 | best-effort | multi-signal (~28 B); published when mode 2 |
| `teensy/pong`          | `std_msgs/Int32`        | Teensy → ROS2 | best-effort | RTT echo of `teensy/ping`               |
| `teensy/command`       | `std_msgs/Int32`        | ROS2 → Teensy | reliable    | LSB → onboard LED                       |
| `teensy/set_rate_hz`   | `std_msgs/Int32`        | ROS2 → Teensy | reliable    | change publish rate live (1..50000)     |
| `teensy/set_mode`      | `std_msgs/Int32`        | ROS2 → Teensy | reliable    | 0=Point32 BE, 1=Point32 reliable, 2=GalvoState BE |
| `teensy/ping`          | `std_msgs/Int32`        | ROS2 → Teensy | best-effort | RTT probe (echoed on `teensy/pong`)     |

Custom message `teensy_msgs/GalvoState` lives in `ros2_ws/src/teensy_msgs`; `build.sh`
stages it into the firmware's `extra_packages/` so the micro-ROS lib compiles the C type.

## Performance choices (and why)

1. **Best-effort QoS** on `hf_counter` — the #1 lever. Reliable QoS acks every
   sample over a confirmed XRCE stream, capping rate/adding latency; best-effort
   is fire-and-forget.
2. **QNEthernet custom UDP transport** — non-blocking lwIP, ~100 Mbit, no serial
   framing. Registered `framing=false` (packet mode).
3. **Lean hot loop** — publish directly via `rcl_publish` on a `micros()` cadence;
   the executor (1 handle) only services the command subscription. No rclc timer.
4. **Liveness ping off the hot path** — `rmw_uros_ping_agent` once/second only.
5. **rmw tuning** (`colcon.meta`) — minimal entity caps + MTU 512.
6. Reconnect state machine — boot-order-independent, survives agent restarts.

Expected: 500 Hz trivial, 1 kHz comfortable, low-kHz feasible for one small topic.
LAN UDP wire latency is sub-ms; end-to-end through WSL+DDS ~1–3 ms.

## One-time host setup (already done on this machine)

- **WSL mirrored networking** (`~/.wslconfig` → `networkingMode=mirrored`, then
  `wsl --shutdown`) so the LAN-connected Teensy can reach the agent at the host IP.
- **Firewall**: inbound rule allowing UDP 8888 (`microROS agent UDP 8888`).

## Configure the agent IP

Edit `src/main.cpp`:
```cpp
static IPAddress AGENT_IP(10, 0, 4, 244);   // = Windows host LAN IP (ipconfig)
static const uint16_t AGENT_PORT = 8888;
#define PUBLISH_HZ 1000
```
`10.0.4.244` is this host's wired LAN IP. The Teensy gets its own IP via DHCP.

## Build → flash → run

```bash
# 1. WSL: build + stage hex on Windows
bash wsl/build.sh microros_ethernet_test
```
```powershell
# 2. Windows: flash (1 UAC)
powershell -ExecutionPolicy Bypass -File flash_teensy.ps1 -Hex "C:\teensy_build\microros_ethernet_test\firmware.hex"
```
```bash
# 3. WSL: run the UDP agent (leave running)
bash wsl/run_agent_udp.sh 8888
```

Plug the Teensy into the LAN. It DHCPs, finds the agent, and starts publishing.

> Note: after flashing, `flash_teensy.ps1` re-attaches the device to WSL via usbipd
> — harmless for Ethernet (USB is only used for flashing/serial-monitor). You can
> `usbipd detach --busid 7-1` if you don't want it bound to WSL.

## Verify the rate (the goal)

`ros2 topic hz` can't measure a best-effort topic (it subscribes reliable and
receives nothing). Use the included rclpy meters, which match best-effort QoS and
report drops via the `z` sequence:

```bash
source /opt/ros/jazzy/setup.bash
# change the rate live (no reflash):
ros2 topic pub -r 10 /teensy/set_rate_hz std_msgs/msg/Int32 "{data: 5000}"
# measure received Hz + drop %:
python3 host/measure_rate_point.py /teensy/galvo_state 5
# command path:
ros2 topic pub -r 5 /teensy/command std_msgs/msg/Int32 "{data: 1}"   # LED on
```

Tip: the ros2 daemon caches a stale graph — `ros2 daemon stop`, or use
`ros2 topic list --no-daemon`.

## Measured performance (direct GbE link, this host)

### True ceiling (C++ subscriber) and payload-size effect

| Target | Point32 (12 B) drops | GalvoState (28 B) drops |
|--------|----------------------|--------------------------|
| 20 kHz | 0%                   | 0%                       |
| 30 kHz | 0.31%                | 3.76%                    |
| 40 kHz | 1.89%                | 7.95%                    |
| 50 kHz | 13.5%                | —                        |

- **C++ lossless ceiling: ~40 kHz for Point32, ~20–25 kHz for the 28-byte multi-signal
  `GalvoState`.** The receive path saturates at ~43k msg/s (agent → FastDDS, single-threaded XRCE).
- **Payload size matters past ~12 B**: 4 B→12 B was free (per-message overhead dominated),
  but the larger multi-signal message measurably lowers the message-rate ceiling.
- The Teensy still publishes its exact target up to its 50 kHz clamp — never the bottleneck.
- Cadence note: the publish loop resyncs if it falls a full period behind, so a target
  period near the loop time can't run away into a burst (an earlier `+= period` version did).

### Throughput — Python vs C++ subscriber (best-effort, 12-byte Point32)

| Target rate | Python rclpy meter | C++ meter (`teensy_bench rate_meter`) |
|-------------|--------------------|---------------------------------------|
| 5 kHz       | 0%                 | 0%                                    |
| 8 kHz       | ~0% (variable)     | 0%                                    |
| 10 kHz      | ~6.5% drops        | **0%**                                |
| 15 kHz      | —                  | **0%**                                |
| 20 kHz      | ~40% drops         | **0%**                                |

- The **Teensy is never the bottleneck** — it generates its target rate even at 20 kHz.
- The **Python subscriber** was the limiter (~12k msg/s). The **C++ subscriber is
  lossless to 20 kHz** (40× the 500 Hz goal) — the receive side, not the firmware.
- Payload size (4 B vs 12 B) barely matters; the cost is per-message overhead.

### Latency — round trip (`teensy_bench rtt_ping`, best-effort echo)

| Ping rate | median RTT | p99 RTT | ~one-way |
|-----------|-----------|---------|----------|
| 100 Hz    | 0.87 ms   | 1.60 ms | 0.45 ms  |
| 200 Hz    | 0.54 ms   | 1.17 ms | 0.29 ms  |
| 300 Hz    | 0.54 ms   | 1.18 ms | 0.30 ms  |

- **Sub-millisecond RTT (~0.5 ms), ~0.3 ms one-way, p99 < 1.2 ms, 0% loss.**
- Path: host → DDS → agent → UDP → Teensy `ping_cb` → UDP → agent → DDS → host.
- Tail can spike under heavy concurrent telemetry or WSL scheduling jitter; keep the
  telemetry rate modest when latency-characterizing.

### C++ benchmark nodes (`ros2_ws/src/teensy_bench`)

```bash
# build once
cd ros2_ws && colcon build --packages-select teensy_bench && source install/setup.bash
# throughput + drops (best_effort | reliable)
ros2 run teensy_bench rate_meter /teensy/galvo_state 5 best_effort
# round-trip latency: <count> <rate_hz>
ros2 run teensy_bench rtt_ping 2000 200
```

### Pushing even higher (if ever needed)
- Batch N samples per message (e.g. `Point32[]`) — fewer, larger packets.
- Multi-threaded agent; larger UDP socket buffers; raise `RMW_UXRCE_TRANSPORT_MTU`.
- Overclock the Teensy (`board_build.f_cpu`) — only if the firmware ever caps out.

## Tuning the rate

- Change `PUBLISH_HZ` in `main.cpp` (e.g. 2000) and re-flash.
- If `ros2 topic hz` reports less than `PUBLISH_HZ`: suspect best-effort drops
  (agent/DDS keeping up?), the 1 s ping briefly stalling the loop, or MTU. Bump
  `RMW_UXRCE_TRANSPORT_MTU` / `RMW_UXRCE_MAX_HISTORY` in `colcon.meta`.
- Overclock headroom: uncomment `board_build.f_cpu = 720000000` in platformio.ini.

## Troubleshooting connection

- Teensy LED blinking = waiting for agent (no session yet).
- Agent shows no client: check the Teensy got a DHCP IP (USB serial monitor prints
  nothing by default — add a `Serial.print(Ethernet.localIP())` if needed), that
  `AGENT_IP` matches `ipconfig`, and that the firewall rule is enabled.
- `ros2 topic hz` from WSL must be in a shell with ROS sourced.
