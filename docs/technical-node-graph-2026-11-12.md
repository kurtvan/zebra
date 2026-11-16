# Node Graph: Technical Details & Milestones

*Drafted 2026-11-12; revised 2026-11-13 from review comments on PR #34. Companion to `product-node-graph-2026-11-12.md`; read that first for what each node does.*

## 0. Summary

A scene's graph is **data in the show file**. The artist edits it on the node canvas, where every node draws its own controls in its body, like an Ableton Live device. Edits are validated before they're applied, and an edit that would break the graph is refused with a message.

Each valid graph is **compiled into a render plan**: a flat, immutable description of what the lane draws each frame. Lanes never see the graph. Effect nodes become **small shader passes on the lane**, so rewiring only changes the plan and never compiles a shader. That's how "everything live" stays stutter-free.

Three projectM patch changes carry the new layers:
- the feedback blend becomes a generic **layer texture** blend, and patches 0002, 0004 and 0005 collapse into one;
- a new **before-composite** blend point;
- a **texture override**, so a preset's sampler can read a lane-owned video texture.

The first two milestones change nothing visible. They prove, with smoke-snapshot comparisons, that converted shows render as they do today, before any new UI lands.

## 1. Graph model (`src/model`)

```cpp
enum class NodeType { VideoSource, Contrast, Saturation, LumaGate, Mix, Preset, Switcher, Viewport };
enum class PortKind { Video, Frame };

struct Node {
    std::string id;           // stable within the scene
    NodeType type;
    float x{0}, y{0};         // canvas position
    bool expanded{true};      // false: shown as its title bar only
    NodeParams params;        // std::variant of one struct per type
};
struct Wire {
    std::string fromNode;     // every node has at most one output
    std::string toNode;
    int toInput{0};           // preset: 0 feedback, 1 before composite, 2 on top, 3 texture; switcher: input index
    std::string textureName;  // preset texture inputs: the sampler it replaces
};
struct Graph { std::vector<Node> nodes; std::vector<Wire> wires; };
```

- **Scene changes:** `Scene` gains `Graph graph` and loses `presetPath`, `playlist` and `video`, which are kept only for reading old files. `beatHit`, `cc20Value`, `name` and `id` stay.
- **Switcher:** a flat node, not a container. `SwitcherParams` holds:
  - the port kind (Frame or Video);
  - the interval (`IntervalUnit`, seconds, count);
  - `shuffle`;
  - the input count, which always includes one empty input at the end.

  What feeds each input is ordinary nodes elsewhere on the canvas. No nested graphs are needed.
- **Viewport:** no per-scene params. The canvas lives on `Performance` (section 8).
- **Validation:** `ValidateGraph` is pure and unit-tested, and returns `{nodeId, reason}` errors. Errors are:
  - port kinds that don't match, or two wires into one input;
  - a loop;
  - any switcher selection that makes **more than one preset or video source active** ("chains, one preset");
  - a switcher reached from another switcher of the same kind;
  - more or fewer than one viewport.

  "Active" means reachable from the viewport when each switcher passes only its selected input. The check runs over every input of every switcher, not just the current one.
- **Unfinished, not invalid:** a viewport with nothing wired into it, and nodes not connected to anything, are allowed. The scene renders black and the viewport node says "Nothing connected". This lets a graph be built one step at a time.

### Blocking invalid edits

- **One gate for all edits:** every graph edit goes through `TryEditGraph(scene, std::function<void(Graph&)>, label) -> std::optional<std::string>`. It applies the change to a copy and validates it. If it's valid, it commits through `Document::Edit` as one undo step; otherwise it discards the copy and returns the first error. The editor shows that error as a short message near the mouse for about 3 seconds.
- **Prevention during dragging:** while a wire is being dragged, inputs that would fail validation are dimmed, so most errors are prevented before they happen.
- **Undo and redo** only ever restore graphs that were valid when committed, so they never need validating.
- **Loading** validates every scene. If a hand-edited or damaged file fails, the bad scene loads as unfinished (its viewport disconnected), with a load warning naming the scene, instead of refusing the whole show.

## 2. File format v3 and migration

- **Format:** `kCurrentFormatVersion = 3`. Scenes are written as `"graph": {"nodes": [...], "wires": [...]}`, and node params keep the existing field names (`lumaMin`, `softness`, ...).
- **Reading v2:** each scene is converted with `GraphFromV2Scene(const Scene&) -> Graph`, following the product doc's conversion table.
  - **Effect order:** today's order is fixed (contrast, saturation, gate, mix), so the converter emits nodes in that order.
  - **Invert:** `invertLuma` becomes `LumaGate.invert` on every gate in the scene.
  - **Overlay:** a second chain from the same source, with its own gate (using `softness`) and mix, into the On top input. Today's feedback gate is hard-edged, so the feedback chain's gate gets softness 0.
  - **Preset playlist:** a Frame switcher. Each entry becomes a preset node fed by that entry's look chain from the shared source, wired into switcher input N.
  - **Video playlist:** a Video switcher of video sources, feeding the look chain(s).
  - **Both playlists together:** the Video switcher fans out into every entry's look chain.
