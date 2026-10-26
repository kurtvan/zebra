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
./build/src/govtech-visualizer [performance.json]
```

- **Setup panel:** the audio input and gain, the MIDI channel, and the preset and texture folders. Settings are saved in `~/Library/Application Support/Govtech Visualizer/settings.json`.
- **Fullscreen:** move the window to the screen you perform on and press **Cmd+F** (or the Fullscreen button at the bottom right of the preview). Only the visuals show; **Cmd+F** or **Esc** brings the editor back.
- **Scenes panel:** click a scene, or use Left/Right, to trigger it. MIDI **CC 20** on the chosen channel triggers the scene whose CC value matches. Badges show heavy presets and each scene's drop rate.
- Logs go to stderr and to `~/Library/Logs/Govtech Visualizer/`.

### Performance files

A performance is a JSON file (format: `technical-details-2026-10-22.md` §6). To convert one from the old libprojectM-4.1.4 fork:

```sh
scripts/convert_fork_performance.py <fork>/performance.json <fork>/favourites.txt performances/my-show.json
```

`performances/govtech-2026-11-06.json` is the fork's show, converted.

### Headless smoke run

```sh
./build/src/govtech-visualizer --smoke --smoke-seconds 30 --smoke-cut-every 2 performances/govtech-2026-11-06.json
```

Plays the performance in a hidden window, cutting to the next scene every 2 seconds, then logs held frames (by cause and per scene) and exits with status 0 on success.

## Test

```sh
ctest --test-dir build --output-on-failure
```
