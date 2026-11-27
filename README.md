# Zebra

A live visuals app for macOS. You build each scene as a node graph: Milkdrop presets, videos and ISF shaders, wired through effects, and moved by LFOs and live audio. Then you cut between scenes during a show, from the keyboard, a MIDI controller or Ableton's clock.

<!-- Screenshot / demo video goes here. -->

## What makes it different

- **Milkdrop, video and shaders in one graph.** Milkdrop presets, videos (from files or Syphon) and ISF shaders are all nodes you wire together. A video or shader can go *inside* a preset, through a patched projectM: into its feedback, so the preset warps and smears it; before or after its composite; or in place of one of its textures. A video or shader can also drive the output on its own, with no preset.
- **Every slider can be moved by the music.** Any slider takes a modulation wire. Audio In → Band (a frequency range you choose, e.g. just the kick) → Envelope gives a number that follows that part of the mix. LFOs follow Ableton's MIDI clock. Number nodes can drive other number nodes, so an Envelope can speed up an LFO.
- **Audio flow you can see.** Each preset hears only the audio wired into it, so two presets in one scene can follow the bass and the hats.
- **Most ISF shaders just work.** Load filters and generators from VDMX, Resolume or isf.video, including multi-pass simulations (slime mould, fluids, reaction-diffusion) with persistent buffers and a per-node resolution setting. Save a `.fs` file in your editor and it updates live, even on air; a broken save keeps the last working version on screen.
- **Cuts with no wait.** Upcoming scenes are compiled and warmed in the background on separate render lanes, so a cut never waits for a preset to load. Changes on the clock land on bar lines.
- **Made for a live show.** Scenes go live from MIDI CC, timings follow Ableton's clock, and Cmd+F gives a clean fullscreen output with no menu bar or Dock.

## Quick start

