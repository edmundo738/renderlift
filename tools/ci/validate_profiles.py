#!/usr/bin/env python3
"""Validate RenderLift profile JSON files (CI + local, stdlib only).

Checks, per schema documented in profiles/README.md:
  * game profiles (generic/, games/): required keys, known enums, sane ranges,
    ascending deduplicated ladders contained by the display resolution
  * hardware profiles (hardware/): required keys, known classes
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
PROFILES = REPO_ROOT / "profiles"

APIS = {"auto", "d3d9", "d3d10", "d3d11", "d3d12", "vulkan"}
RECON_MODES = {"spatial", "edge", "nis", "temporal"}
INTEGRATION_MODES = {"observe", "steer", "reconstruct", "full"}
HW_CLASSES = {"integrated", "discrete"}

errors: list[str] = []


def fail(path: Path, message: str) -> None:
    errors.append(f"{path.relative_to(REPO_ROOT)}: {message}")


def resolution(value: object, path: Path, field: str) -> tuple[int, int] | None:
    if not isinstance(value, dict) or "width" not in value or "height" not in value:
        fail(path, f"{field} must be an object with width/height")
        return None
    w, h = value["width"], value["height"]
    if not (isinstance(w, int) and isinstance(h, int) and 0 < w <= 65535 and 0 < h <= 65535):
        fail(path, f"{field} has invalid dimensions ({w}x{h})")
        return None
    return w, h


def check_game_profile(path: Path, doc: dict) -> None:
    for key in ("id", "title"):
        if not doc.get(key):
            fail(path, f"missing required field '{key}'")

    api = doc.get("api", "auto")
    if api not in APIS:
        fail(path, f"unknown api '{api}'")

    display = resolution(doc.get("display"), path, "display") if "display" in doc else None

    ladder = doc.get("internalLadder", [])
    seen: list[tuple[int, int]] = []
    for i, entry in enumerate(ladder):
        r = resolution(entry, path, f"internalLadder[{i}]")
        if r is None:
            continue
        if r in seen:
            fail(path, f"internalLadder has duplicate rung {r[0]}x{r[1]}")
        seen.append(r)
        if display and r[0] * r[1] > display[0] * display[1]:
            fail(path, f"ladder rung {r[0]}x{r[1]} exceeds the display resolution")
    if any(b[0] * b[1] <= a[0] * a[1] for a, b in zip(seen, seen[1:])):
        fail(path, "internalLadder must be sorted ascending by pixel count")

    start = doc.get("defaultLevelIndex", 0)
    if not isinstance(start, int) or start < 0:
        fail(path, "defaultLevelIndex must be a non-negative integer")
    elif ladder and start >= len(ladder) + 1:  # +1: engine may append native
        fail(path, f"defaultLevelIndex {start} beyond ladder size {len(ladder)}")

    recon = doc.get("reconstruction", {})
    if not isinstance(recon, dict):
        fail(path, "reconstruction must be an object")
    else:
        if recon.get("mode", "edge") not in RECON_MODES:
            fail(path, f"unknown reconstruction mode '{recon.get('mode')}'")
        sharpening = recon.get("sharpening", 0.35)
        if not isinstance(sharpening, (int, float)) or not 0.0 <= sharpening <= 1.0:
            fail(path, f"sharpening {sharpening!r} out of [0,1]")

    integration = doc.get("integration", {})
    if not isinstance(integration, dict):
        fail(path, "integration must be an object")
    else:
        mode = integration.get("mode", "observe")
        if mode not in INTEGRATION_MODES:
            fail(path, f"unknown integration mode '{mode}'")
        frames = integration.get("observationFrames", 600)
        if not isinstance(frames, int) or frames < 0:
            fail(path, "observationFrames must be a non-negative integer")

    dyn = doc.get("dynamicResolution", {})
    if not isinstance(dyn, dict):
        fail(path, "dynamicResolution must be an object")
    elif isinstance(dyn.get("targetFps"), (int, float)) and dyn["targetFps"] <= 1:
        fail(path, "dynamicResolution.targetFps must be > 1")

    # Advisory (not an error): DRS is inert until the "full" mode.
    if (
        isinstance(integration, dict)
        and integration.get("mode", "observe") != "full"
        and isinstance(dyn, dict)
        and dyn.get("enabled") is True
    ):
        print(
            f"  · note: {path.relative_to(REPO_ROOT)}: dynamicResolution stays inert "
            f"until integration.mode='full'"
        )


def check_hardware_profile(path: Path, doc: dict) -> None:
    for key in ("id", "title", "match", "class"):
        if key not in doc:
            fail(path, f"missing required field '{key}'")
    if doc.get("class") not in HW_CLASSES:
        fail(path, f"unknown hardware class '{doc.get('class')}'")
    match = doc.get("match", {})
    if isinstance(match, dict) and "vendorId" not in match:
        fail(path, "match.vendorId is required")


def main() -> int:
    ids: set[str] = set()
    files = sorted(PROFILES.rglob("*.json"))
    if not files:
        print("no profiles found", file=sys.stderr)
        return 1

    for path in files:
        try:
            doc = json.loads(path.read_text(encoding="utf-8"))
        except json.JSONDecodeError as e:
            fail(path, f"invalid JSON: {e}")
            continue
        if not isinstance(doc, dict):
            fail(path, "root must be an object")
            continue

        pid = doc.get("id")
        if pid:
            if pid in ids:
                fail(path, f"duplicate profile id '{pid}'")
            ids.add(pid)

        folder = path.parent.relative_to(PROFILES)
        if str(folder) in {"generic", "games"} or str(folder).startswith("games"):
            check_game_profile(path, doc)
        elif str(folder) == "hardware":
            check_hardware_profile(path, doc)
        else:
            fail(path, f"unknown profile folder '{folder}'")

    if errors:
        print(f"profile validation: {len(errors)} error(s)", file=sys.stderr)
        for e in errors:
            print(f"  ✗ {e}", file=sys.stderr)
        return 1
    print(f"profile validation: {len(files)} file(s) ok ({len(ids)} unique ids)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
