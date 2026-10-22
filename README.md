# Govtech Visualizer

A VJ / visual performance designer for macOS, built on the projectM (MilkDrop) renderer.

Planning documents:
- `product-2026-10-22.md`: what we're building.
- `technical-details-2026-10-22.md`: technical decisions and their reasons.
- `implementation-plan-2026-10-22.md`: milestones.

## Requirements

- macOS 12 or later (developed on Apple Silicon).
- Xcode (or the Command Line Tools) and CMake 3.24+: `brew install cmake`.
- SDL2: `brew install sdl2`.
- Syphon.framework in `/Library/Frameworks`, `~/Library/Frameworks` or `third_party/Syphon/` (needed from milestone 3).
- Optional: Ninja (`brew install ninja`) for faster builds; add `-G Ninja` to the configure command.

Dear ImGui, nlohmann/json and Catch2 are downloaded at configure time (pinned in `cmake/Dependencies.cmake`).

## Build

```sh
git clone --recursive https://github.com/kurtvan/govtech-visualizer.git
cd govtech-visualizer
cmake -S . -B build
cmake --build build -j
```

If you cloned without `--recursive`: `git submodule update --init --recursive`.

projectM is built from a patched copy of the `third_party/projectm` submodule. See `third_party/patches/README.md`.

## Run

```sh
./build/src/govtech-visualizer
```

Logs go to stderr and to `~/Library/Logs/Govtech Visualizer/`.

`--smoke-frames N` renders N frames in a hidden window, logs the result and exits (for automated checks).

## Test

```sh
ctest --test-dir build --output-on-failure
```