- **Writing and reading:** the app writes v3 only, and keeps reading v1 and v2.
- **Tests:**
  - round-trips and each conversion case, in `test_performance_json.cpp`;
  - a test that converts the real 16-scene show (from a fixture copy), compiles its plans, and checks them against plans built from the v2 settings (section 3).

## 3. Render plan (`src/engine`)

`CompilePlan(const Graph&, const SwitcherSelection&, const Performance&) -> RenderPlan` is pure and unit-tested. `SwitcherSelection` maps each switcher to its current input. The compiler walks back from the viewport, following only the selected input of each switcher:

```cpp
struct EffectPass { EffectType type; float a, b; bool flag; };   // contrast / saturation / gate / mix
struct VideoBranch { std::vector<EffectPass> passes; int layer; std::string textureName; };
struct RenderPlan {
    std::string presetPath;               // the active preset
    std::string videoId;                  // or Syphon app/server; empty: no video
    std::vector<VideoBranch> branches;    // fan-out: every chain from the active source to the preset
    uint64_t revision;
};
```

- **Lane interface:** `VideoSettings` (Lane.hpp) is replaced by `RenderPlan` minus the preset path. `Lane::UpdateVideo` becomes `Lane::SetPlan`, queued like a tick and applied at the start of the next tick. Plans are small and copied.
- **The next selection:** the same compiler, run with each switcher's *next* input selected, gives the plan to prepare ahead (section 6).
- **Shared passes:** where branches share a prefix (e.g. contrast, then saturation, feeding both the feedback and on-top chains), the shared passes could run once. That's an N2 optimisation, not a requirement.

## 4. Effect passes on the lane

- **Programs:** each lane gets a new `VideoChain` with one fragment program per effect type. All four share `VideoOverlay`'s fullscreen-triangle vertex shader, are compiled when the lane starts, and are never recompiled.
- **Each tick:** the lane takes the live video frame (`VideoFrame`, unchanged). For each branch, it runs the passes ping-pong between two lane-owned RGBA textures at canvas size.
  - **Contrast and saturation** change RGB.
  - **Luma gate** writes the mask into alpha. It uses today's step/smoothstep math from `VideoOverlay.cpp`, with invert.
  - **Mix** multiplies alpha.
  - **Result:** an RGBA texture with **straight alpha**, consumed by the layer.
- **Layers:**
  - **Feedback and before composite** go through projectM: `projectm_set_layer_texture(handle, layer, texture)`, blended alpha-over (section 5).
  - **On top:** `VideoOverlay` shrinks to a plain alpha-over draw of the branch texture, after `projectm_opengl_render_frame_fbo`.
  - **Texture:** the branch result is copied into a **stable lane-owned texture per sampler name**, which the preset reads through the override (section 5). Its ID stays the same frame to frame, so nothing is rebound.
- **Cost:** about one fullscreen pass per effect per branch, at canvas size. N2 measures it with the existing GPU timer queries (`Lane::LiveGpuMs`) on the real show; the budget is the current headroom per frame.
- **Equality check:** the feedback blend's colour math moves from the patch into these passes. N2 has to prove the output is unchanged by diffing `--smoke-snapshots` before and after, on a copy of the show, within a small per-pixel tolerance.

## 5. projectM patches

| Patch | Change |
| --- | --- |
| 0002 (reworked), 0004 and 0005 (dropped) | `FeedbackBlend` loses contrast, saturation, gate and invert, and alpha-blends a straight-alpha texture with opacity 1. API: `projectm_set_layer_texture(handle, PROJECTM_LAYER_FEEDBACK, tex)`. The old setters go. |
| 0006 (new): before composite | In `MilkdropPreset::RenderFrame`, after the second `m_flipTexture.Draw` and before `m_finalComposite.Draw`, blend the before-composite layer into **the flip texture**, not `m_currentFrameBuffer`. After the swap, the current framebuffer becomes next frame's feedback, so drawing there would smear the video like the feedback layer. Only the composite reads the flip texture. |
| 0007 (new): texture override | `projectm_set_texture_override(handle, name, tex)` and `projectm_clear_texture_overrides(handle)`. `TextureManager::GetTexture` resolves overridden names to a non-owning `Texture` wrapping the caller's ID. To attach or detach an override on a **live** preset without reloading it, each sampler name's descriptor gets one level of indirection, so its texture can be swapped in place. Spike in N3. |

