# Node Graph: Technical Details & Milestones

*Drafted 2026-11-12. Companion to `product-node-graph-2026-11-12.md`; read that first for what each node does.*

## 0. Summary

A scene's graph is **data in the show file**, edited on the main thread and **compiled into a render plan**: a flat, immutable description of what the lane draws each frame. Lanes never see the graph. Effect nodes become **small shader passes on the lane**, so rewiring only changes the plan and never compiles a shader; that is how "everything live" stays stutter-free.

Three projectM patch changes carry the new layers:
- the feedback blend becomes a generic **layer texture** blend (patches 0002, 0004 and 0005 collapse into one);
- a new **before-composite** blend point;
- a **texture override** so a preset's sampler can read a lane-owned video texture.

The first two milestones change no visible behaviour. They prove, with smoke-snapshot comparisons, that converted shows render as they do today before any new UI lands.

## 1. Graph model (`src/model`)

```cpp
enum class NodeType { VideoSource, Contrast, Saturation, LumaGate, Mix, Preset, Cycler, Viewport };
enum class PortKind { Video, Frame };
enum class Layer { Feedback, BeforeComposite, OnTop, Texture };

struct Node {
    std::string id;           // stable within the scene
    NodeType type;
    float x{0}, y{0};         // canvas position
    NodeParams params;        // std::variant of one struct per type
};
struct Wire {
    std::string fromNode;     // output side; outputs are single per node (Video or Frame)
    std::string toNode;
    Layer toLayer;            // preset inputs only; effect/viewport/cycler nodes have one input
    std::string textureName;  // Layer::Texture: the preset sampler it replaces
};
struct Graph { std::vector<Node> nodes; std::vector<Wire> wires; };
```

- `Scene` gains `Graph graph` and loses `presetPath`, `playlist` and `video` (kept only for reading old files). `beatHit`, `cc20Value`, `name` and `id` stay.
- **Cycler:** `CyclerParams` holds the interval (`IntervalUnit`, seconds, count), `shuffle`, and `branches`: a `std::vector<Graph>` without a viewport. Each branch has one *exit* node whose output is the cycler's output. Branches can't contain cyclers.
- **Viewport:** no per-scene params. The canvas lives on `Performance` (section 8).
- **Validation** (`ValidateGraph`, pure, unit-tested) returns a list of `{nodeId, reason}`:
  - exactly one viewport, with its input wired;
  - port kinds match, and each input takes at most one wire;
  - no cycles;
  - **at most one preset and one video source active at any time**, counting each cycler as one of its branches. This is the "chains, one preset" rule, and it keeps the engine's one-preset-per-scene and one-video-per-scene model.
- The engine runs the **last valid graph** of each scene. An invalid edit is saved and shown in amber, but doesn't reach the lanes.

## 2. File format v3 and migration

- `kCurrentFormatVersion = 3`. Scenes are written as `"graph": {"nodes": [...], "wires": [...]}`; node params use the existing field names (`lumaMin`, `softness`, ...).
- **Reading v2** converts each scene with `GraphFromV2Scene(const Scene&) -> Graph`, following the product doc's conversion table:
  - The fixed order today is contrast, then saturation, then the gate, then mix. The converter emits nodes in that order.
  - `invertLuma` becomes `LumaGate.invert` on every gate in the scene.
  - Overlay becomes a second branch from the same source: its own gate (with `softness`) and mix, into `Layer::OnTop`. Today's feedback gate is hard-edged, so the feedback-branch gate gets softness 0.
  - A preset playlist becomes a cycler with one branch per entry. Each branch is the preset plus that entry's `VideoLook` chain.
  - A video playlist becomes a cycler of video sources feeding the effect chain.
  - A playlist *and* a video playlist together become a preset cycler whose branches share one video cycler upstream. Section 6 covers how a source node outside the cycler feeds every branch.
