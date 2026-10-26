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
- Syphon.framework in `/Library/Frameworks`, `~/Library/Frameworks` or `third_party/Syphon/` ([releases](https://github.com/Syphon/Syphon-Framework/releases)). The build fails without it.
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

- **Scenes panel:** click a scene to select it; **double-click**, **Enter** or **Go Live** in the Scene panel triggers it; **Left/Right** trigger the previous/next scene. Drag rows to reorder, right-click for more. Drop `.milk` presets onto the list to add scenes (or onto a scene to replace its preset), drop a video onto a scene to use it. MIDI **CC 20** on the chosen channel triggers the scene with that CC value.
- **Scene panel:** name, CC value, preset, and video (file or Syphon sender, mix, luma gate, restart on entry). Edits apply to the running visuals immediately.
- **Presets tab** (bottom): your whole preset library, indexed in the background. Search by name, author or category, filter by the category tree, favourites (click the star) or hide broken presets. Notes flag broken presets, heavy ones (measured while playing) and missing textures. With **Previews** on, presets show as a grid of small live previews (rendered in the background, cached on disk, paused in fullscreen); off, a plain list. Double-click a preset to use it in the selected scene, or drag it onto a scene (or below the list to add a scene).
- **Setup panel:** performance name, audio input and gain, MIDI channel, preset and texture folders. Settings are saved in `~/Library/Application Support/Govtech Visualizer/settings.json`.
- **Menus:** File (New, Open, Open Recent, Save, Save As, Revert), Edit (Undo/Redo), Scene (Add, Duplicate, Delete, Go Live), View (Fullscreen). Unsaved changes are asked about on close and recovered after a crash.
- **Fullscreen:** move the window to the screen you perform on and press **Cmd+F** (or the Fullscreen button at the bottom right of the preview). Only the visuals show; **Cmd+F** or **Esc** brings the editor back.
- Logs go to stderr and to `~/Library/Logs/Govtech Visualizer/`.

### Performance files

A performance is a JSON file (format: `technical-details-2026-10-22.md` §6). To convert one from the old libprojectM-4.1.4 fork:

```sh
scripts/convert_fork_performance.py <fork>/performance.json <fork>/favourites.txt performances/my-show.json
```

`performances/govtech-2026-11-06.json` is the fork's show, converted.

### Syphon test pattern

`./build/tools/syphon_test_server/gv_syphon_test_server [seconds]` publishes a server named "GV Test" (red top half, green bottom half, a sweeping white bar), for checking Syphon input.

### MIDI test sender

`./build/tools/midi_test_sender/gv_midi_test_sender 1 3@1 5@2` creates a virtual MIDI source and sends CC 20 = 3 after a second, then 5 two seconds later, on channel 1, then removes the source.

### Show checklist

See `docs/show-checklist.md` before a performance.

### Headless smoke run

```sh
./build/src/govtech-visualizer --smoke --smoke-seconds 30 --smoke-cut-every 2 performances/govtech-2026-11-06.json
```

Plays the performance in a hidden window, cutting to the next scene every 2 seconds, then logs held frames (by cause and per scene) and each scene's video state, and exits with status 0 on success. `--smoke-snapshots DIR` saves the output just before each cut as a BMP; `--smoke-editor-snapshot FILE` saves the editor window; `--smoke-edits` makes scripted edits during playback (reorder, change the live preset, delete it, undo).

## Test

```sh
ctest --test-dir build --output-on-failure
```
