# RenderLift profiles

JSON configuration consumed by ALRR Core. Three families:

| Folder | Schema | Purpose |
|---|---|---|
| `generic/` | `renderlift/game-profile@1` | fallback profile when no game match exists |
| `games/` | `renderlift/game-profile@1` | per-title tuning (exe name, pinned ladders, API) |
| `hardware/` | `renderlift/hardware-profile@1` | per-GPU-class suggestions (start tier, avoid lists) |

Validate before committing:

```bash
python tools/ci/validate_profiles.py
```

## `renderlift/game-profile@1`

```jsonc
{
  "schema": "renderlift/game-profile@1",     // required when evolving the format
  "id": "gta5",                              // required, stable, unique
  "title": "Grand Theft Auto V",             // required
  "executable": "GTA5.exe",                  // optional detection hint
  "api": "d3d11",                            // "auto"|"d3d9"|"d3d10"|"d3d11"|"d3d12"|"vulkan"
  "display":  { "width": 1366, "height": 768 },   // resolution the profile was tuned for
  "internalLadder": [                        // optional; ascending; if omitted the
    { "width": 426, "height": 240 }          // engine derives factor-based rungs
  ],
  "defaultLevelIndex": 2,                    // ladder rung used at startup
  "uiNative": true,                          // HUD/UI at native resolution
  "reconstruction": { "mode": "edge",        // "spatial"|"edge"|"nis"|"temporal"
                      "sharpening": 0.35 },  // 0..1, keep low for very low-res sources
  "dynamicResolution": { "enabled": true, "targetFps": 30,
                         "warmupFrames": 20, "cooldownFrames": 45 }
}
```

Rules the loader enforces (see `GameProfile.cpp`): required `id` + `title`,
known `api`/reconstruction modes, `sharpening ∈ [0,1]`, `targetFps > 1`.
Defaults match `generic/default.json`.

## `renderlift/hardware-profile@1`

Hardware profiles are advisory: they map a detected GPU to a suggested start
configuration. The dynamic controller adapts at runtime — a hardware profile
only chooses the *starting point*, so it's OK to be approximate.

```jsonc
{
  "schema": "renderlift/hardware-profile@1",
  "id": "intel-uhd-620",
  "title": "Intel UHD Graphics 620",
  "match": { "vendorId": "0x8086", "deviceIds": ["0x5917", "0x5916"] },
  "class": "integrated",                     // "integrated" | "discrete"
  "preferredApis": ["d3d11"],
  "suggestedStartTier": "extreme"            // generic ladder hint
}
```