- **Write v3 only.** The app keeps reading v1 and v2.
- **Tests:**
  - round-trips and each conversion case in `test_performance_json.cpp`;
  - a test that loads the real 16-scene show (from a fixture copy), converts it, compiles plans, and checks them against plans built from the v2 settings (section 3).

## 3. Render plan (`src/engine`)

`CompilePlan(const Graph&, const Performance&) -> RenderPlan`, pure and unit-tested:

```cpp
struct EffectPass { EffectType type; float a, b; bool flag; };   // contrast / saturation / gate / mix
struct VideoBranch { std::vector<EffectPass> passes; Layer layer; std::string textureName; };
struct RenderPlan {
    std::string presetPath;               // the active preset (cycler resolved)
    std::string videoId;                  // or Syphon app/server; empty: no video
    std::vector<VideoBranch> branches;    // fan-out: every chain from the active source
    uint64_t revision;
};
```

- `VideoSettings` (Lane.hpp) is replaced by `RenderPlan` minus the preset path. `Lane::UpdateVideo` becomes `Lane::SetPlan`, queued like a tick and applied at the start of the next tick. Plans are small, so they're copied.
- Branches that share a prefix (e.g. contrast, then saturation, feeding both the feedback and on-top chains) run the shared passes once. The compiler emits a small tree, not independent lists; that's an optimisation for N2, not a requirement.

## 4. Effect passes on the lane

- A new `VideoChain` per lane holds one fragment program per effect type, compiled when the lane starts. That's four programs, sharing `VideoOverlay`'s fullscreen-triangle vertex shader, and they're never recompiled.
- Each tick, the lane takes the live video frame (`VideoFrame`, unchanged). For each branch it runs the passes ping-pong between two lane-owned RGBA textures at canvas size:
  - Contrast and saturation change RGB.
  - Luma gate writes the mask into alpha, using today's step/smoothstep math from `VideoOverlay.cpp`, with invert.
  - Mix multiplies alpha.
  - The result is an RGBA texture with **straight alpha**, consumed by the layer.
- **Layers:**
  - **Feedback and before composite** go through projectM: `projectm_set_layer_texture(handle, layer, texture)`, blended alpha-over (section 5).
  - **On top:** `VideoOverlay` shrinks to a plain alpha-over draw of the branch texture after `projectm_opengl_render_frame_fbo`.
  - **Texture:** the branch result is copied into a **stable lane-owned texture per sampler name**, which the preset reads through the override (section 5). The ID stays the same frame to frame, so nothing is rebound.
- **Cost:** about one fullscreen pass per effect per branch at canvas size. N2 measures it with the existing GPU timer queries (`Lane::LiveGpuMs`) on the real show; the budget is the current headroom per frame.
- **Equality check:** the feedback blend's colour math moves from the patch into these passes. N2 has to prove the output is unchanged by diffing `--smoke-snapshots` before and after on a copy of the show, within a small per-pixel tolerance.

## 5. projectM patches

| Patch | Change |
| --- | --- |
| 0002 (reworked), 0004 and 0005 (dropped) | `FeedbackBlend` loses contrast, saturation, gate and invert; it alpha-blends a straight-alpha texture with opacity 1. API: `projectm_set_layer_texture(handle, PROJECTM_LAYER_FEEDBACK, tex)`. The old setters go. |
| 0006 (new): before composite | In `MilkdropPreset::RenderFrame`, after the second `m_flipTexture.Draw` and before `m_finalComposite.Draw`, blend the before-composite layer into **the flip texture**, not `m_currentFrameBuffer`. The current framebuffer becomes next frame's feedback after the swap, so drawing there would smear the video like the feedback layer. The flip texture is only read by the composite. |
| 0007 (new): texture override | `projectm_set_texture_override(handle, name, tex)` and `projectm_clear_texture_overrides(handle)`. `TextureManager::GetTexture` resolves overridden names to a non-owning `Texture` wrapping the caller's ID. To attach or detach an override on a **live** preset without reloading it, the descriptor for every sampler name gets one level of indirection, so its texture can be swapped in place. Spike in N3. |

