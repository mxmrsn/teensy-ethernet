# Update rate & latency vs host CPU load

[![Overview](https://img.shields.io/badge/Overview-30363d?style=for-the-badge)](../README.md)
[![Serial Baseline](https://img.shields.io/badge/Serial_Baseline-30363d?style=for-the-badge)](../serial_client_test/README.md)
[![microROS Serial](https://img.shields.io/badge/microROS_Serial-30363d?style=for-the-badge)](../microros_serial_test/README.md)
[![Ethernet UDP](https://img.shields.io/badge/Ethernet_UDP-30363d?style=for-the-badge)](README.md)
[![QoS Results](https://img.shields.io/badge/QoS_Results-30363d?style=for-the-badge)](QOS_COMPARISON.md)
[![Load Test](https://img.shields.io/badge/Load_Test-1f6feb?style=for-the-badge)](LOAD_TEST.md)

How does the Teensy→ROS2 update rate hold up when the host is busy (e.g. 2 cameras
@ 60 fps 1080p + OpenCV + galvo control)? This characterizes the **shape** of the
degradation over the Ethernet/UDP path.

## Method

`host/loadsweep.sh` pegs N of the 16 cores with busy loops (`yes`, sudo-free),
then measures the best-effort `galvo_state` rate (`rate_meter`) and round-trip
latency (`rtt_ping`) at each load level. The agent **and** the meters compete on
the same cores — same topology as production. Synthetic, uniform CPU load.

```bash
bash host/loadsweep.sh 1000  "0 8 15"     # control-rate
bash host/loadsweep.sh 10000 "0 8 15"     # stress
```

## Results (UDP best-effort, direct GbE, 16-core host)

### 1 kHz — control-rate target

| Host load   | Recv rate | Drops  | RTT p50 | RTT p99 | Ping loss |
|-------------|-----------|--------|---------|---------|-----------|
| idle        | 1000 Hz   | 0%     | 0.50 ms | 1.23 ms | 0%        |
| 8/16 cores  | 1000 Hz   | 0.02%  | 0.75 ms | 1.43 ms | 0%        |
| 15/16 cores | 1001 Hz   | 0.15%  | 0.93 ms | 5.40 ms | 0%        |

### 10 kHz — stress

| Host load   | Recv rate | Drops | RTT p50 | RTT p99  |
|-------------|-----------|-------|---------|----------|
| idle        | 9998 Hz   | 0%    | 0.67 ms | 1.35 ms  |
| 8/16 cores  | 9997 Hz   | ~0%   | 0.81 ms | 2.77 ms  |
| 15/16 cores | 9152 Hz   | 8.2%  | 1.46 ms | 10.7 ms  |

## Interpretation

- **1 kHz is effectively bulletproof** — 0.15% drops with 15/16 cores pegged,
  median RTT still sub-millisecond. A control loop here has huge margin.
- **Degradation is graceful, not a cliff.** Heavy load costs a small % of samples
  and some tail latency — not a rate collapse. (Serial, by contrast, drops to
  ~100 Hz under load because the Teensy's USB-CDC TX times out when the host can't
  drain the port — UDP has no such coupling; the kernel socket buffer absorbs it.)
- **The tail moves, the median mostly doesn't.** p99 RTT climbs (1.2 → 5–11 ms);
  p50 stays ~1 ms. Expect *occasional* multi-ms spikes under load, not sustained slowdown.
- **10 kHz** holds lossless through 8 cores; only at near-total saturation does it
  shed ~8%.

## Caveats

- Synthetic uniform CPU load. Real cameras + OpenCV add **memory-bandwidth
  contention, IRQ load, and burstiness** not captured here — could be somewhat worse.
- Linux CFS shares fairly, so even at 15/16 the agent kept ~1 core. Aggressive
  core-pinning by the CV stack could starve it more — so **pin the agent/control
  node to dedicated cores** in production.
- The authoritative number comes from running `rate_meter` on the **real rig with
  the cameras live**.

## Hardening the tail under load

1. **Core affinity** — pin cameras/OpenCV to one core set, agent + control node to another.
2. **Separate the Teensy subscriber from the CV loop** (dedicated thread / executor)
   so callbacks aren't gated by frame processing.
3. Raise `net.core.rmem_max` / `rmem_default` so bursts never overflow the socket buffer.
4. Best-effort for telemetry, reliable for commands (already the default).
