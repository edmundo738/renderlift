# Contributing to RenderLift

Thanks for helping low-end PCs punch above their weight. 🛠️

## Ground rules

- **Single-player / offline focus.** Do not submit code aimed at bypassing anti-cheat or enabling use in online modes. RenderLift targets offline games and benchmarking.
- **No game assets.** Never commit ripped game code, executables, textures, or decompiled listings. We interact with games only through public graphics APIs.
- **Engine vs product.** `RenderLift` is the product; `ALRR` is the reconstruction engine. Keep API-agnostic logic inside `src/` (ALRR Core) and API-specific interception inside `backends/`. If code includes `<d3d11.h>` or `<vulkan/vulkan.h>`, it belongs under `backends/`.
- **Third-party code** must be compatible with the MIT license, vendored under `third_party/` and recorded in `THIRD_PARTY_NOTICES.md`.

## Development setup

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug   # needs CMake ≥ 3.20, C++20 compiler
cmake --build build
ctest --test-dir build --output-on-failure
```

- Core, tests and tools build on Linux/macOS/Windows.
- Backend DLLs target Windows; they compile as platform-neutral stubs until the hook engine lands (0.2).

## Where things live

| You want to change… | Look in |
|---|---|
| dynamic resolution rules | `src/resolution/` (+ tests in `tests/`) |
| reconstruction quality/perf | `src/reconstruction/`, `shaders/` |
| game/hardware behavior | `profiles/` (JSON — validate with `python tools/ci/validate_profiles.py`) |
| per-API interception | `backends/<api>/` |
| architecture decisions | `docs/architecture/` — add an ADR for notable decisions (`docs/architecture/adr/`) |

## Pull requests

- Keep PRs focused; describe *what problem* is solved and *how you tested it*.
- Include before/after FPS or image comparisons for reconstruction changes when possible.
- Run the test suite; add tests for controller/reconstruction logic changes.
- Follow the existing style: `snake_case` files, `PascalCase` types, `camelCase` functions, namespaces under `rl::`.

## Reporting bugs

Open an issue with: game + version, GPU/driver, API used by the game, RenderLift version, resolution ladder in use, and (if possible) a log with `RENDERLIFT_LOG=debug`.