1. **Build it** (see [Build](#build) below), then run:
   ```sh
   ./build/src/govtech-visualizer --performance ~/Shows/my-show.json
   ```
   Leave out `--performance` to open the last show, or start a new one from **File > New**.
2. **Setup** (right panel): pick your audio input, MIDI channel, and your preset and texture folders.
3. **Add a scene** (Shift+Cmd+N), then build it in the **Graph** panel. Right-click the canvas to add nodes, and drag from pin to pin to wire them.
4. **Go live:** double-click a scene, press Enter, or send its MIDI CC. Press **Cmd+F** for fullscreen output.

## Using it

### The window

| Panel | What it's for | Show/hide |
|---|---|---|
| **Scenes** (left) | Your show's running order. Double-click or press Enter to go live, use Left/Right for the previous/next scene, drag to reorder. | Cmd+1 |
| **Scene / Setup** (right) | The selected scene's name and MIDI CC; the show's audio, MIDI and folders. | Cmd+2 |
| **Graph** (centre) | The selected scene's nodes and wires. | |
| **Preview** | What's going out. **Preview behind** shows it under the graph. | |
| **Presets / Videos / Nodes** (bottom) | Your preset library, the show's video pool, and a list of the scene's nodes. | Cmd+3 |

Cmd+0 hides every panel except the Preview and the Graph.

### Building a scene

Every scene ends in a **Viewport** node: whatever is wired into it is what the audience sees.

| Node | Does |
|---|---|
| **Milkdrop preset** | Renders a `.milk` preset. Its inputs take a picture into its **Feedback**, **Before composite** or **On top** layers, or in place of one of its **textures**, plus the **Audio** it listens to. |
| **Video** | A file from the show's video pool, or a Syphon sender. |
| **ISF shader** | A `.fs` filter or generator. Its sliders come from the file. It reloads when you save the file. |
| **Contrast, Saturation, Luma gate, Opacity** | Built-in effects on a picture. |
| **Switcher** | Cycles through its inputs every N seconds, beats or bars, in order or shuffled. Double-click an input to jump to it. |
| **LFO** | A sine, triangle, saw or square wave, synced to the MIDI clock or free-running. |
| **Audio In → Band → Envelope** | The show's audio input, optionally narrowed to a frequency band, turned into a number that follows its loudness. |

**Wires.** You can only connect wires of the same kind, and a wire that isn't allowed is refused with a reason.

| Colour | Carries |
|---|---|
| Blue (round pins) | Pictures |
| Orange (diamond pins) | Numbers, to modulate a slider |
| Green (square pins) | Audio |

**Modulation:** wire a number into the diamond at the start of any slider's row. The slider stays where you left it, as the centre of the movement, and the row's **depth** sets how far it swings.

**Presets hear only the audio wired into them.** An unwired preset is still. New presets arrive already wired to the scene's Audio In.

**Canvas controls:**
- Scroll to pan, and pinch or Cmd+scroll to zoom.
- **F** fits the view to the graph.
- **Arrange** lays the scene out from its wires.
- Wires route around nodes like circuit traces, and several wires into one pin spread out across a pill-shaped pin so you can follow each one.

### Library and videos

- **Presets tab:** your whole preset library, indexed in the background, shown as a grid of small live previews. Presets that can't hold 60 fps on your machine are flagged from their measured frame time, and so are presets with missing textures. Double-click a preset to put it in the live scene.
- **In-node browser:** a preset node can open the library in place, so you can swap its preset without leaving the canvas.
- **Videos tab:** the show's shared video pool. Each video has a start point, speed (0.1x–4x) and fit (Fill, Fit or Stretch). Scenes that use the same video share one player, so it carries on across cuts.

### Performing

- **Going live:** each scene is triggered by **MIDI CC 20** on your channel, with the scene's CC value. **Renumber CCs** numbers them in list order.
- **MIDI clock:** beat and bar timings follow Ableton's MIDI clock, and changes land on bar lines.
- **Cmd+F** shows only the visuals, fullscreen on the screen the window is on. Press Esc or Cmd+F to come back. Move the window to your output screen first.
- **Before a show,** go through [the show checklist](docs/2026-10-25-show-checklist.md).

### Files

- **Shows** are JSON files. Keep them outside this repo: `performances/` is ignored.
- **Settings:** `~/Library/Application Support/Govtech Visualizer/settings.json`
- **Logs:** `~/Library/Logs/Govtech Visualizer/`
- **Converting a show from the old libprojectM fork:**
  ```sh
  scripts/convert_fork_performance.py <fork>/performance.json <fork>/favourites.txt ~/Shows/my-show.json
  ```

## For developers

### Build

Requirements: macOS 12+ (developed on Apple Silicon), Xcode or the Command Line Tools, CMake 3.24+ and SDL2 (`brew install cmake sdl2`). You also need **Syphon.framework** in `/Library/Frameworks`, `~/Library/Frameworks` or `third_party/Syphon/` ([releases](https://github.com/Syphon/Syphon-Framework/releases)). The build fails without it.

```sh
git clone --recursive https://github.com/kurtvan/govtech-visualizer.git
cd govtech-visualizer
cmake -S . -B build          # add -G Ninja for faster builds
cmake --build build -j
```

- **Submodules:** if you cloned without `--recursive`, run `git submodule update --init --recursive`. New git worktrees need this too.
- **projectM** is built from a patched copy of the `third_party/projectm` submodule; see `third_party/patches/README.md`.
- **Other dependencies:** Dear ImGui, imgui-node-editor, nlohmann/json and Catch2 are downloaded at configure time, with versions pinned in `cmake/Dependencies.cmake`.
- **Tests:** add `-DGV_BUILD_TESTS=OFF` to skip building them.

### Test

```sh
ctest --test-dir build --output-on-failure
```

The GL tests open a hidden OpenGL context. Each node type has its own test file in `tests/nodes/`.

### Headless smoke run

```sh
./build/src/govtech-visualizer --smoke --smoke-seconds 30 --smoke-cut-every 2 --performance ~/Shows/my-show.json
```

This plays the show in a hidden window, cutting every 2 seconds, then logs held frames and exits with status 0 on success.

| Flag | Does |
|---|---|
| `--smoke-random` | Cuts in random order. |
| `--smoke-snapshots DIR` | Saves the output before each cut. |
| `--smoke-editor-snapshot FILE` | Saves the editor window. |
| `--smoke-edits` | Makes scripted edits during playback. |
| `--smoke-no-editor` | Runs without the editor. |

**Never run smoke tests against your real show or settings.** Use a copy, and point `CFFIXED_USER_HOME` at a scratch folder.

### Tools

- **Syphon test pattern:** `./build/tools/syphon_test_server/gv_syphon_test_server [seconds]` publishes "GV Test" for checking Syphon input.
- **MIDI test sender:** `./build/tools/midi_test_sender/gv_midi_test_sender 1 3@1 5@2` sends CC 20 = 3, then 5, on channel 1. Use `clock:128:8` to send 8 seconds of clock at 128 BPM.

### Code map

| Path | What's there |
|---|---|
| `src/model/` | The show, scenes and graph, with no GL. Each node type is defined once in `model/nodes/<Type>Node.cpp`. |
| `src/engine/` | Render plans, lanes (projectM instances), scene preparation, audio, video and ISF rendering. |
| `src/isf/` | The ISF parser, size expressions and multi-pass buffers. |
| `src/app/` | The ImGui shell, the graph editor and each node's canvas UI (`app/nodes/`). |
| `tests/` | Catch2 tests, mirroring `src/`. |
| `docs/` | Product and technical design docs, named by date. |

### Contributing

Work is tracked in GitHub Issues and on the [project board](https://github.com/users/kurtvan/projects/1). See `docs/issues/README.md` for how tickets work, and `docs/issues/AGENTS.md` for the rules agents follow. Keep each PR to one ticket: about 500 changed lines including tests, and 1,000 at most.
