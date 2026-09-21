# ALRR pipeline

## Spatial

Lowest-cost reconstruction. Start with stable sampling and controlled sharpening.

## Edge

Use local gradients and edge-aware weighting to preserve important geometry at aggressive internal resolutions.

## Temporal

Future stage. It requires trustworthy frame history and, ideally, motion information. Previous frames must never be treated as valid temporal data without lifecycle validation.

## UI

Preferred architecture: expensive 3D at internal resolution, reconstruction to display resolution, and native-resolution UI composition where the game's renderer permits it.