- **Workflow:** follow the existing patch workflow (`scripts/patches.sh start/export/finish`), then re-run `cmake -S . -B build`.
- **Listing a preset's texture samplers** for the preset node:
  - Scan the warp and composite shader text for `sampler_<name>`, leaving out the built-ins (`main`, `blur1`–`blur3`, `noise_*`, `pw_*`, `fw_*`, `fc_*`).
  - The library indexer already resolves texture names to flag missing textures (`src/library`); reuse that.
  - Each texture input's thumbnail comes from the thumbnail service's disk cache, or a small decode of the image file.

## 6. Switcher runtime

- **State:** for each switcher in the live scene, a `SwitcherState` on the main thread holds:
  - the current input;
  - the next input;
  - a `CycleTimer` (already shared by the video playlists).

  The rules are unchanged: beats and bars change on bar lines. Empty and unwired inputs are skipped.
- **Switching** updates `SwitcherSelection` and recompiles the plan.
  - **Preset change:** this uses the existing playlist path. `ScenePool` prepares the next selection's preset ahead, and the switch waits for it on the next bar line (today's `Engine::UpdatePlaylist` behaviour). `UpdatePlaylist` generalises to "the switcher whose selection changes the active preset".
  - **Video change:** this uses the existing video-playlist path in `VideoManager`. The next video waits ready, so the change is a clean cut.
  - **Look-only change:** just a new plan, applied on the next tick.
- **Several switchers in one graph** (e.g. a Video switcher upstream of a Frame switcher) run independently, each on its own clock. Switchers in series are allowed; only same-kind nesting is refused (section 1).
- **Preparing ahead:** for each live switcher, compile the plan with its next input selected. If that plan's preset differs, prepare it in the pool; if its video differs, warm it in `VideoManager`. This generalises the current "next playlist preset" and "next playlist video" preloads.
- **Unit tests:**
  - input order, and shuffle never picking the same input twice;
  - skipping empty inputs;
  - interval units;
  - waiting for a preset that isn't ready;
  - double-click to jump;
  - two switchers in series.

## 7. Node canvas and live editing

### Controls on the node

- **Layout:** each node is laid out like an Ableton Live device: a title bar (name, expand/collapse button), a body of ImGui controls, and pins on the edges. The controls are today's video widgets, moved out of Shell.cpp into one draw function per node type in `src/app/nodes/`.
- **Sizes:** each node type has a fixed width and its controls are sized for the canvas at 100% zoom, so the graph keeps a stable layout. Text is kept readable down to about 75% zoom; below that, nodes draw only their title bars.
- **Edits:** controls edit `NodeParams` through `TryEditGraph`, with coalesce keys for slider drags, so a drag is one undo step, as now.
- **Preset node:**
  - It shows a small live preview of its preset. The pool's prepared instance or the thumbnail service's preview, whichever is available, keeps this cheap.
  - It has one pin per texture sampler, each labelled with the name and a thumbnail.
- **Switcher node:** one row per input, each with a pin, a "passing" marker and a double-click target.

### Library

- **Spike:** a half-day spike at the start of N4 picks between **imnodes** (small, ImGui-native, MIT) and **thedmd/imgui-node-editor** (built-in pan and zoom, MIT, heavier).
- **Requirements:**
  - **Real widgets in node bodies:** sliders, combos, checkboxes and popups must work there, including under zoom. imgui-node-editor needs its suspend/resume calls around popups; check that combos open correctly.
  - It builds against our **ImGui 1.92.9b docking**. Check this against the library's release notes before choosing.
  - Typed pins, with link validation in our code.
  - Pan and zoom.
  - Node positions that we own and save.
- **Wrapper:** the library sits behind `src/app/GraphEditor.{hpp,cpp}`, so Shell.cpp only sees our types.

### Live edits

- **Syncing:** every `Document::Edit` already bumps `Revision()`, which the engine syncs on.
- **Values or wiring change:** recompile the live scene's plan and `SetPlan` it to the live lane. There are no shader compiles (section 4), so the change shows on the next frame.
- **The preset on the active path changes:** prepare it in the pool and swap when it's ready, like a cut.
- **A texture override is attached or detached:** set it through patch 0007 at the next tick, without reloading the preset.
- **Invalid edits never reach the engine:** they're refused before they're committed (section 1).

### Drag and drop

- Dropping a preset or video on the canvas creates the node; dropping it on a node replaces that node's preset or video.
- Dropping on a scene in the Scenes list still sets the preset on the scene's active preset node.

