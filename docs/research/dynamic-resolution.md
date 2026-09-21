# Dynamic resolution

Dynamic resolution is primarily a GPU-side optimization.

Lower resolution when GPU utilization is high and frame time is materially above target, provided the CPU is not the dominant bottleneck.

Hold when the CPU is saturated, when the system is balanced, or while hysteresis/cooldown is active.

Increase resolution when the GPU has clear headroom and frame time is comfortably below target.

Thresholds in the prototype are starting points, not benchmark conclusions.
