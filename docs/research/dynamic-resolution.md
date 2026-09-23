# Dynamic resolution — controller behavior research

The internal-resolution ladder is a control system, not a slider. This note
defines *when* it moves, and — more importantly — when it must **not**.

## Inputs

`FrameSample { frameMs, gpuBusy, cpuBusy }` per frame, both busy values in
0..1 (heuristics per backend; perfect numbers aren't required — the margins
absorb noise).

## Decision table (defaults)

| Condition | Decision | Rationale |
|---|---|---|
| CPU ≥ 95% busy | **HOLD** | CPU-bound: smaller render targets don't buy frames, only quality loss |
| frame > 115% of budget **and** GPU ≥ 97% busy | step **down** | real GPU saturation |
| frame < 85% of budget **and** GPU ≤ 75% busy | step **up** | quality headroom |
| frame slow but GPU 75–97% | HOLD | ambiguous (pacing/vsync/fence stalls) — don't guess |
| otherwise | HOLD | within tolerance |

## Anti-pumping measures

- **Warmup** (default 20 frames): load screens / shader compilation spikes
  are ignored before any decision counts.
- **Cooldown** (default 45 frames ≈ 1.5 s @30 fps): minimum frames between
  ladder steps; a user override (`setLevel`) also resets the cooldown so the
  controller never immediately fights the user.
- **One rung per decision**: no “panic drops” across multiple rungs; repeated
  saturated frames still reach the floor quickly (worst case: 3 changes × 45
  frames ≈ 4.5 s from 854×480 to 426×240).

## Why the CPU rule is the product differentiator

On low-end APUs (UHD 620 + 15 W mobile CPU), GTA V's streaming/AI keep the
CPU near saturation in traffic. A naive dynamic-resolution system reads
"slow frames → downscale", produces a smaller image the CPU still can't feed,
and the user sees *blurrier* frames at the *same* FPS. RenderLift refuses
that trade. Detecting it is the reason `cpuBusy` is a first-class input.

## Tests as the spec

`tests/test_dynamic_resolution.cpp` encodes the table above plus the
hysteresis rules — behavior changes to the controller must come with test
updates in the same commit. The CLI simulator
(`RenderLift.CLI simulate <profile>`) replays the three canonical phases
(GPU-bound → CPU-bound → headroom) on any profile.
