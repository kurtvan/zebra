# Govtech Visualizer: Technical Details & Decisions

*Agreed on 2026-10-22. Companion document: `product-2026-10-22.md` (what we're building). This document covers how, and why.*

## 0. Starting point

- **Fork:** https://github.com/kurtvan/libprojectM-4.1.4 (libprojectM 4.1.4 plus custom work in `src/sdl-test-ui` and small library patches). Custom work spans commits `8e0b2af..bec025c`, about 2,200 lines.
- The fork is a **prototype to learn from, not a base to extend.** It proves out: video injected into warp feedback, Syphon input, AVFoundation file playback, CoreMIDI CC triggers, a scene JSON file and a lookahead pre-compile.
- **Decision:** treat libprojectM as an **upstream dependency with a minimal, documented patch set**, and build a **new application shell**. Do not grow `sdl-test-ui` (it is projectM's test harness).
- **App name:** "Govtech Visualizer" is the working name and will be renamed later. Keep it in **one constant / CMake variable** (window titles, bundle ID, the Application Support folder) so the rename is trivial.

### Repository layout (set up 2026-10-22)
- This directory is the git repo (`main` branch) and the home of all the code.
- **projectM is the upstream repo as a git submodule** at `third_party/projectm`, **pinned to tag `v4.1.8`** (the latest 4.1.x). It has its own nested submodule (`vendor/projectm-eval`), so clone with `git clone --recursive`, or run `git submodule update --init --recursive`.
- The fork was based on **v4.1.4**, so its patches must be **ported to v4.1.8**, not copied verbatim.
- **Never commit inside the submodule.** Changes to projectM live as ordered patch files in `third_party/patches/` and are applied by the build **to a copy of the submodule in the build directory**, so the submodule's working tree stays clean (details in the implementation plan §1.1). Patch application must be idempotent and fail loudly if a patch doesn't apply. Upgrading projectM means bumping the submodule tag and rebasing the patches.
- **Windowing: SDL2** (decided: kept from the fork because it's known to work with the author's setup). ImGui uses `imgui_impl_sdl2` + `imgui_impl_opengl3`.

## 1. Review of the fork: what to keep and what to rewrite

### Keep (as ideas, and some of the code)
| What | Where in fork | Notes |
|---|---|---|
| libprojectM renderer core | `src/libprojectM` | Solid. Keep patches small and ideally upstreamable. |
| Video blended into the **warp feedback** buffer | `MilkdropPreset::BlendVideo` | The key creative feature: video is drawn into the previous-frame FBO before the warp pass, so the preset warps it. Keep the effect; re-implement it cleanly (see §3). |
| `samplerVideo` available to comp shaders | `MilkdropShader.cpp` | Keep the capability; implement without text-splicing hacks. |
| Luma-gate shader math | `BlendVideo` fragment shader | `smoothstep(lumaMin-0.05, lumaMin+0.05, luma)` gate × mix. Fine as is. |
| MIDI callback → atomics → render-loop pickup | `MidiInput.mm` | Correct threading pattern. |
| AVFoundation player setup (muted, loop via notification) | `VideoFilePlayer.mm` | Keep the setup; replace the per-frame CPU copy (see below). |
| Scene data model shape | `Scene.h` | Preset + video source + mix + luma + restart. Extend it (see §6). |
| Lookahead pre-compile idea | `pmSDL.cpp` | Right idea, wrong implementation (see §4). |

### Rewrite or remove
| Problem in fork | Decision |
|---|---|
| Everything is in the `projectMSDL` god-class in the SDL test harness; keyboard controls and logs are the only UI. | New app shell (§2). |
| One `USE_SYPHON` define gates MIDI, scenes, video files and the overlay. `projectM_SDL_main.cpp` calls `loadPerformanceFile` outside the guard, so non-Syphon builds fail to compile. | Separate modules, each behind its own platform interface (§7). No catch-all flag. |
| `std::swap(_projectM, _warmProjectM)` for instant switches: the audio thread writes `app->_projectM` with no synchronisation (data race); the spare instance never receives PCM (no audio history); the playlist stays bound to the old handle; only the *next sequential* scene is prepared; the startup warmup depends on the driver's shader cache. | Replace with the scene-instance pool (§4). |
| `projectm_set_video_texture` changed signature (ABI break), and its doc omits the luma and mask params. | New additive API (§3). |
| Comp shaders get `samplerVideo` by inserting text after line 1 of the generated GLSL. | Inject through the shader generator's proper declaration path (§3). |
| `glGetUniformLocation` every frame; blend-state save/restore covers RGB only (not alpha); unused `m_videoBlendFBO`. | Cache uniform locations; save/restore full blend state (separate RGB/alpha); remove dead members. |
| Video file frames: CPU copy, software row-flip, `glTexSubImage2D` every frame. | Zero-copy via `CVOpenGLTextureCache` / IOSurface. Flip in the shader / UVs. |
| Syphon: connects to the first server found; keeps showing the last frame after the server disappears. | Server picker per scene; match by app name + server name; explicit "disconnected" state (§5). |
| MIDI: CCs 20–23 hard-coded; channel ignored; running status not handled; sources connected only at launch; beat clock and CC 23 parsed but unused. | Rewrite (§5): CC 20 only, one configured channel, running status, hotplug via `MIDINotifyProc`. Drop the unused clock and CC 23 handling. |
| Audio callback multiplies samples by 4 and hard-clips. | Configurable gain; device selection (§5). |
| `favourites.txt` / `performance.json` use absolute paths into the old `/Users/kurt/projects/visualizers/libprojectM-4.1.4/...` checkout; the format has no version; presets are named by hand-written alias. | New formats (§6). |
| `PersonMask` (Vision segmentation) is not in the CMake target; the mask param is always 0. `beatPulseStrength` is parsed but unused. | **Delete** both. Segmentation happens upstream of Syphon. |
| `VideoCompositor` overlay path (video drawn on top, keyboard-toggled), separate from warp injection. | Drop. The single video path is warp-feedback injection with mix and luma per scene. |

## 2. Application architecture

### Windows and rendering
- **Two windows:** an **editor window** (main display) and a **borderless fullscreen output window** on a user-chosen display, shown during performance.
- **Output resolution = the output display's native resolution** (decided). The output window is an SDL2 borderless fullscreen-desktop window on the chosen display; the render size follows that display (handle Retina/backing scale and display changes). Memory and GPU cost scale with it, so measure 4K displays in milestone 1.
- **Shared GL contexts.** The active scene renders **once per frame** into an offscreen FBO at output resolution. That texture is drawn to the output window and shown in the editor's preview panel. The preview costs about nothing extra.
- Output frame pacing follows the output display's vsync. The editor must never stall the output.

### Graphics API
- **Decision: stay on OpenGL** (macOS GL 4.1 core) for now. projectM is GL-only. Apple has deprecated OpenGL on macOS; we accept that risk today.
- **Constraint for a future Metal move:** keep all projectM/GL code inside a **render engine module**. The editor UI must not issue GL calls or depend on GL types. Later, either the engine hands frames to Metal via IOSurface, or the renderer is ported.

### UI toolkit: Dear ImGui (decided 2026-10-22)
- **Decision: Dear ImGui** (docking branch), rendering inside the GL editor window.
- **Reason:** the author's show is coming up soon, and ImGui gets to a working editor fastest. It's the same language as the engine; the preview and thumbnails are just `ImGui::Image(tex)`; `ImGuiListClipper` handles the virtualized browser; it works cross-platform and has a Metal backend.
- **Accepted downsides:** it looks like a developer tool unless themed (invest in fonts, spacing and colours for non-technical artists); it isn't native (no VoiceOver, non-native text fields, no macOS menu bar unless we add one via `NSMenu`); file pickers (`NSOpenPanel`) and Finder drag-and-drop need small Objective-C++ shims; there's no `NSDocument`, so save, unsaved-changes state, recent files and undo are hand-rolled; the editor redraws continuously (throttle it so it never competes with the output).
- **Alternative considered: native AppKit/SwiftUI.** More artist-friendly and Mac-native, `NSDocument` for free, and its IOSurface texture bridge would also serve the later Metal move. Costs: a Swift↔C++ bridge, more glue to show GL frames, a macOS-only UI. **Revisit after the show** for the distributable version.
- **Constraint that keeps that switch cheap:** the UI talks to the engine **only through the engine's interface**. No engine state in ImGui code, no ImGui types in the engine, `model/` and `library/` UI-agnostic. A later move to native should discard only `app/` UI code.

### Suggested module layout
```
engine/      projectM wrapper, scene instance pool, video injection, offscreen render, output
library/     preset folder indexer, tags, search, favourites, thumbnail service + disk cache
io/          platform interfaces + macOS impls: MidiInput (CoreMIDI), AudioInput (CoreAudio),
             SyphonSource, VideoFileSource (AVFoundation)
model/       Scene, Performance, (de)serialization + schema versioning
app/         windows, editor UI, perform mode, settings
third_party/ libprojectM (pinned upstream 4.1.x + patch set)
```

### Threads
| Thread | Owns |
|---|---|
| Main / render | Output GL context; renders the active scene; picks up MIDI/audio state; UI (if ImGui). |
| Scene compile worker | Shared GL context; compiles upcoming scenes' presets (§4). |
| Thumbnail worker | Its own shared GL context; tiny preset instances for the browser (§8). Lower priority; paused/throttled in perform mode. |
| CoreMIDI callback | Writes the pending scene trigger to an atomic. |
| CoreAudio callback | Writes PCM into a lock-free ring buffer. The render thread feeds it to projectM instances. **Never** call projectM from the audio thread. |
| Indexer | Filesystem scan of the preset library (no GL). |

## 3. libprojectM patch set

Keep patches minimal, additive and isolated so they can be rebased onto new upstream versions or upstreamed.

0. **Render to a caller-provided FBO.** v4.1.8 always binds framebuffer 0 at the end of `ProjectM::RenderFrame`, so the offscreen render in §2 needs `projectm_opengl_render_frame_fbo(instance, fbo)`. Match upstream master's signature if it has one, so the patch can be dropped on upgrade.
1. **External texture injection (replaces `projectm_set_video_texture`).** Additive C API in projectM's existing style (one setter per property, state persists on the instance like `projectm_set_beat_sensitivity`), declared in `render_opengl.h`:
   ```c
   /* tex: GL_TEXTURE_2D, bottom-up (GL) orientation, owned by the caller. 0 disables injection. */
   PROJECTM_EXPORT void projectm_set_feedback_texture(projectm_handle instance, uint32_t tex);
   PROJECTM_EXPORT void projectm_set_feedback_mix(projectm_handle instance, float mix);           /* 0..1 */
   PROJECTM_EXPORT void projectm_set_feedback_luma_gate(projectm_handle instance, float luma_min); /* 0..1 */
   ```
   Rationale:
   - Separate setters can be extended later (e.g. a future mask or blend mode) **without breaking the ABI**, unlike the fork's growing parameter list.
   - **State lives on the `ProjectM` instance** and is handed to whichever preset is active, including after a preset load. The caller sets it once on scene entry rather than every frame; it only re-sets the texture ID if the source's texture changes.
   - The caller normalises the input (Syphon rectangle textures → 2D, correct orientation), so the library sees only one kind of texture.

   Implementation: a small, self-contained blend step in `MilkdropPreset::RenderFrame` (where the fork's `BlendVideo` sits), drawn into the previous-frame FBO before the y-flip/warp. Uniform locations are cached at init, blend state (RGB and alpha) is fully saved and restored, and the GL program/VAO are owned by the projectM instance, **not per preset**.
2. **Comp shader sampler, renamed `sampler_video`.** This matches MilkDrop's `sampler_*` naming (`sampler_main`, `sampler_noise_lq`, …). No presets in the fork use the old `samplerVideo` name, so the rename costs nothing. Declare it through the shader generator's normal sampler-declaration path rather than string insertion. Bind it to a reserved texture unit; when injection is off, bind a 1×1 black texture so the shader always has something valid.
3. **Split compile from per-context setup (needed for §4).** See "Critical risk" below.
4. **(Optional) Reset preset state without recompiling.** Fresh-on-entry is implemented by recycling instances (§4), so this patch is only needed if milestone 1 shows re-preparation is too slow. An API such as `projectm_reset_preset(instance)` that clears the preset's framebuffers, re-runs its init equations and resets preset time, **reusing the compiled shaders**.

Remove from the fork's patches: mask texture params, and the `MilkdropPreset` destructor GL cleanup (moves with the resources to instance level).

## 4. Scene switching: instant cuts

### Requirements
- Hard cuts only. Moving to the next scene must have **zero stutter**.
- Any scene can be triggered. **Optimize for linear playback; accept a brief stutter on out-of-order jumps** (hold the last output frame rather than going black).
- **Up to 128 scenes per performance** (decided; the CC 20 range, see §5). Pre-compiling every scene does not scale (each instance holds output-resolution FBOs), so use a **sliding window**.

### Design: scene instance pool
- Each pooled entry is a projectM instance with its scene's preset loaded and compiled.
- **Kept ready (decided starting point):** the **previous 1**, the **current** scene and the **next 2**. Window size is configurable; tune it from the milestone 1 measurements (output resolution, memory).
- On a trigger:
  - **Pooled scene:** swap the active pointer. Next frame renders it. Instant.
  - **Not pooled:** hold the last frame, compile synchronously or with priority, then cut.
  - After any switch, the pool shifts: evict scenes far from the current one and queue new neighbours to the compile worker.
- **All pooled instances receive PCM every frame**, so a swapped-in preset has live audio history. The render thread does this from the audio ring buffer. Only the active instance renders.
- **Every scene entry starts fresh** (decided), including re-entering a scene that is still pooled (e.g. jumping back to the previous scene). Implemented by **recycling**: when a scene is left, its used instance is discarded and a fresh one is queued for background preparation if the scene is still in the window. Pooled-but-inactive instances receive PCM but never render, so every entry gets a fresh instance. The optional reset patch (§3 item 4) is a fallback if re-preparation is too slow.
- Video settings (source, mix, luma) are applied to the instance as part of the switch, **before** its first rendered frame.

### Critical risk: prototype first
GL **programs/shaders are shareable** across contexts in a share group; **FBOs and VAOs are not** (container objects). A projectM instance created on the compile worker's context therefore cannot simply be rendered on the main context.

**Finding from reading v4.1.8:** VAOs and FBOs are created in constructors throughout (`RenderItem::Init`, `Framebuffer`, `CustomShape`, `PerPixelMesh`, `BlurTexture`, …), so the approach below is a larger patch than first assumed. Milestone 1 therefore compares it (S2) with **S1: CPU-only work (parse, projectm-eval, HLSL transpile) on a worker thread without GL, and GL work sliced across frames on the render thread.** See the implementation plan's M1 section.

Original approach (S2): patch libprojectM so that (a) preset parse plus shader compile/link can run on the worker's shared context, and (b) creation of per-context objects (FBOs, VAOs) happens lazily on the render context at first use, which is cheap. Validate early:
1. Does compiling on a shared context actually avoid hitches on the render thread with the macOS GL driver? Watch for driver-side work deferred to first draw. Mitigate by issuing a tiny warm-up draw on the worker.
2. Measure compile times across a sample of Cream of the Crop presets.
3. Measure per-instance memory at 1080p and 4K to size the pool window.

**This prototype is milestone 1.** The rest of the playback design depends on it. Fallback if it fails: compile on the main thread during idle frame time in small slices, accepting a larger window and longer lead time.

## 5. I/O

### MIDI (CoreMIDI)
- **CC 20 on one MIDI channel** (configurable in settings, default channel 1). The CC value 0–127 selects the scene whose `midi.cc20Value` matches. No other mappings.
- Parse properly: status bytes, **running status**, interleaved real-time bytes (0xF8 etc. ignored).
- Connect to all sources; handle **hotplug** via the client notify proc (connect new sources as they appear).
- The callback writes the latest value to an atomic; the render loop polls once per frame (keep the fork's pattern).
- **Maximum 128 scenes per performance** (decided). Every scene has a unique `cc20Value` 0–127. The editor prevents adding a 129th scene; the loader rejects or flags files that exceed the limit or have duplicate values. The data model doesn't otherwise hard-code 128, so a larger scheme can come later.

### Audio input (CoreAudio)
- Enumerate input devices; the user picks one in settings (persisted by device UID). Handle device removal gracefully.
- A gain setting replaces the hard-coded 4× boost.
- Mono/stereo float PCM into a lock-free ring buffer; the render thread feeds all pooled projectM instances.
- The author routes Ableton in with Loopback today. Easier routing for non-technical users is a later effort.

### Syphon input (per scene)
- A scene with source type `syphon` stores **`appName` + `serverName`** (Syphon server UUIDs change every launch, so don't store them).
- The scene editor shows a picker of currently available servers (`SyphonServerDirectory`, observing announce/retire notifications).
- On scene entry, connect to the matching server. If it's absent or lost, report a **disconnected** state (no injection, a UI warning) and **do not show a stale frame**. Reconnect automatically when the server appears.
- Keep the fork's `GL_TEXTURE_RECTANGLE` → `GL_TEXTURE_2D` blit (or sample the rectangle texture directly in the inject shader).
- Use the legacy `SyphonOpenGLClient` for now; Syphon has a Metal client for the later Metal move.

### Video files (AVFoundation)
- `AVPlayer` + `AVPlayerItemVideoOutput`, muted, looping (as in the fork).
- **Zero-copy upload:** `CVOpenGLTextureCache` (or IOSurface-backed textures) instead of copying pixels on the CPU. Handle orientation in UVs, not with a CPU row flip.
- Open players for pooled scenes ahead of time so their first frame is ready at the cut. Honour `restartVideo` on scene entry.
- File picker: native `NSOpenPanel`.

### Platform abstraction
`MidiInput`, `AudioInput`, `VideoSource` (Syphon / file) are interfaces with macOS implementations. The scene format names a **source type**, never a platform API. This keeps Windows (Spout, WASAPI, Media Foundation) possible later.

## 6. Data formats

### Performance file (`*.json`, user-chosen location)
```json
{
  "version": 1,
  "name": "GovTech set",
  "scenes": [
    {
      "id": "2h-intro",
      "name": "2H intro",
      "preset": { "path": "/abs/path/to/presets/Dancer/Petals/....milk" },
      "video": {
        "enabled": true,
        "source": { "type": "file", "path": "/Users/.../2H-intro.mp4" },
        "mix": 1.0,
        "lumaMin": 0.0,
        "restartOnEnter": true
      },
      "midi": { "cc20Value": 0 }
    },
    {
      "id": "cave-1",
      "preset": { "path": "/abs/path/....milk" },
      "video": {
        "enabled": true,
        "source": { "type": "syphon", "appName": "Resolume", "serverName": "Composition" },
        "mix": 0.6,
        "lumaMin": 0.1
      },
      "midi": { "cc20Value": 1 }
    }
  ]
}
```
- **`version` from day one**, so the later "pack for portability" feature can migrate files.
- **Absolute paths** for presets and videos (agreed for now; a portable packed format comes later).
- `id` is stable and unique within the performance. `name` is a display label.
- `midi.cc20Value` (required, 0–127, unique) defaults to the lowest free value on creation and **does not change when scenes are reordered**, so the Ableton set keeps working. Validate uniqueness; show conflicts in the UI. At most 128 scenes.
- Scenes are an ordered array (linear display and lookahead order). Triggering doesn't depend on order, which leaves room for non-linear performances later.
- Missing preset/video paths load fine and are flagged **broken** in the UI; they never cause a crash.
- Use a real JSON library (e.g. nlohmann/json) with schema validation and clear error messages. Avoid `NSJSONSerialization` in core code, to stay portable.

### App-level data (`~/Library/Application Support/Govtech Visualizer/`, derived from the app-name constant)
- `settings.json`: preset library root, audio device UID, gain, MIDI channel, output display, names-only toggle.
- `library.json` (or SQLite if it gets large): preset index. Each entry has its path relative to the library root (the **preset ID**; names repeat across folders, so names are not IDs), display name, category, subcategory, author, mtime/size or content hash, and a broken flag.
- `favourites.json`: a list of preset IDs.
- `thumbnails/`: cached last-frame PNGs keyed by preset ID plus content hash.

## 7. Preset library & indexing

- The user picks the library folder on first launch (no bundling, to avoid licensing issues). Re-index on demand, and incrementally by mtime.
- Expected size is about 10,000 presets (Cream of the Crop). The indexer runs off the main thread and only does filesystem plus name parsing (fast). It never compiles presets.
- **Tags:** for a path `<root>/<Category>/<Subcategory>/<file>.milk`, category = first level and subcategory = second level, shown as a two-level filter. Handle deeper or shallower trees by using the first two levels and leaving missing levels empty.
- **Author:** the filename prefix before `" - "` (e.g. `shifter - mandala.milk` → `shifter`). Heuristic; may be empty or include several authors (`"fed + flexi + geiss"`).
- Search: case-insensitive substring over name, author and tags; fast enough in memory for 10k entries.

## 8. Thumbnail previews

Goal: **instant-feeling, no upfront wait.** Never pre-generate the whole library.

- The browser is a **virtualized grid/list**: only visible tiles exist. The name shows immediately.
- For each visible tile, the thumbnail worker creates a **tiny projectM instance (~160×90)** on its own shared GL context, compiles the preset (the dominant cost, ~50–500 ms) and renders it at **~1 fps**. Previews **do not react to the live audio input** (decided). Feed them a built-in synthetic audio signal so audio-driven presets still move and don't look dead; the same signal is used for the broken/black detection. The entire instance lives on the worker context, so the FBO/VAO sharing issue in §4 doesn't apply. Frames are read back as small images, or shared as textures, for the UI.
- **Queue management:** visible tiles in priority order. Tiles that scroll away before they finish are cancelled. When idle, prefetch tiles just outside the viewport. Cap concurrent live instances to the visible count (~6–20).
- **Disk cache:** store the last frame per preset so revisited tiles show an image instantly while the live preview restarts.
- A **"names only" switch** disables the worker entirely.
- **Perform mode:** pause or heavily throttle the worker so it can never affect output frame pacing.
- Presets that fail to compile, or render (near-)black after N frames, are marked **broken** in the index and show the placeholder.

## 9. Suggested milestones

**Superseded by `implementation-plan-2026-10-22.md`**, which reorders these to reach a show-capable build first (skeleton → compile spike → engine → video → editor → library → previews → hardening). The original list is kept below for reference.

1. **Background compile prototype** (§4 critical risk): patched libprojectM, shared-context compile, hitch measurement, memory per instance. Go/no-go for the pool design.
2. **Engine core:** offscreen render, output window on the chosen display, scene instance pool, PCM fan-out, CC 20 triggers, the performance JSON loader (v1). The old fork's performance can be converted by hand to validate it.
3. **Video:** clean feedback-injection patch, zero-copy file playback, Syphon per scene with a server picker and disconnected state.
4. **Library:** folder picker, indexer, tags/author/search, favourites.
5. **Thumbnails:** virtualized browser, worker, disk cache, names-only toggle.
6. **Editor (Dear ImGui, §2):** theming pass, scene list (linear, reorder, broken placeholders), scene properties, preview panel, audio device/gain, MIDI channel, perform-mode toggle.

## 10. Open decisions (summary)

| Decision | Recommendation |
|---|---|
No open decisions remain as of 2026-10-22. Decisions log:

| Decision | Outcome |
|---|---|
| UI toolkit | **Dear ImGui**, because the show is close (§2). Revisit native AppKit/SwiftUI after the show. |
| Scenes beyond 128 | **Not supported.** Max 128 scenes, one unique CC 20 value each (§5). |
| Pool window size | **Prev 1 / current / next 2** as the starting point; tune from milestone 1 measurements. |
| libprojectM injection API | **Separate persistent setters** `projectm_set_feedback_texture/_mix/_luma_gate`, plus `sampler_video` (§3). Final names may be adjusted while porting in milestones 1/3. |
| Re-entering a scene | **Always starts fresh**, by recycling instances (§4); the reset-without-recompile patch is an optional fallback. |
| App name | **"Govtech Visualizer"** (working name, kept in one constant for a later rename). |
| Output resolution | **The output display's native resolution** (§2). |
| Thumbnail audio | **Not audio-reactive**; driven by synthetic audio (§8). |
| Windowing library | **SDL2** (§0). |
| Repo / projectM source | **This directory is the repo**; projectM is an upstream submodule pinned to **v4.1.8**, with patches in `third_party/patches/` (§0). |
