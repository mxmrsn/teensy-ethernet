# QoS performance: best-effort vs reliable (Teensy 4.1 micro-ROS over UDP)

Measured on the direct Gigabit link, `geometry_msgs/Point32` (12 B) payload,
5 s windows, drop detection via the monotonic sequence in `.z`. One firmware,
two publishers, `teensy/set_mode` to isolate each QoS (no cross-load):

- `teensy/galvo_state`     — best-effort (`rclc_publisher_init_best_effort`)
- `teensy/galvo_state_rel` — reliable    (`rclc_publisher_init_default`)

Reliable XRCE stream window: `RMW_UXRCE_STREAM_HISTORY=16`.

## Results

| Target rate | Best-effort: received / drops | Reliable: received / drops |
|-------------|-------------------------------|----------------------------|
| 1 kHz       | 1000 Hz / 0%                  | ~1020 Hz / ~0%             |
| 2 kHz       | 2000 Hz / 0%                  | 2002 Hz / **0%**           |
| 3 kHz       | ~2824 Hz / ~6% *(varies)*     | **capped ~2837 Hz / 0%**   |
| 5 kHz       | ~5021 Hz / ~5.6% *(varies)*   | **capped ~2820 Hz / 0%**   |

Best-effort on a *quiet* receive pipeline was lossless to ~5–8 kHz (see the
single-QoS sweep); under load it drops a few %. Best-effort loss is **variable**.

## What the numbers mean

**Best-effort** = fire-and-forget. The Teensy sends every sample; the transport
makes no delivery guarantee. Losses happen on the **receive side** (agent →
FastDDS → subscriber through WSL) when it can't keep up. Lowest latency, highest
throughput, but you will occasionally miss samples — and how many depends on
host load, so it's not deterministic.

**Reliable** = acknowledged, in-order delivery over a bounded XRCE stream window.
When the Teensy tries to publish faster than acknowledgements return, the stream
buffer fills and `rcl_publish` **fails on the Teensy side** — the sample is never
put on the wire. So:

- Whatever is delivered is **guaranteed, in order, 0% loss**.
- The effective rate **hard-caps** (here ~2.8 kHz) — beyond that the firmware
  back-pressures (publish failures) instead of flooding the network. Note the
  "capped" rows: targeting 3 k or 5 k both land at ~2.8 kHz.
- Higher latency (each sample waits on ack round-trips).

In short: under rate pressure, **best-effort drops data; reliable drops rate.**

## The reliable ceiling is tunable

~2.8 kHz is a function of the ack round-trip × the stream window
(`RMW_UXRCE_STREAM_HISTORY`, currently 16). A larger window allows more
in-flight unacked samples → higher reliable ceiling, at the cost of RAM and
worst-case latency. Raise `RMW_UXRCE_STREAM_HISTORY` in `colcon.meta` and rebuild
to push it up. Best-effort has no such window and isn't affected.

## Which to use

| Use case | QoS |
|----------|-----|
| High-rate telemetry / sensor streaming (galvo position, IMU, ADC) | **best-effort** — newest sample matters most; an occasional miss is fine |
| Commands, setpoints, mode/state changes, e-stop | **reliable** — must not be lost or reordered (low rate, well within the cap) |
| Bulk/critical data that must all arrive | **reliable**, and keep the rate under the window-limited ceiling |

This is exactly how the firmware is set up: `galvo_state` telemetry is
best-effort; `command` / `set_rate_hz` / `set_mode` are reliable.

## Caveats

- Best-effort drop % is load-dependent and varied run-to-run (0% quiet, ~5% busy).
- Measured through WSL2 (mirrored networking) + the Python rclpy subscriber, which
  is itself a receive-side limiter. A C++ subscriber would raise the best-effort
  ceiling; it would not change the reliable window cap (that's set by the XRCE
  stream + ack RTT, on the Teensi/agent side).
- Latency was not measured here, only throughput/loss. Reliable is higher-latency
  by construction.