- Follow the existing patch workflow (`scripts/patches.sh start/export/finish`), then re-run `cmake -S . -B build`.
- **Listing a preset's texture samplers** for the inspector: scan the warp and composite shader text for `sampler_<name>`, excluding the built-ins (`main`, `blur1`–`blur3`, `noise_*`, `pw_*`, `fw_*`, `fc_*`). The library indexer already resolves texture names to flag missing textures (`src/library`), so reuse that code.

## 6. Cycler runtime

- One `CyclerState` per cycler in the live scene, on the main thread: current branch, the next one, and a `CycleTimer` (already shared by the video playlists). The rules are unchanged: beats and bars change on bar lines.
- **Switching** recompiles the plan with the next branch resolved:
  - **Preset change:** the existing playlist path. `ScenePool` prepares the next branch's preset ahead, and the switch waits for it on the next bar line (today's `Engine::UpdatePlaylist` behaviour). `UpdatePlaylist` generalises to "the cycler that owns the preset".
  - **Video change:** the existing video-playlist path in `VideoManager`. The next video waits ready, so the change is a clean cut.
  - **Look-only change:** just a new plan, applied the next tick.
- **A source outside the cycler** (the v2 "playlist plus video playlist" case, or one video shared by every branch) is legal. Validation treats it as active for every branch.
- **Preparing the next branch:** for each live cycler, compile the plan for its next branch. Prepare that preset in the pool if it differs, and warm that video in `VideoManager` if it differs. That generalises the current "next playlist preset" and "next playlist video" preloads.
- **Unit tests:** branch order, shuffle (never the same twice), interval units, waiting for a preset that isn't ready, and double-click to jump.

## 7. Editor UI and live editing

- **Node editor library:** a half-day spike at the start of N4 picks between **imnodes** (small, ImGui-native, MIT) and **thedmd/imgui-node-editor** (built-in pan and zoom, MIT, heavier).
  - It must build against our **ImGui 1.92.9b docking**, with typed pins (link validation in our code), pan and zoom, and node positions we own and save. Check the version against the library's release notes before choosing.
  - Wrap the library behind `src/app/GraphEditor.{hpp,cpp}`, so Shell.cpp only sees our types.
- **Inspector:** move the existing video widgets from Shell.cpp into per-node-type functions. They edit `NodeParams` through the same `Document::Edit` calls, with coalesce keys for slider drags, so undo works as now.
- **Cyclers on the canvas:** branches are drawn inline, as rows inside an expanded cycler node, each with its own chain. Check in the N4 spike that the chosen library can nest nodes in a container (group) node, or draw branch rows as our own widgets inside one node.
- **Texture inputs:** the preset node's Texture input lists the preset's samplers by name, each with a thumbnail from the texture folder. The thumbnails come from the thumbnail service's disk cache, or a small decode of the image file.
- **Live edits:** every `Document::Edit` already bumps `Revision()`, which the engine syncs on.
  - **Values or wiring change:** recompile the live scene's plan and `SetPlan` it to the live lane. No shader compiles (section 4), so the change shows on the next frame.
  - **The preset changes:** prepare it in the pool and swap when ready, like a cut.
  - **A texture override is attached or detached:** set it through patch 0007 at the next tick, without reloading the preset.
  - **Validation fails:** keep the last valid plan.
- **Drag and drop:** dropping a preset or video on the canvas creates the node; dropping on a node replaces its preset or video. This keeps today's Scenes-list drops, so dropping on a scene still sets its preset node.

## 8. Canvas and viewport