## 8. Canvas and viewport

- **Model:** `Performance` gains `struct Canvas { bool matchScreen{true}; int width{1920}; int height{1080}; }`. Match screen is today's behaviour and stays the default, so old shows don't change. Today's behaviour is `UpdateRenderSize` in main.cpp: native resolution, capped at `kMaxRenderWidth` = 1920.
- **Chosen size:** otherwise the app calls `Engine::SetRenderSize(width, height)` from the canvas. Lanes, slot textures, `VideoManager::SetCanvasSize` and the video fit already follow the render size.
- **Above 1920 wide** (decided: allowed): `kMaxRenderWidth` applies only to Match screen.
  - The viewport node and the frame-time readout turn amber when the live lane's GPU time exceeds the frame budget.
  - N6 tests 3840×1080 and 3840×2160 on the real show.
- **Output:** the canvas is presented scaled to fit the output window, letterboxed, in both Fullscreen Visuals and the preview panel. Thumbnails keep their own size.

## 9. Milestones

Each one is a PR to main. N1 and N2 keep the app visually unchanged. N6 is independent and can move earlier.

| # | Milestone | Size | Depends on | Done when |
| --- | --- | --- | --- | --- |
| N1 | Graph model, format v3, v2 migration, validation and `TryEditGraph`, plan compiler | M | none | Old shows load as graphs. Plans compiled from graphs equal plans from the v2 settings for the real show. The engine runs from plans with no visible change. |
| N2 | Effect passes on the lane; generic layer patch (0002 reworked, 0004/0005 dropped); fan-out; on top from branches | M–L | N1 | Smoke snapshots of the real show match today's within tolerance. GPU time is measured and within budget. |
| N3 | Before-composite layer (0006) and texture override (0007), attachable live | M, risky | N2 | Both layers work in a smoke run on presets with and without a composite shader, and with texture samplers. Attaching an override to a live preset doesn't reload it. |
| N4 | Node canvas: device-style nodes with controls in their bodies, wiring, blocked invalid edits with messages, expanding and collapsing, drag and drop, undo, live rewiring | L | N1, N2 (N3 for the two new inputs) | Every current scene feature can be edited on the canvas, and the old video section is gone. Invalid wiring is refused with a message. Rewiring the live scene doesn't stutter: held-frame counts in the smoke log stay at zero. |
| N5 | Switcher node replacing preset and video playlists | M–L | N1, N4 | Converted playlists behave as before: interval, bar lines, waiting for readiness, shuffle, double-click. Switchers fed by chains with different looks switch cleanly. |
| N6 | Viewport node and show canvas | S–M | N1 | Canvas sizes apply, are saved, and letterbox correctly. Match screen is unchanged. 3840×1080 and 4K are measured. |

**Ordering rationale:**
- N1 and N2 move today's behaviour onto the new architecture, with snapshot proof, before any UI depends on it.
- N3 is the main technical risk, so it comes before the editor needs it.
- N4 can start on its library spike while N3 is in progress.
- N5 comes last, because switchers need the canvas to be usable.

## 10. Risks

| Risk | Mitigation |
| --- | --- |
| Moving the blend math out of the patch changes the look of existing shows | Snapshot diffs in N2 against today's build, on a copy of the real show |
| Texture override on a live preset needs deeper changes in projectM's texture binding than expected | Spike first in N3. The fallback is to re-prepare the preset in the background and swap it in, which resets its feedback history once |
| GPU cost of fan-out and several passes at 4K | Run shared-prefix passes once; measure in N2 and N6; warn in the UI when over budget |
| Node library can't host real widgets (combos, popups) in node bodies, or is incompatible with ImGui 1.92.9b | Both are checked in the N4 spike. Both candidates are small enough to patch or vendor |
| Device-style nodes make big graphs crowded | Collapsing to title bars, fixed node widths, and title-only drawing when zoomed out |
| Graph edits racing with switcher changes on the live scene | Both run on the main thread and produce plans with revisions; the lane applies only the newest |

## 11. Open technical questions

1. **Straight or premultiplied alpha** for branch textures. Straight alpha matches today's shaders; premultiplied is cheaper to composite. Decide in N2.
2. **Canvas-size branch textures for small videos.** Running passes at the video's own size, then fitting it, would save GPU time but complicates fit and placement. Measure first.
3. **Built-in sampler list** for texture inputs. Confirm the exclusions against projectM 4.1.8's `TextureManager`.
4. **Old setters:** keep the old `projectm_set_feedback_*` setters as deprecated shims for a release, or drop them in N2? Nothing outside this app uses them.
