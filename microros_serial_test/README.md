# microros_serial_test — minimal micro-ROS node over serial (via WSL)

[![Overview](https://img.shields.io/badge/Overview-30363d?style=for-the-badge)](../README.md)
[![Serial Baseline](https://img.shields.io/badge/Serial_Baseline-30363d?style=for-the-badge)](../serial_client_test/README.md)
[![microROS Serial](https://img.shields.io/badge/microROS_Serial-1f6feb?style=for-the-badge)](README.md)
[![Ethernet UDP](https://img.shields.io/badge/Ethernet_UDP-30363d?style=for-the-badge)](../microros_ethernet_test/README.md)
[![QoS Results](https://img.shields.io/badge/QoS_Results-30363d?style=for-the-badge)](../microros_ethernet_test/QOS_COMPARISON.md)

A dependency-free micro-ROS node for the Teensy 4.1 that validates the full
**Teensy ↔ micro_ros_agent ↔ ROS2 (Jazzy)** path over USB serial. Counter
publisher + command subscriber, with a proper **reconnection state machine** so
boot order doesn't matter and it survives agent restarts.

| Topic            | Type            | Dir            | Notes                          |
|------------------|-----------------|----------------|--------------------------------|
| `teensy/counter` | `std_msgs/Int32`| Teensy → ROS2  | 10 Hz incrementing             |
| `teensy/command` | `std_msgs/Int32`| ROS2 → Teensy  | LSB drives onboard LED         |

## Why the build/flash split (important)

- **micro_ros_platformio cannot build on native Windows** (its build scripts use
  Unix `source`/shell). So the firmware is **built in WSL**.
- micro_ros_platformio also breaks on **spaces in the path**, so the project is
  mirrored to a space-free dir (`~/teensy/...` in WSL).
- **Teensy is flashed from Windows** (the Windows Teensy loader handles the HID
  bootloader natively; flashing from WSL is far more painful).
- WSL2 can't see COM ports, so the device is handed to WSL with **usbipd**.

## One-time setup (WSL Ubuntu 24.04 + ROS2 Jazzy)

Privileged steps (need sudo — run yourself):
```bash
# 1. core deps + rosdep
bash wsl/01_privileged_setup.sh
# 2. agent build deps
bash wsl/02_agent_deps.sh
# 3. serial access (one time)
sudo usermod -aG dialout $USER          # permanent (new sessions)
```

Unprivileged (already scripted/automated):
- PlatformIO in a venv: `python3 -m venv ~/.pio-venv && ~/.pio-venv/bin/pip install platformio`
  - symlink so micro_ros_platformio finds it: `ln -s ~/.pio-venv ~/.platformio/penv`
- Build the agent from source: `~/microros_ws` via `micro_ros_setup` (create_agent_ws + build_agent)

## Iteration loop (3 steps)

Edit `microros_serial_test/src/main.cpp`, then:

```bash
# 1. WSL: build + stage the hex on Windows
bash wsl/build.sh
```
```powershell
# 2. Windows: flash + hand back to WSL, all in ONE elevation (1 UAC click, no button)
powershell -ExecutionPolicy Bypass -File flash_teensy.ps1
```
```bash
# 3. WSL: (re)start the agent  -- udev makes /dev/ttyACM0 0666, no sudo/sg needed
bash wsl/run_agent.sh /dev/ttyACM0 115200          # leave running
```

The reconnect firmware re-establishes its session automatically once the agent is up.

### Why flashing goes through Windows (not WSL)

Flashing requires the Teensy to enter its **HalfKay bootloader**, which re-enumerates
as a different USB device (`16c0:0478`). On this machine a `CsDeviceControl` USB
security filter prevents that bootloader device from attaching into WSL, so
`teensy_loader_cli` inside WSL just hangs at "Waiting for Teensy device". Windows
flashes the bootloader natively, so `flash_teensy.ps1` does:
`unbind → teensy_post_compile -reboot → bind → attach --wsl` in a single elevated run.

## Verify

```bash
source /opt/ros/jazzy/setup.bash
ros2 topic hz   /teensy/counter                                          # ~10.00 Hz
ros2 topic echo /teensy/counter                                          # incrementing
# NOTE: use a continuous stream for commands -- a single --once can drop:
ros2 topic pub -r 5 /teensy/command std_msgs/msg/Int32 "{data: 1}"       # LED on
ros2 topic pub -r 5 /teensy/command std_msgs/msg/Int32 "{data: 0}"       # LED off
```

## Verified result (2026-06-18, USB serial via WSL)

- Session established; node `/teensy_serial_test`, topics `/teensy/counter` + `/teensy/command`
- `/teensy/counter` streaming at **10.00 Hz**, std dev 0.0003 s
- `/teensy/command` delivered to the Teensy (subscription count 1; LED toggles)

## Bus ID note

`usbipd` bus id was **7-1** on this machine — re-check with `usbipd list` if the
Teensy moves to a different port/hub.