- `Performance` gains `struct Canvas { bool matchScreen{true}; int width{1920}; int height{1080}; }`. Match screen is today's behaviour (`UpdateRenderSize` in main.cpp: native resolution, capped at `kMaxRenderWidth` = 1920) and stays the default, so old shows don't change.
- Otherwise the app calls `Engine::SetRenderSize(width, height)` from the canvas. Lanes, slot textures, `VideoManager::SetCanvasSize` and the video fit already follow the render size.
- **Output:** present the canvas scaled to fit the output window, letterboxed, in both Fullscreen Visuals and the preview panel. Thumbnails keep their own size.
- **Above 1920 wide** (decided: allowed): `kMaxRenderWidth` applies only to Match screen. Show the frame-time readout in amber when the live lane's GPU time exceeds the frame budget, and test 3840×1080 and 3840×2160 on the real show in N6.

## 9. Milestones

Each one is a PR to main. N1 and N2 keep the app visually unchanged; N6 is independent and can move earlier.

| # | Milestone | Size | Depends on | Done when |
| --- | --- | --- | --- | --- |
| N1 | Graph model, format v3, v2 migration, validation, plan compiler | M | none | Old shows load as graphs; the plans compiled from graphs equal the plans from v2 settings for the real show; the engine runs from plans with no visible change |
| N2 | Effect passes on the lane; generic layer patch (0002 reworked, 0004/0005 dropped); fan-out; on top from branches | M–L | N1 | Smoke snapshots of the real show match today's within tolerance; GPU time measured and within budget |
| N3 | Before-composite layer (0006) and texture override (0007), attachable live | M, risky | N2 | Both layers work in a smoke run on presets with and without a composite shader and with texture samplers; attaching an override to a live preset doesn't reload it |
| N4 | Node editor: canvas, inspector, wiring, validation display, drag and drop, undo, live rewiring | L | N1, N2 (N3 for the two new inputs) | Every current scene feature can be edited as nodes; the old video section is gone; rewiring the live scene doesn't stutter (held-frame counts in the smoke log stay at zero) |
| N5 | Cycler node replacing preset and video playlists | M–L | N1, N4 | Converted playlists behave as before (interval, bar lines, waiting for readiness, shuffle, double-click); branches with different looks cycle cleanly |
| N6 | Viewport node and show canvas | S–M | N1 | Canvas sizes apply, are saved, and letterbox correctly; match screen is unchanged; 3840×1080 measured |

**Ordering rationale:**
- N1 and N2 move today's behaviour onto the new architecture, with snapshot proof, before any UI depends on it.
- N3 is the main technical risk, so it comes before the editor needs it.
- N4 can start on its library spike while N3 is in progress.
- N5 last, because cyclers need the editor to be usable.

## 10. Risks

| Risk | Mitigation |
| --- | --- |
| Moving the blend math out of the patch changes the look of existing shows | Snapshot diffs in N2 against today's build, on a copy of the real show |
| Texture override on a live preset needs deeper changes in projectM's texture binding than expected | N3 spike first; fallback is to re-prepare the preset in the background and swap, which resets its feedback history once |
| GPU cost of fan-out and several passes at 4K | Shared-prefix passes; measure in N2 and N6; warn in the UI when over budget |
| Node editor library incompatible with ImGui 1.92.9b | Check in the N4 spike; both candidates are small enough to patch or vendor |
| Graph edits racing with cycler switches on the live scene | Both run on the main thread and produce plans with revisions; the lane applies only the newest |

## 11. Open technical questions

1. **Straight or premultiplied alpha** for branch textures. Straight alpha matches today's shaders; premultiplied is cheaper to composite. Decide in N2.
2. **Canvas-size branch textures for small videos.** Running passes at the video's own size, then fitting, would save GPU time, but complicates fit and placement. Measure first.
3. **Built-in sampler list** for the texture-layer inspector. Confirm the exclusions against projectM 4.1.8's `TextureManager`.
4. **Old setters.** Keep the old `projectm_set_feedback_*` setters as deprecated shims for a release, or drop them in N2? Nothing outside this app uses them.
