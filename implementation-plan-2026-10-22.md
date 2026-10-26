# Govtech Visualizer: Implementation Plan

*Written 2026-10-22. **Status: awaiting approval. Do not start implementation until the author approves this plan.***

Read first: `product-2026-10-22.md` (what) and `technical-details-2026-10-22.md` (decisions and why). Where this plan refines the technical doc, it says so explicitly and the technical doc has been updated to match.

---

## 0. Guiding principles

1. **Build everything, in dependency order.** All milestones M0–M7 are in scope and are implemented back to back. The order means a show-capable build exists from the end of M3 (it plays a performance from a JSON file with MIDI, video and instant cuts); M4–M6 then add authoring, and M7 hardens it.
2. **Kill the biggest risk first.** Instant cuts depend on compiling presets without stalling the render thread (milestone 1).
3. **Keep the boundaries the technical doc requires:** the engine has no UI knowledge; UI code makes no GL calls except displaying textures through ImGui; platform APIs stay behind interfaces; `model/` and `library/` are pure C++.
4. **Every milestone ends in a runnable state**, with stated acceptance criteria and a commit (or a small series of commits) on `main`, or on a feature branch if the author prefers.

---

## 1. Toolchain & conventions

| Item | Choice |
|---|---|
| Language | C++20 for core; Objective-C++ (`.mm`, ARC on) only inside `io/macos/` and small `app/macos/` shims. |
| Build | CMake ≥ 3.24, Ninja. One top-level `CMakeLists.txt`. Targets: `gv_model`, `gv_library`, `gv_io`, `gv_engine` (static libs), `govtech-visualizer` (app), `gv_tests`, `gv_spike_compile` (milestone 1 tool). |
| Platform | macOS (Apple Silicon first; Intel not tested); deployment target macOS 12+. OpenGL 4.1 core profile. |
| projectM | Submodule `third_party/projectm` @ `v4.1.8`, built **from a patched copy** (§1.1). `ENABLE_PLAYLIST=OFF`, `ENABLE_SDL_UI=OFF`, `ENABLE_SYSTEM_PROJECTM_EVAL=OFF` (use the vendored copy; it defaults to ON), `BUILD_TESTING=OFF`. Shared library (keeps LGPL compliance simple later). |
| Dependencies | SDL2 2.30+ from Homebrew (`find_package`; changed in M0 from FetchContent because Homebrew SDL2 is already the known-good setup). `FetchContent`, pinned exact tags: Dear ImGui **docking branch** (pin a specific `vX.Y-docking` tag), nlohmann/json (3.11.x), Catch2 v3 (tests). Pin the exact versions in milestone 0 and record them in `CMakeLists.txt`. |
| Syphon | `Syphon.framework` built from the Syphon-Framework source (pinned tag) or a prebuilt release, placed in `third_party/Syphon/`. The build fails with a clear message if it's missing. |
| Format/lint | `.clang-format` (based on projectM's style so patches read consistently); `-Wall -Wextra` on our targets. |
| Logging | A small `log.hpp` (levels, timestamp, category) writing to stderr plus a rotating file in `~/Library/Logs/Govtech Visualizer/`. No `printf` sprinkled around. |
| App name | A single CMake variable `GV_APP_NAME="Govtech Visualizer"` → generated `app_info.hpp` (window titles, Application Support / Logs folders, bundle ID `com.govtech.visualizer`). |

### 1.1 Applying projectM patches (refines technical doc §0)
- At configure time CMake copies `third_party/projectm` into `${CMAKE_BINARY_DIR}/projectm-src` (FetchContent with a local `SOURCE_DIR` + `PATCH_COMMAND`, or an equivalent `execute_process`), then runs `git apply` for each `third_party/patches/*.patch` in filename order. **The submodule's working tree is never modified**, so `git status` stays clean.
- Re-run whenever the patch files or the submodule commit change (track their hashes in a stamp file).
- A patch that fails to apply stops the configure step with the patch name.
- Patch file naming: `0001-render-to-target-fbo.patch`, `0002-feedback-texture-api.patch`, …. Each patch is one concern, with a header comment explaining why.
- Helper `scripts/new-patch.sh`: creates a scratch git worktree of the pinned projectM, applies the existing patches, and lets you edit and export a new patch with `git format-patch`. Documented in `third_party/patches/README.md`.

---

## 2. Repository layout (target)

```
CMakeLists.txt
cmake/                  FetchContent pins, projectM patch step, macOS bundle helpers
scripts/                apply/new-patch helpers, convert_fork_performance.py
third_party/
  projectm/             submodule @ v4.1.8 (never edited)
  patches/              ordered .patch files + README
  Syphon/               Syphon.framework (or build instructions)
src/
  common/               log, app_info (generated), small utilities (paths, hashing, ring buffer)
  model/                Scene, Performance, JSON (de)serialisation, validation, migration
  library/              PresetIndex, indexer, tag/author parsing, search, Favourites, ThumbnailCache (disk)
  io/
    MidiInput.hpp       interface + MidiParser (pure C++, unit-tested)
    AudioInput.hpp      interface
    VideoSource.hpp     interface (SyphonSource, FileVideoSource)
    macos/              CoreMIDI, CoreAudio, Syphon, AVFoundation implementations (.mm)
  engine/
    GlContext*          SDL2 GL context creation / share-group helpers
    ProjectMInstance*   RAII wrapper around projectm_handle (+ patched APIs)
    ScenePool*          sliding-window pool, compile scheduling
    Lane*               worker thread + shared GL context hosting instances (M1 decision)
    (presenting)        presented texture copy; the app draws it in the Preview panel or fullscreen
    PcmFanout*          audio ring buffer → all pooled instances
    Engine.hpp          the only interface the app uses
  thumbnails/           ThumbnailService (worker context, tiny instances, queue)
  app/
    main.cpp            SDL init, windows, main loop
    ui/                 ImGui panels: Preview, SceneList, SceneProperties, Browser, Settings, Theme
    macos/              NSOpenPanel / NSMenu / Finder-drop shims (.mm)
tests/                  Catch2 unit tests (model, library, MIDI parser, ring buffer, scheduling logic)
tools/spike_compile/    milestone 1 measurement tool
docs/                   (later) user notes, show checklist
```

---

## 3. Milestones

Effort guesses are relative sizes (S ≈ ≤1 day, M ≈ 2–3 days, L ≈ about a week), not commitments.

### M0: Build skeleton (S)
**Goal:** an empty app window builds and runs against patched projectM, with one trivial patch proving the patch pipeline.
- Top-level CMake, FetchContent pins, projectM patched-copy step (§1.1), Syphon framework detection.
- `0001-render-to-target-fbo.patch` (needed anyway, see M1): projectM v4.1.8 unconditionally binds framebuffer 0 at the end of `ProjectM::RenderFrame` (there is a `ToDo: Allow external apps to provide a custom target framebuffer` there). Add `projectm_opengl_render_frame_fbo(instance, GLuint fbo)`, which draws the final image into the given FBO. Upstream master has an equivalent function, so match its signature if it's there; that makes the patch easy to drop on a future upgrade.
- `app/main.cpp`: SDL2 + GL 4.1 core context, an ImGui docking window, one projectM instance with the idle preset rendering into an offscreen FBO shown via `ImGui::Image`.
- `common/log`, `app_info`, `.clang-format`, `gv_tests` with one passing test, `README.md` with build steps (`git clone --recursive`, brew deps if any, cmake commands).

**Acceptance:** a fresh clone builds with the documented commands; the app shows the idle preset inside an ImGui panel; `git status` is clean after building; the test target runs.

### M1: Background compile spike (M–L) ⚠️ the critical risk
**Goal:** choose and prove the strategy that lets a scene's preset be prepared **without stalling the render thread**, and measure what pool size we can afford.

Findings from reading v4.1.8 that shape the spike:
- `ProjectM::LoadPresetFile` → `PresetFactoryManager::CreatePresetFromFile` (parse plus construction) → `StartPresetTransition` → `MilkdropPreset::Initialize`. That runs expression compile (projectm-eval), framebuffer sizing, and `CompileWarpShader` / `CompileCompositeShader` (HLSL → GLSL transpile via hlslparser, then `glCompileShader` / `glLinkProgram`).
- **VAOs and FBOs are created in constructors** throughout (`RenderItem::Init`, `Framebuffer`, `CustomShape`, `PerPixelMesh`, `BlurTexture`, …). A preset built on one GL context therefore has container objects that don't exist on another. This makes the "build on a shared context, render on main" approach a **larger patch than the technical doc assumed**.
- Static shaders (`MilkdropStaticShaders`) and per-instance texture managers also hold GL objects.

**Strategies to evaluate (in this order):**
- **S1: split CPU and GL work.** Move the CPU-only work (file parse, projectm-eval compile, HLSL preprocessing/transpile) to a worker thread with **no GL**. Do the GL work (shader compile/link, VAO/FBO creation, texture loads) on the render thread, **sliced across frames** (e.g. warp program in one frame, comp program in the next), keeping each slice within the frame budget. This needs a patch that separates the preparation and GL phases of `MilkdropPreset` construction and `Initialize`. It has no cross-context problems.
- **S2: shared-context build plus per-context container recreation.** Build the whole instance on a worker context in the same share group (programs, buffers and textures are shared). Then on the render thread call a patched `projectm_attach_to_current_context(instance)`, which recreates only the VAOs (re-running each `RenderItem::InitVertexAttrib`) and FBOs (re-attaching the shared textures). The patch is larger and touches many classes. Driver behaviour (deferred compile at first draw) must be checked.
- **S3: fallback.** Whole preset load on the render thread during frames where nothing else is happening, with a larger lookahead window and longer lead time. No patch beyond M0. Only acceptable if S1/S2 fail.

**Tasks:**
1. `tools/spike_compile`: a headless (hidden SDL window) harness that loads N presets sampled across Cream of the Crop categories and records, per preset: parse time, eval compile time, HLSL transpile time, GL compile/link time, first-draw time (to catch deferred driver work), and per-instance GPU memory at 1080p and 4K (estimated from FBO/texture sizes).
2. Instrument the patched-copy build with timing hooks (spike-only patch, not kept).
3. Prototype S1. If the GL slices can't be kept under about 4–6 ms each on the author's Mac, prototype S2.
4. Decide, write the result into `technical-details-2026-10-22.md` §4 (strategy, numbers, pool size), and turn the chosen approach into clean patches (`0003-…`).

**Acceptance:** a written decision with numbers. A demo in the M0 app: press a key to cut between two presets prepared in the background, with **no frame over 16.7 ms** at 60 Hz during preparation or at the cut (frame-time graph in the debug overlay). Also: measured **per-instance memory at 1080p** (framebuffers, blur levels, motion-vector map, per-instance texture manager). From that, the pool window that fits inside the **5 GB app budget**, leaving room for video players, thumbnails (M6) and the UI.

Rough pre-measurement estimate: about 8 MB per full-size RGBA8 1080p texture, and roughly 6–10 such textures per instance, so about 50–100 MB per instance before textures loaded from disk. If that holds, the starting window (prev 1 / current / next 2) uses well under 1 GB, and the window can grow (e.g. next 4–6) to make out-of-order jumps less likely to stutter. Decide this with the real numbers.

**Exit risk:** if neither S1 nor S2 works within budget, stop and discuss with the author before continuing (S3 means accepting stutter on cuts outside the lookahead window).

**M1 outcome (2026-10-22): decided on lanes.** No projectM context-migration patch; see `tools/spike_compile/RESULTS.md` and technical doc §4.

### M2: Engine core and playback from JSON (L): **first show-capable path**
**Goal:** play a performance file with MIDI-triggered instant hard cuts on a fullscreen output display.

1. **Model (`src/model`).** `Scene`, `Performance`, JSON v1 per technical doc §6 using nlohmann/json:
   - Validation: version present; ≤128 scenes; unique `id`s; `cc20Value` 0–127 and unique; mix and luma in [0,1].
   - Errors are collected (path + message), never thrown at the UI.
   - Missing files are marked `broken` but the load still succeeds.
   - A migration hook (`migrate(json, fromVersion)`) exists with nothing to do yet.
   - Unit tests: round-trip, every validation rule, missing-file flagging.
2. **`scripts/convert_fork_performance.py`.** Converts the fork's `performance.json` plus `favourites.txt` aliases into v1. Aliases are resolved to absolute paths in the user's library location (with a `--library-root` remap for the old `libprojectM-4.1.4` paths). `cc20Value` follows the fork's positional order. This gets the author's existing show into the new app on day one.
3. **`Lane` and `ProjectMInstance`** (M1 decision: lanes, see `tools/spike_compile/RESULTS.md`).
   - `Lane`: a thread + GL context in the output share group with a job queue; three output slot textures + fences; a scratch target for warm-up; instances confined to the lane. Promote the spike's `pipeline.cpp` design to production code (vsync-driven instead of `sleep_until`, proper shutdown, error handling).
   - `ProjectMInstance`: RAII wrapper used only on its lane: set window size; preset locked; hard-cut disabled; soft-cut duration 0; preset duration effectively infinite; load; 2 warm-up draws (recording steady frame time for the heavy-preset flag); render to FBO; PCM add.
   - **Pre-warm** all scenes once when a performance loads, with progress shown.
4. **`PcmFanout`.** The audio thread writes into a lock-free ring buffer; once per output tick the engine snapshots the new samples and each lane calls `projectm_pcm_add_float` on **every pooled instance it hosts**. Unit-test the ring buffer.
5. **`AudioInput` (CoreAudio).** Enumerate input devices (name + UID), open by UID, mono/stereo float callback into the ring buffer, gain applied on the render-thread side (no clipping hacks), device-removed notification → falls back to "no input" with a UI warning.
6. **`MidiInput`.** A `MidiParser` that is pure C++, handling status bytes, **running status**, real-time bytes interleaved anywhere (ignored), channel filter, CC 20 only; unit-tested with byte streams. The CoreMIDI implementation connects all sources, handles hotplug via the notify proc (connect new sources), and feeds the parser. The latest value goes to an atomic for the render loop.
7. **`ScenePool`** (logic separated from GL so it can be unit-tested):
   - Window: prev 1 / current / next 3 (configurable); prepared round-robin onto non-live lanes (3 lanes).
   - **Fresh-on-entry, implemented by recycling (refines technical doc §3 patch 4 / §4):** when a scene is left, its used instance is discarded and a **fresh instance is queued** for background preparation if the scene is still inside the window. Every entry then gets an instance that has never rendered. The "reset without recompile" patch becomes **optional**, needed only if M1 shows re-preparation is too slow for the "prev 1" slot.
   - On a trigger: if ready, switch the live lane/instance (the cut is on screen next tick). If not ready, **hold the last output frame**, prepare with top priority on a non-live lane, then cut. Log a warning with the timing.
   - After a switch: evict instances outside the window; enqueue missing neighbours in priority order (next+1, prev, next+2, next+3).
   - Unit tests: window maths at the ends of the list, out-of-order jumps, rapid repeated triggers, triggers for scenes still being prepared.
8. **`OutputRenderer` and windows.**
   - The output window is an SDL2 borderless fullscreen-desktop window on the chosen display, sized from that display's native resolution (backing scale aware).
   - The live lane renders into its slot textures at that size; the output thread waits on the slot fence, draws it to the output window, and the editor shows the same texture via `ImGui::Image`.
   - Before the first trigger: black.
   - Display hot-unplug: the output window closes and the UI shows a warning; the engine keeps running.
   - VSync on the output window. The editor window renders at a lower, throttled rate (e.g. 30 fps) and never blocks the output's swap. The editor and output share a GL context group; the main loop presents the output first.
9. **Minimal ImGui shell** (temporary, replaced in M4): open a performance file (via the M4 `NSOpenPanel` shim, or a path text field for now), show the scene list with the active scene highlighted and broken scenes marked, click a scene to trigger it, pick the output display, audio device, gain and MIDI channel, and toggle a debug overlay (frame time graph, pool state, compile queue, MIDI last value, audio level).
10. **Settings** (`settings.json` in Application Support): library root, **texture folder(s)**, audio device UID, gain, MIDI channel, output display, names-only toggle; load and save with defaults.
11. **Texture folder.** User-supplied, like the preset library (author's copy: `/Users/kurt/projects/visualizers/milkdrop-textures/textures`, a clone of [projectM-visualizer/presets-milkdrop-texture-pack](https://github.com/projectM-visualizer/presets-milkdrop-texture-pack)). Every instance gets the folder via `projectm_set_texture_search_paths` on its lane before loading a preset. Checked 2026-10-22: 1,682 of 9,795 presets reference image textures (64 distinct names). The pack covers 46 names, so 1,321 of those presets (78%) get every texture and 361 still miss at least one (biggest gaps: `pic`, `rose`, `shub1`, `state`).

**Acceptance:**
- The author's converted performance plays on the second display.
- CC 20 from Ableton (IAC) triggers scenes.
- Sequential cuts show **0 frames over 16.7 ms** across a full run of the performance (debug overlay plus a log summary).
- An out-of-order jump holds the last frame and then cuts; it never shows black.
- Every scene entry is visibly fresh.
- Unplugging the MIDI device or audio device, or deleting a referenced preset file, never crashes the app.

**M2 as built (2026-10-22):**
- **Handover at cuts:** the outgoing scene keeps rendering on its lane until the incoming scene's first frame is presented, so a cut never repeats a frame. The scene pool's release of the outgoing instance is deferred until then.
- **Frame cost and drops per scene:** the heavy flag uses the live lane's average CPU and GPU (`GL_TIME_ELAPSED` timer query) render time once a scene has been live for 0.5 s; the warm-up frame is only an initial estimate (too noisy on its own). The scene list also shows each scene's measured drop rate while live.
- **Fullscreen instead of an output window (2026-10-25, author's decision):** the editor window itself goes fullscreen on its current screen with Cmd+F (or the Preview's Fullscreen button) and draws only the visuals; Cmd+F/Esc returns. No output-display setting.
- **Audio** via SDL2 capture (devices by name) and **MIDI** via UMP decoding: see technical doc §5.
- `--smoke` mode plays a performance headless, cutting every N seconds, and reports held frames by cause and per scene.

### M3: Video (M–L): **show-capable milestone**
**Goal:** per-scene video injected into the warp feedback from files or Syphon.

1. **`0002-feedback-texture-api.patch`.**
   - `projectm_set_feedback_texture/_mix/_luma_gate`, with state persisted on `ProjectM` and handed to the active preset on load (technical doc §3).
   - Blend step ported from the fork's `BlendVideo` into `MilkdropPreset::RenderFrame` before the y-flip: cached uniforms, full RGB and alpha blend-state save/restore, and GL objects owned by the instance rather than per preset.
   - `sampler_video` declared through the shader generator's normal sampler path, with a 1×1 black fallback.
   - Ported against v4.1.8 code, not copied.
2. **`VideoSource` interface:** `prepare()`, `tick(now) → GLuint tex2D` (bottom-up orientation), `state()` (connecting / live / disconnected / broken), `restart()`.
3. **`FileVideoSource`** (AVFoundation):
   - `AVPlayer` + `AVPlayerItemVideoOutput`, muted, looping.
   - **Zero-copy** via `CVOpenGLTextureCache` (rectangle textures) converted to 2D with a GPU pass that also fixes orientation.
   - Created ahead of time for pooled scenes so the first frame is ready at the cut; honours `restartOnEnter`.
   - A source used by several pooled scenes is shared and reference-counted.
4. **`SyphonSource`:**
   - Server directory observation (announce/retire) gives a list of `{appName, serverName}` for the UI.
   - The source matches on both names and reconnects automatically. When disconnected it returns 0, so injection is off and there's no stale frame, and its state is reported to the UI.
   - Rectangle → 2D conversion on the GPU.
5. **Engine:** on scene entry, apply the source, mix and luma to the instance **before** its first frame. Tick the sources every frame before rendering.
6. **Minimal shell additions:** per-scene video state in the scene list (live / disconnected / missing file).

**Acceptance:**
- The author's converted show runs end to end with its file videos: video is warped by presets, mix and luma match the fork's look, and restart works.
- A Syphon test sender (e.g. Simple Server) is picked up by name, survives sender restarts, and shows "disconnected" (not frozen) when it quits.
- 4K and 1080p file playback cost measured: the CPU stays low with no per-frame copies (check in Instruments).

### M4: Editor: performance and scene authoring (L)
**Goal:** create and edit performances without touching JSON.

1. **Theme pass:** font (bundled Inter or similar), spacing, a colour palette, larger hit targets. ImGui must not look like a developer tool.
2. **Layout** (ImGui docking, saved per user): Preview (centre, large), Scene List (left), Scene Properties (right), Browser (bottom, placeholder until M5), status bar (MIDI, audio level, output display, compile queue).
3. **Document handling (hand-rolled, as there's no NSDocument):**
   - New / Open / Save / Save As via `NSOpenPanel` / `NSSavePanel` shims.
   - Unsaved-changes indicator plus a prompt on close or open; recent files list; autosave of a recovery copy.
4. **Undo/redo:** a command stack over the model (add, remove, reorder, edit fields). Coalesce slider drags into one undo step.
5. **Scene List:**
   - Linear list showing the CC value, name, preset name and video badge.
   - Drag to reorder (CC values keep their numbers).
   - Add, duplicate, delete; enforce the 128 limit.
   - Broken placeholders; click or double-click to trigger; the active scene is highlighted.
6. **Scene Properties:**
   - Name; CC 20 value with conflict warning; preset (path display plus "choose…" via file picker until the M5 browser exists).
   - Video: on/off, source type, file picker or Syphon server dropdown (live list), mix, luma, restart.
   - Changes made to the active scene apply live.
7. **macOS menu bar** via the `NSMenu` shim (File / Edit / View / Window with the standard shortcuts: ⌘N, ⌘O, ⌘S, ⌘Z, ⇧⌘Z, plus a Perform toggle).
8. **Finder drop:** dropping a video onto a scene sets it as that scene's file source; dropping a `.json` opens it.
9. **Perform mode = fullscreen** (Cmd+F, built in M2): throttles editor-only work such as thumbnails while fullscreen.

**Acceptance:** the author can rebuild their show from scratch in the editor, save it, reopen it and play it. Undo works for every edit. The app warns about unsaved changes and recovers from a crash via autosave.

### M5: Preset library, browser and favourites (M–L)
**Goal:** browse, search, filter and favourite the whole Cream of the Crop library; assign presets to scenes from the browser.

1. **First-run flow:** prompt for the library folder and the texture folder (default suggestions are common locations if they exist), saved in settings, with change options in Settings.
2. **Indexer** (background thread, no GL):
   - Recursive scan for `.milk`; the preset ID is its path relative to the root.
   - Category and subcategory from the first two folder levels; author from the filename prefix before `" - "`; display name from the remainder.
   - Mtime + size stored for incremental re-index.
   - Persisted to `library.json`. Benchmark at 10k entries; switch to SQLite only if load or search exceed ~100 ms.
   - Unit tests for tag and author parsing on real-world filename samples (multi-author, no author, odd punctuation, deeper/shallower trees).
3. **Search/filter:** case-insensitive substring over name, author and tags; a two-level category filter; author filter; favourites-only toggle; results update as you type (target under 16 ms for 10k entries).
4. **Favourites:** star toggle in the browser and the scene properties; stored in `favourites.json` (a list of preset IDs); IDs that no longer exist after a re-index are shown as broken, not silently dropped.
5. **Browser panel:**
   - A virtualized grid (`ImGuiListClipper` over rows), names-only mode as a list.
   - Tile: thumbnail slot (filled in M6), name, author, star, broken badge.
   - Assign to the selected scene by double-clicking or dragging onto a scene.
   - "Reveal in Finder" for a preset.
6. **Broken detection (part 1):** presets that fail to parse or compile when used in a scene are marked broken in the index.
7. **Missing textures:** during indexing, collect each preset's referenced texture names (`sampler_*` in the shader code, minus prefixes `fw_`/`fc_`/`pw_`/`pc_` and the built-ins `main`, `blur1-3`, `noise*`, `noisevol*`, `rand00-15`). Compare them with the files in the texture folder(s), store the missing names in the index, and **flag those presets** ("missing textures: rose, pic") in the browser and scene list. Recompute when the texture folder changes. Unit-test the name extraction.

**Acceptance:** the full library indexes in the background on first run without blocking the UI. Browsing, filtering and searching 10k presets is smooth (no dropped editor frames). Favourites persist. A preset can be assigned to a scene in two actions.

### M6: Live mini-previews (M–L)
**Goal:** visible tiles show tiny ~1 fps live previews without ever affecting the output.

1. **`ThumbnailService`:**
   - A dedicated thread with its own shared GL context (an SDL hidden window).
   - Tiny instances (~160×90) living entirely on that context, using the M1 prepare strategy.
   - A built-in **synthetic audio signal** (no live input).
   - Renders each active tile at ~1 fps; frames are read back to small RGBA buffers, which the UI thread uploads into a texture atlas.
2. **Queue:**
   - Visible tiles ordered by position. Tiles that leave the viewport before they finish are cancelled.
   - Idle prefetch of one row above and below.
   - Concurrency cap = visible tile count (configurable, ~6–20).
   - Instances torn down when they scroll out.
3. **Disk cache:** the last frame per preset as a PNG in `thumbnails/`, keyed by preset ID + mtime/size. Shown immediately on revisit while the live preview spins up. LRU size cap (e.g. 500 MB).
4. **Broken detection (part 2):** compile failure, or near-black output after N frames of synthetic audio, marks the preset broken (with a "maybe" state for near-black, since some presets are legitimately dark at first).
5. **Names-only switch** disables the service entirely.
6. **Perform mode:** the service pauses (or drops to one tile every few seconds, configurable). It must **never** cause an output frame over budget; verify with the M1 frame-time overlay while scrolling the browser during playback.

**Acceptance:** scrolling the browser shows names instantly and previews filling in progressively, with no upfront wait. Revisited pages show cached images instantly. Output frame times are unaffected (0 frames over budget) while scrolling during a performance.

### M7: Show hardening (S–M)
- **Soak test:** run the full show in a loop for 2+ hours at 1080p with random and sequential triggers. **Peak physical footprint must stay ≤ 5 GB** (`footprint` / Activity Monitor) with no growth over the run. Check for GL object leaks and review the log for warnings.
- **Failure drills:** MIDI unplug/replug, audio device change, Syphon sender crash, display unplug/replug, missing video, a broken preset in a scene. Each must degrade gracefully with a visible indicator.
- **Startup:** reopen the last performance, re-select the last audio and MIDI settings, and put the output on the last display if it's present.
- **`docs/show-checklist.md`:** pre-show steps (Loopback routing, IAC CC 20 channel, displays, macOS "Do Not Disturb", disable sleep) and recovery steps.
- Write down in the docs which macOS GL deprecation warnings are expected so nobody chases them.

---

## 4. Patch set summary (target)

| # | Patch | Milestone | Keep long-term? |
|---|---|---|---|
| 0001 | Render to a caller-provided FBO (`projectm_opengl_render_frame_fbo`) | M0 | Drop if upstream equivalent arrives on upgrade |
| 0002 | Feedback texture API + `sampler_video` | M3 | Yes (candidate for upstreaming) |
| ~~0003+~~ | ~~Background-prepare support~~: **not needed** (M1 chose lanes) | M1 | — |
| (opt) | Reset preset without recompile | M2 if needed | Only if recycling is too slow |

Spike-only instrumentation patches live in `tools/spike_compile/patches/` and are never part of the main build.

---

## 5. Testing strategy

- **Unit (Catch2, run in CI and locally):** model JSON and validation, the migration hook, MIDI parser byte streams, the ring buffer, ScenePool window/scheduling logic (GL-free by design), indexer tag/author parsing, search, favourites, settings defaults.
- **GL integration (local, needs a GPU):** `spike_compile` reused as a regression benchmark (compile timings, frame-time histogram); a smoke test that loads a performance, triggers every scene and checks there are no GL errors (`glGetError` sweep in debug builds).
- **Manual show tests:** the M7 drills and soak. A test Ableton set sending CC 20 values 0..N on a known channel.
- **CI (optional, later):** GitHub Actions macOS runner for build + unit tests only (no GPU tests).

---

## 6. Risks & mitigations

| Risk | Impact | Mitigation |
|---|---|---|
| ~~Background preparation can't meet the frame budget~~ | **Resolved in M1:** lanes prototype, no cut waited for a load. | — |
| macOS GL driver defers compile work to first draw | Hitch on the first frame after a cut | **Measured in M1:** 2 warm-up draws on the preparing lane absorb it; pre-warm at performance load keeps the shader cache hot. |
| Heavy presets (~15% of the library) can't reach 60 fps on the M1 Pro | Dropped frames whenever such a scene is live | Measure steady frame time during warm-up; flag heavy presets in the browser and scene list (M2 index field, shown in M4/M5). |
| Exceeding the 5 GB app budget (pool, video players, thumbnails) | Memory pressure, fewer instances ready | Per-instance memory measured at 1080p in M1; the window size is derived from the budget and configurable; the engine tracks its estimated usage and the debug overlay shows it; verified in the M7 soak. |
| Patches drift from upstream | Upgrade pain | Small single-concern patches; prefer upstream API shapes; README procedure for rebasing. |
| ImGui looks like a developer tool to artists | Poor first impression | Theme pass in M4; revisit native after the show (technical doc §2). |
| Some Cream of the Crop presets misbehave in projectM | Broken scenes | Broken detection (M5/M6); failing presets never crash the app; logged with the preset ID. |
| Syphon.framework sourcing/build | Build friction | Pin the version, document it in the README, fail clearly in CMake. |

---

## 7. Changes this plan makes to the technical doc

1. **§0 patch application:** patches are applied to a **copy in the build directory**, so the submodule stays clean (detail added).
2. **§3:** a new patch **0001 "render to target FBO"** is required; v4.1.8 always draws the final image to framebuffer 0.
3. **§4 "critical risk":** v4.1.8 creates VAOs/FBOs in constructors, so the shared-context approach is a bigger patch than assumed. M1 compares S1 (CPU/GL split, GL sliced on the render thread) with S2 (shared context + container re-attach).
4. **§3 patch 4 / §4 fresh-on-entry:** implemented by **recycling instances** (discard the used one, prepare a fresh one); the reset patch becomes optional.
5. **§9 milestones:** reordered to reach a show-capable build first (engine → video → editor → library → thumbnails) instead of library before editor.

---

## 8. Author's answers (2026-10-22)

1. **Show date: November 6, 2026.** No milestone schedule; all milestones are implemented back to back.
2. **Branching:** one branch and **pull request per milestone** on GitHub (`kurtvan/govtech-visualizer`, private). Branch names: `m0-skeleton`, `m1-compile-spike`, `m2-engine`, `m3-video`, `m4-editor`, `m5-library`, `m6-previews`, `m7-hardening`. Each PR description lists the milestone's acceptance criteria with their status. The author reviews and merges.
3. **Machine: M1 MacBook Pro, 32 GB RAM.** **Memory budget: the whole app stays at or below 5 GB** (physical footprint, counting GPU allocations since M1 memory is unified). **Target output: 1080p.** All frame-time and memory budgets are measured at 1920×1080. The "render at the display's native resolution" decision stands (the show display is treated as 1080p). If a larger display is used, the 5 GB cap decides how many scenes can be kept ready.
4. **Preset library:** `/Users/kurt/projects/visualizers/libprojectM-4.1.4/presets/milk/`. 9,795 presets: 9,791 are exactly two levels deep (`Category/Subcategory/file.milk`) and 4 are one level deep, so the tag scheme fits. There are 12 top-level categories, including `! Transition`. The folder's `LICENSE.md` treats the presets as public domain, but the user-supplied-library decision still stands.
