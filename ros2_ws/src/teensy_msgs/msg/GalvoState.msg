# Multi-signal galvo/board state -- one realistic high-rate telemetry sample.
uint32  seq        # monotonic sequence (drop detection)
float32 galvo_x    # galvo X position / feedback
float32 galvo_y    # galvo Y position / feedback
float32 trigger    # laser trigger level
float32 temp_c     # board temperature
int32   encoder    # encoder count
uint8   clutch     # clutch engaged
uint8   status     # status / fault bits
