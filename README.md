# teensy-ethernet — Teensy 4.1 micro-ROS peripheral (serial → Ethernet/UDP)

[![Overview](https://img.shields.io/badge/Overview-1f6feb?style=for-the-badge)](README.md)
[![Serial Baseline](https://img.shields.io/badge/Serial_Baseline-30363d?style=for-the-badge)](serial_client_test/README.md)
[![microROS Serial](https://img.shields.io/badge/microROS_Serial-30363d?style=for-the-badge)](microros_serial_test/README.md)
[![Ethernet UDP](https://img.shields.io/badge/Ethernet_UDP-30363d?style=for-the-badge)](microros_ethernet_test/README.md)
[![QoS Results](https://img.shields.io/badge/QoS_Results-30363d?style=for-the-badge)](microros_ethernet_test/QOS_COMPARISON.md)

Prototypes for a high-performance Teensy 4.1 peripheral talking to ROS2 (Jazzy)
over micro-ROS — built up from a plain-serial baseline to a tuned Ethernet/UDP
node benchmarked well past the original >500 Hz goal.

## Layout

| Path | What |
|------|------|
| [`serial_client_test/`](serial_client_test/README.md) | Standalone serial baseline (**no ROS**) — Arduino sketch + Python host client for latency/throughput sanity checks |
| [`microros_serial_test/`](microros_serial_test/README.md) | Minimal micro-ROS node over USB serial, with a reconnecting state machine |
| [`microros_ethernet_test/`](microros_ethernet_test/README.md) | **High-performance micro-ROS over Ethernet/UDP** — QNEthernet custom transport, best-effort + reliable QoS, runtime rate / QoS / payload control |
| [`bcc_board_reference_ros2/`](bcc_board_reference_ros2/) | Reference BCC hardware-interface node (galvo / trigger / clutch) |
| [`ros2_ws/src/teensy_msgs/`](ros2_ws/src/teensy_msgs/) | Custom multi-signal message `GalvoState` |
| [`ros2_ws/src/teensy_bench/`](ros2_ws/src/teensy_bench/) | C++ benchmark nodes: `rate_meter`, `rate_meter_full`, `rtt_ping` |
| [`wsl/`](wsl/) | WSL helper scripts — privileged setup, build, run agent |
| [`flash_teensy.ps1`](flash_teensy.ps1) | One-elevation Windows flash + usbipd re-attach |

## Toolchain notes

- Firmware is built with **PlatformIO inside WSL** — `micro_ros_platformio` can't build
  on native Windows and chokes on spaces in paths, so builds run from a space-free WSL dir.
- Flashed **from Windows** (`flash_teensy.ps1`); the device is handed to WSL via **usbipd**
  for serial, or reached directly over the LAN for Ethernet/UDP.
- micro-ROS **agent** built from source in WSL; ROS2 **Jazzy** on Ubuntu 24.04.
- WSL uses **mirrored networking** so a LAN/direct-link device reaches the agent.

Each subproject has its own README with build / flash / run / verify steps.

## Headline results (direct Gigabit link, C++ subscriber)

- **Lossless to ~40 kHz** (12-byte `Point32`) and **~20–25 kHz** (28-byte multi-signal `GalvoState`).
- **Round-trip latency ~0.5 ms** (≈0.3 ms one-way).
- **Reliable QoS**: zero loss, ~2.8 kHz cap (use for commands). **Best-effort**: high rate,
  occasional loss at the edge (use for telemetry).
- Bottleneck at the top end is the micro-ROS **agent** (~43k msg/s), not the Teensy.
- ≥ 40× the original >500 Hz target.

See [microros_ethernet_test/README.md](microros_ethernet_test/README.md) and
[QOS_COMPARISON.md](microros_ethernet_test/QOS_COMPARISON.md) for the full measurements.
