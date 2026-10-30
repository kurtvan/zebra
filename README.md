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

- **Scenes panel:** click a scene to select it; **double-click**, **Enter** or **Go Live** in the Scene panel triggers it; **Left/Right** trigger the previous/next scene. Drag rows to reorder, right-click for more. Drop `.milk` presets onto the list to add scenes (or onto a scene to replace its preset), drop a video onto a scene to use it (it's added to the video pool). MIDI **CC 20** on the chosen channel triggers the scene with that CC value.
- **Scene panel:** name, CC value, preset, and video (a pool video or Syphon sender, mix, luma gate, contrast, saturation, restart on entry, loop: off plays a file video to the end once and holds its last frame until a scene that restarts it, or its video playlist, starts it again). Tick **Playlist** to make a scene cycle through several presets every N seconds, beats or bars (beats and bars follow Ableton's MIDI clock and only ever change on a bar line: 6 beats becomes 2 bars, and a preset that isn't ready in time waits for the next bar), in order or shuffled; drag presets from the browser onto its list. **Double-click** a preset in the list to go live on it (the cycle carries on from there). For a file video, tick **Playlist of videos** to cycle through several pool videos the same way (its own interval, in order or shuffled; each video plays from its start point, and the next one waits ready so the change is a clean cut); drag videos onto its list from the Videos tab, or use **Add from pool**. Tick **Invert luma gate** to keep the video's dark parts instead of its bright ones (for the whole scene). Tick **Overlay on top** to also draw the video crisp over the visuals wherever it's brighter than the luma gate (Mix is its opacity, **Softness** feathers the edge); it still feeds the preset underneath. Each playlist preset has its own video look (mix, luma gate, contrast, saturation, overlay): click a preset in the list and the Video sliders edit its look. Edits apply to the running visuals immediately.
- **Presets tab** (bottom): your whole preset library, indexed in the background. Search by name, author or category, filter by the category tree, favourites (click the star) or hide broken presets. Notes flag broken presets, heavy ones (measured while playing) and missing textures. With **Previews** on, presets show as a grid of small live previews (rendered in the background, cached on disk, paused in fullscreen); off, a plain list. Double-click a preset, or drag it onto the preview, to put it in the live scene right away (in a playlist scene it replaces the preset on screen); this edits the show and can be undone. Nothing happens when nothing is live. Drag a preset onto a scene to use it there (or below the list to add a scene); right-click for **Use in Selected Scene** (or **Add to Selected Scene's Playlist**).
- **Videos tab** (bottom): the performance's shared pool of video files. Set each video's **start** point (where it starts, restarts and loops back to) and **speed** (0.1x to 4x), choose its **fit** (Fill crops to cover the output, Fit shows the whole picture with black bars, Stretch squashes it to the output's shape), see its length and current position, and which scenes use it. Add videos with **Add Video...** or by dropping files on the tab; drag a row onto a scene to use it. Scenes using the same video share one player: a later scene carries on from where the video was, unless the scene has **Restart video when the scene starts** ticked. Only videos on screen play. Older performance files are converted on load (one pool video per file). Vertical phone videos play upright: the rotation the phone recorded is applied.
- **Setup panel:** performance name, audio input and gain, MIDI channel, preset and texture folders. Settings are saved in `~/Library/Application Support/Govtech Visualizer/settings.json`.
- **Menus:** File (New, Open, Open Recent, Save, Save As, Revert), Edit (Undo/Redo), Scene (Add, Duplicate, Delete, Go Live), View (Fullscreen). Unsaved changes are asked about on close and recovered after a crash.
- **Fullscreen:** two kinds. macOS full screen (green button, Globe+F or Ctrl+Cmd+F) makes the whole app full screen with the editor showing. **Cmd+F** (View > Fullscreen Visuals, or the Fullscreen button at the bottom right of the preview) shows only the visuals, with the menu bar and Dock kept hidden even at the screen edge; from a normal window it goes into macOS full screen too. **Esc** or **Cmd+F** brings the editor back (and leaves macOS full screen again if Cmd+F entered it). Move the window to the screen you perform on first.
- Logs go to stderr and to `~/Library/Logs/Govtech Visualizer/`.

### Performance files

A performance is a JSON file (format: `technical-details-2026-10-22.md` §6). To convert one from the old libprojectM-4.1.4 fork:

```sh
scripts/convert_fork_performance.py <fork>/performance.json <fork>/favourites.txt ~/Shows/my-show.json
```

Keep show files outside the repo (`performances/` is ignored, so a show saved there is never committed).

### Syphon test pattern

`./build/tools/syphon_test_server/gv_syphon_test_server [seconds]` publishes a server named "GV Test" (red top half, green bottom half, a sweeping white bar), for checking Syphon input.

### MIDI test sender

`./build/tools/midi_test_sender/gv_midi_test_sender 1 3@1 5@2` creates a virtual MIDI source and sends CC 20 = 3 after a second, then 5 two seconds later, on channel 1, then removes the source. `clock:128:8` sends Start, 8 seconds of MIDI clock at 128 BPM, then Stop.

### Show checklist

See `docs/show-checklist.md` before a performance.

### Headless smoke run

```sh
./build/src/govtech-visualizer --smoke --smoke-seconds 30 --smoke-cut-every 2 ~/Shows/my-show.json
```

Plays the performance in a hidden window, cutting to the next scene every 2 seconds, then logs held frames (by cause and per scene) and each scene's video state, and exits with status 0 on success. `--smoke-snapshots DIR` saves the output just before each cut as a BMP; `--smoke-editor-snapshot FILE` saves the editor window; `--smoke-edits` makes scripted edits during playback (reorder, change the live preset, delete it, undo).

## Test

```sh
ctest --test-dir build --output-on-failure
```
