# Node Graph: Implementation Plan

*Drafted 2026-11-16, after PR #34 merged. It turns `technical-node-graph-2026-11-12.md` into file-by-file work for each milestone, N1–N6. Read the product and technical docs first. This doc says how each part gets built, tested and proven, and lists the decisions that need your answer before or during each milestone.*

## 0. What changed since the technical doc

Reading the code for this plan turned up corrections and simplifications. The technical doc is updated in this PR where it was wrong.

| # | Finding | Effect on the plan |
| --- | --- | --- |
| C1 | **Today's feedback gate isn't hard-edged.** Patch 0002 uses `smoothstep(lumaMin - 0.05, lumaMin + 0.05, luma)`, and the overlay uses `step` when its softness is 0. | Converted feedback chains get a Luma gate with **softness 0.1**, which is the same curve. Converted overlay chains keep their own softness. |
| C2 | **Smoke snapshots aren't repeatable.** Live audio, video timing and preset randomness change every run, so diffing snapshots before and after can't prove anything. | N2 proves equality with a **deterministic GPU test** (§N2.6). Smoke snapshots become a visual check for you. |
| C3 | **One shader pass per branch, not one per effect.** A single "effect chain" shader can run a list of up to 16 effects from uniforms, in one draw, with nothing recompiled when the list changes. | Lower GPU cost; no intermediate textures inside a branch; the colour math runs in float registers exactly as today. Running shared prefixes once is no longer needed. |
| C4 | **"Only one preset active" follows from the port kinds.** A preset has no Frame inputs, and a Frame switcher can't feed a Frame switcher, so at most one preset can ever be active. | Validation only has to check the **video** side (§N1.4). That check is cheap and has no exponential worst case. |
| C5 | **Texture override can avoid live descriptor swaps.** projectM 4.1.8 caches textures by name per instance (`TextureManager::m_textures`), and `Texture` can wrap an existing GL id. | Patch 0007 becomes small: the lane pre-registers its own texture for each of the preset's textures before loading the preset, then only changes what it copies into it (§N3.3). |
| C6 | **The real show has 19 scenes**, and 14 pool videos. Two scenes combine a preset playlist with a Syphon source, and one has a video playlist turned on with a Syphon source, which today ignores the playlist. | Conversion drops a video playlist on Syphon sources, with a load note. The migration test uses a fixture copy of all 19 scenes. |

## 1. Ground rules for every milestone

- One PR per milestone, targeting main. The next one opens after the previous one merges.
- **Verification**, run by me before opening each PR:
  - `cmake --build build` with no warnings (`gv_warnings` stays on);
  - `ctest --test-dir build`;
  - smoke runs on a scratchpad copy of the show only, never the real app or the real show file:
    `build/src/govtech-visualizer --smoke --performance <scratch copy> --smoke-seconds 120 --smoke-cut-every 3 --smoke-report-every 20` (plus `--smoke-edits` and `--smoke-random`).
- **PR descriptions** include smoke stats before and after: held ticks, held near cut, live GPU ms per lane, and memory at start and end.
- **Anything that needs a real window** (canvas interaction, fullscreen letterboxing) is listed in the PR as "please try" steps for you.
- **Commits** stage explicit paths only.

## 2. Decisions needed

Each is marked with the milestone that needs it. My recommendation comes first.

| # | Needed by | Question | Recommendation |
| --- | --- | --- | --- |
| D1 | N1 | Until the canvas lands (N4), how is a scene edited? | **A temporary "classic" bridge** (§N1.6). The current Scene panel keeps working unchanged, reading and writing graphs through two conversion functions, and N4 deletes it. The alternative is building the canvas first, which leaves main unable to edit video between N1 and N4. |
| D2 | N1 | First save of a v2 show in the new format | **Write a one-time backup** `<name>.v2-backup.json` next to it, then save v3. Older builds can't open v3. |
| D3 | N2 | Texture format of branch results | **RGBA8.** One write per branch, so the difference from today is at most 1/255 per channel. RGBA16F doubles memory, about 66 MB per branch at 4K. |
| D4 | N2 | Remove the old `projectm_set_feedback_*` setters? | **Remove them.** Only this app uses them, and keeping both paths doubles the testing. |
| D5 | N4 | Preset node preview | The product doc asks for a small live preview. Rendering every preset node live costs a lane render each. **Proposal:** the library thumbnail on every preset node, plus the real output on the node that's live. |
| D6 | N3 | Random textures (`sampler_rand00` and so on) | **Not offered as texture inputs** in the first version. projectM picks their image at random when the preset loads, so the name doesn't say which image it is. |
| D7 | — | Should N6 (canvas) move earlier? | **Yes: right after N1.** It's small, visible, and doesn't touch the lane work. |

## 3. Milestone order

```
N1 model + format + compiler ──► N6 canvas (D7) ──► N2 lane effect passes ──► N3 layer + texture patches
                                                                     └──► N4 node canvas (spike can start during N3) ──► N5 switchers
```

---

## N1: Graph model, format v3, migration, validation, plan compiler

**Goal:** shows load and save as graphs. The app looks and behaves exactly as today. Size: M, about 2,000 lines including tests.

### N1.1 New files

| File | Contents |
| --- | --- |
| `src/model/Graph.hpp/.cpp` | `NodeType`, `PortKind`, per-type param structs, `NodeParams` variant, `Node`, `Wire`, `Graph`; lookup helpers; pure edit helpers (§N1.5) |
| `src/model/GraphValidate.cpp` | `ValidateGraph` |
| `src/model/GraphConvert.hpp/.cpp` | `GraphFromV2Scene`, the classic bridge (§N1.6), and the default layout of node positions |
| `src/model/LegacyScene.hpp` | Today's `VideoLook`, `ScenePlaylist`, `SceneVideo` and `VideoPlaylist`, moved here and only used by the v2 parser and the bridge |
| `src/engine/RenderPlan.hpp/.cpp` | `RenderPlan`, `EffectPass`, `VideoBranch`, `SwitcherSelection`, `CompilePlan` |
| `tests/model/test_graph.cpp`, `test_graph_convert.cpp`, `tests/engine/test_render_plan.cpp` | Unit tests |
| `tests/fixtures/govtech-nov-6th.json` | A copy of the real show, with absolute paths kept. Missing files only produce warnings, so the test doesn't depend on your Movies folder. |

### N1.2 Types

```cpp
enum class NodeType { VideoSource, Contrast, Saturation, LumaGate, Mix, Preset, Switcher, Viewport };
enum class PortKind { Video, Frame };
enum class PresetLayer { Feedback = 0, BeforeComposite = 1, OnTop = 2, Texture = 3 };

struct VideoSourceParams { VideoSource source; bool restartOnEnter{true}; bool loop{true}; };
struct ContrastParams    { float contrast{1.0f}; };          // 0..kMaxContrast
struct SaturationParams  { float saturation{1.0f}; };        // 0..2
struct LumaGateParams    { float threshold{0.0f}; float softness{0.0f}; bool invert{false}; };
struct MixParams         { float opacity{1.0f}; };
struct PresetParams      { std::filesystem::path path; };
struct SwitcherParams    { PortKind kind{PortKind::Frame}; IntervalUnit unit{IntervalUnit::Seconds};
                           double intervalSeconds{30.0}; int intervalCount{4}; bool shuffle{false};
                           int inputCount{1}; };             // always ends with one empty input
struct ViewportParams    {};
using NodeParams = std::variant<VideoSourceParams, ContrastParams, SaturationParams, LumaGateParams,
                                MixParams, PresetParams, SwitcherParams, ViewportParams>;
```

`Node`, `Wire` and `Graph` are as in the technical doc. `Wire::toInput` holds a `PresetLayer` value for presets, and the input index for switchers.

- **Node ids:** a type prefix plus a number, unique within the scene (`preset-1`, `gate-2`). They're never reused within a scene.
- **Limits:** 256 nodes per scene and 256 switcher inputs (`kMaxPlaylistCount`).
- **Port table**, a `constexpr` function each, so the rules live in one place:
  - `OutputKind(type)` → VideoSource Video; effects Video; Preset Frame; Switcher its kind; Viewport none.
  - `InputKind(node, input)` → effects input 0 Video; Preset inputs 0–3 Video; Switcher inputs its kind; Viewport input 0 Frame.
- **`Scene` changes:** it loses `presetPath`, `playlist` and `video`, and gains `Graph graph`. The not-serialized `presetMissing`, `videoMissing` and `playlistMissing` become `std::set<std::string> missingNodes`, filled by `CheckFiles`.
- **Old helpers:** `Presets()`, `VideoIds()`, `UsesVideo()` and `FirstPreset()` become free functions over the graph in `Graph.hpp`. `PresetsOf` returns every preset node's path in node order. `FirstPreset` is the preset active with every switcher on input 0.

### N1.3 File format v3

```json
{ "version": 3, "name": "...", "videos": [ ... unchanged ... ],
  "scenes": [ { "id": "2h-intro", "name": "...", "cc20Value": 0, "beatHit": { ... },
    "graph": {
      "nodes": [
        { "id": "video-1", "type": "videoSource", "x": 40, "y": 80, "expanded": true,
          "source": { "type": "file", "video": "v3" }, "restartOnEnter": true, "loop": true },
        { "id": "gate-1", "type": "lumaGate", "x": 520, "y": 80, "expanded": true,
          "threshold": 0.4, "softness": 0.1, "invert": true },
        { "id": "preset-1", "type": "preset", "x": 900, "y": 80, "expanded": true, "path": "/…/x.milk" },
        { "id": "viewport", "type": "viewport", "x": 1200, "y": 80, "expanded": true }
      ],
      "wires": [ { "from": "gate-1", "to": "preset-1", "input": "feedback" },
                 { "from": "x", "to": "preset-1", "input": "texture", "texture": "clouds" },
                 { "from": "y", "to": "switcher-1", "input": 2 } ] } } ] }
```

- **Field names:** params keep today's names where they exist. `lumaMin` becomes `threshold`, since a gate now stands alone. The format is new, so this is the moment to rename.
- **Preset inputs** are written as names (`feedback`, `beforeComposite`, `onTop`, `texture`), so the file reads clearly.
- **Parsing** follows the existing `ReadField` and `Issue` style. Unknown node types and malformed wires produce an `Issue` and are dropped. Then `ValidateGraph` runs (§N1.4). A scene that fails it has its viewport wire removed (it loads unfinished), with a warning naming the scene and the reason.
- **Serializing** writes nodes in their stored order and wires sorted, so saves are stable and diffs stay readable.
- **Versions:** `kCurrentFormatVersion = 3`. v1 and v2 still load: they're parsed into the legacy structs as today, `MigrateFileSources` runs, then `GraphFromV2Scene` converts each scene.
- **D2 backup:** handled in the app's save path (`main.cpp`), not in `SavePerformance`. It applies when the document was loaded from a file older than v3 and the backup doesn't exist yet.

### N1.4 Validation

`ValidateGraph(const Graph&) -> std::vector<GraphError>`, where `GraphError` is `{std::string nodeId; std::string message}`. The messages are written for the artist, because the editor shows them.

1. **Structure:**
   - exactly one viewport;
   - wire ends exist and input indices are in range;
   - at most one wire per input (texture inputs are keyed by name);
   - port kinds match.
2. **No loops:** a depth-first search over the wires.
3. **Same-kind switchers:** no switcher reachable upstream of another switcher of the same kind. Walk upstream from each switcher.
4. **One video source** (C4), defined through a node's **root**: walk upstream through effect nodes, which have a single input, until reaching a video source or a Video switcher.
   - Every wired video input of a given preset must have the **same root**. Fan-out from one source or one switcher is fine.
   - A Video switcher's inputs each lead, through effects, to a video source, never another Video switcher (that's rule 3).
   - A chain with no root (effects only) is **unfinished**: it's allowed and carries no video.
5. **Limits:** node count, switcher input count, parameter ranges (the existing `ValidateLook` ranges, moved), and `inputCount` equal to the highest wired input + 2.

The rules are linear in nodes and wires, so the editor can run them on every drag-hover (§N4.4).

### N1.5 Edits

- **Pure helpers** in `Graph.cpp`, each returning the new node's id or a bool:
  - `AddNode(graph, type, x, y)`;
  - `RemoveNode(graph, id)`, which refuses the viewport and drops attached wires;
  - `Connect(graph, from, to, input, texture)`, which replaces any wire already in that input;
  - `Disconnect(graph, to, input, texture)`;
  - `MoveNode`;
  - `SetExpanded`;
  - `NormalizeSwitcherInputs`, which keeps exactly one empty input at the end and compacts nothing, so input numbers stay stable.
- **The gate**, in `Document.hpp`:

  ```cpp
  auto TryEditGraph(Document& document, int scene, const std::function<void(Graph&)>& edit,
                    std::string label, std::string coalesceKey = {}) -> std::optional<std::string>;
  ```

  It copies the performance, applies `edit` to the scene's graph, then validates. If the graph is valid it calls `Document::Edit` and returns nullopt; otherwise it returns the first error's message.
- **Existing whole-show edits move onto graphs:**
  - `ReplacePreset(performance, scene, nodeId, path)`;
  - `AssignVideo` sets the source on the scene's video source node, adding a source → feedback chain if there's none;
  - `VideoUsage` and `RemovePoolVideo` scan video source nodes;
  - `AddScene(…, presetPath)` builds a minimal graph: preset → viewport.

### N1.6 The classic bridge (D1, deleted in N4)

```cpp
struct ClassicScene { std::filesystem::path presetPath; ScenePlaylist playlist; SceneVideo video; };
auto ClassicFromGraph(const Graph&) -> std::optional<ClassicScene>;   // nullopt: not a classic-shaped graph
auto GraphFromClassic(const ClassicScene&) -> Graph;                  // same as GraphFromV2Scene
```

- **Reading:** Shell's scene section reads a `ClassicScene` once per frame for the selected scene and draws exactly as today. Draw code that keeps references keeps them into that per-frame copy.
- **Writing:** every widget edit becomes `TryEditGraph(…, [&](Graph& g){ g = GraphFromClassic(edited); })`.
- **Engine and VideoManager** read the classic view for playlist timing and video playlists until N5. The video **look** is the exception: it comes from `CompilePlan` (§N1.7).
- **Positions and ids:** `GraphFromClassic` lays nodes out in fixed columns (source, contrast, saturation, gate, mix, preset, switcher, viewport), with deterministic ids. Nothing can move a node before N4, so regenerating loses nothing.
- **Key test:** `ClassicFromGraph(GraphFromClassic(c)) == c` for every scene of the fixture show, and for generated cases (each playlist type, overlay on and off, invert, Syphon, video off).

### N1.7 Plan compiler

```cpp
struct EffectPass { enum class Type { Contrast, Saturation, LumaGate, Mix } type; float a{0}, b{0}; bool flag{false}; };
struct VideoBranch { std::vector<EffectPass> passes; PresetLayer layer; std::string textureName; };
struct RenderPlan {
    std::string presetNode; std::filesystem::path presetPath;     // empty: nothing connected (black)
    std::string sourceNode; VideoSource source;                   // type File with empty videoId: no video
    std::vector<VideoBranch> branches;
    auto operator==(const RenderPlan&) const -> bool = default;
};
using SwitcherSelection = std::map<std::string, int>;             // switcher node id → input; missing = first wired
auto CompilePlan(const Graph&, const SwitcherSelection&) -> RenderPlan;
```

- **Walk:**
  1. Start at the viewport's input and follow the Frame side to the preset, through the selected input of a Frame switcher if there is one.
  2. For each wired video input of that preset, walk upstream collecting effects (stored in source → preset order), through the selected input of a Video switcher, until reaching the source.
  3. Each such input becomes one `VideoBranch`.
- **Classic look:** `VideoSettingsFromPlan(const RenderPlan&) -> VideoSettings` maps a classic-shaped plan back to today's lane settings. N1 uses it in `Engine::VideoSettingsFor`, so the lanes are untouched.
- **Equivalence test:** for every scene and playlist entry in the fixture, `VideoSettingsFromPlan(CompilePlan(graph, {frameSwitcher: item}))` equals today's `VideoSettingsFor(scene, item)`. This is the proof that N1 changes nothing on screen.

### N1.8 Other touch points

| File | Change |
| --- | --- |
| `Performance.hpp` | Moves the legacy structs out. Fixes the misplaced `VideoFit` comment. |
| `PerformanceJson.cpp` | v3 read and write, v2 → graph, `Validate` calls `ValidateGraph`, and `CheckFiles` fills `missingNodes` |
| `Document.cpp/.hpp` | `TryEditGraph`; edits moved onto graphs |
| `Engine.cpp` | Reads presets and playlist timing through the classic view; `VideoSettingsFor` goes through the plan; `UpdatePerformance` compares plans instead of looks |
| `VideoManager.cpp` | Reads video sources and the video playlist through the classic view |
| `Shell.cpp` | Uses the bridge (§N1.6). No visible change. |
| `main.cpp` | D2 backup on save; smoke edits go through the new edit helpers |
| `Library.cpp/.hpp` | "Used in scenes" goes through `PresetsOf` |
| `scripts/convert_fork_performance.py` | Writes v2 as now, and the app converts. A note in the script says so. |
| tests | `test_performance_json.cpp` and `test_document.cpp` are updated for the new fields. About 140 references move to graph helpers or the classic bridge. |

### N1 done when

- [ ] All tests pass, including the new graph, validation, conversion, round-trip and equivalence tests.
- [ ] The fixture show converts, validates with no errors, and compiles plans equal to today's settings for every scene and entry.
- [ ] A smoke run with `--smoke-edits` on a scratch copy matches main's stats (held ticks within noise), and the saved file is v3 and reloads.
- [ ] You open your show in the PR build and see no difference. That's your check, listed in the PR.

---

## N6: Show canvas (proposed to run second, D7)

**Goal:** a per-show output size; Match screen is unchanged. Size: S–M. The viewport node's controls come with N4; until then the canvas size is set in **Setup → Output**.

- **Model:**
  - `struct Canvas { bool matchScreen{true}; int width{1920}; int height{1080}; }` on `Performance`, saved as `"canvas"` (omitted when it's Match screen).
  - Allowed sizes: width and height from 320 to 7680, even numbers. Presets: 1920×1080, 1280×720, 3840×2160, 3840×1080.
- **`main.cpp` `UpdateRenderSize`:** with Match screen it works as now, capped at `kMaxRenderWidth`. Otherwise it uses the canvas size, with no cap. It calls `Engine::SetRenderSize` only when the size changes; that call already reconfigures lanes, slot textures and `VideoManager::SetCanvasSize`.
- **Letterboxing:**
  - The fullscreen output draw and the preview panel compute an aspect-fit rectangle (`FitRect(canvasW, canvasH, targetW, targetH)`, a unit-tested pure function) and clear the rest to black.
  - Thumbnails and the editor snapshot are unchanged.
- **Over budget:** `Engine::GetStats().laneGpuMs` for the live lane is averaged over a second. Above 16.7 ms it turns the frame-time readout amber, plus the viewport node later.
- **Tests:**
  - `FitRect`;
  - Canvas JSON round-trip;
  - Validate rejecting bad sizes;
  - smoke runs at 3840×1080 and 3840×2160 on a scratch copy with the canvas set, with GPU ms and held ticks recorded in the PR.
- **Please try:** fullscreen at 3840×1080 on your display, to check the black bars and that it's sharp.

---

## N2: Effect passes on the lane, generic layer patch

**Goal:** the lane renders from `RenderPlan`. Contrast, saturation, gate and mix run in our shader, and projectM only blends a finished texture. It looks the same as today, proven by test. Size: M–L.

### N2.1 Effect chain shader (C3)

`src/engine/VideoChain.hpp/.cpp`, one per lane, created on the lane thread at start-up:

```glsl
#version 330 core
uniform sampler2D uVideo;
uniform int  uCount;             // 0..16
uniform int  uType[16];          // 0 contrast, 1 saturation, 2 gate, 3 mix
uniform vec2 uParam[16];         // contrast: (c,_) saturation: (s,_) gate: (threshold, softness) mix: (opacity,_)
uniform bool uFlag[16];          // gate: invert
in vec2 vUv; out vec4 fragColor;
void main() {
    const vec3 w = vec3(0.299, 0.587, 0.114);
    vec3 c = texture(uVideo, vUv).rgb; float a = 1.0;
    for (int i = 0; i < uCount; ++i) {
        if (uType[i] == 0)      c = (c - 0.5) * uParam[i].x + 0.5;          // colour passes: no clamp
        else if (uType[i] == 1) c = mix(vec3(dot(c, w)), c, uParam[i].x);
        else if (uType[i] == 2) { c = clamp(c, 0.0, 1.0); float l = dot(c, w); float e = uParam[i].y * 0.5;
                                  float m = e > 0.0 ? smoothstep(uParam[i].x - e, uParam[i].x + e, l) : step(uParam[i].x, l);
                                  a *= uFlag[i] ? 1.0 - m : m; }
        else                    a *= uParam[i].x;
    }
    fragColor = vec4(clamp(c, 0.0, 1.0), a);
}
```

- **Exact match with today:** today's shaders run contrast and saturation unclamped, then clamp once, then gate. Clamping after each colour pass wouldn't match: saturation can pull values that contrast pushed out of range back inside [0, 1]. So, as shown above, colour passes leave values unclamped, and the gate and the chain's end clamp. For converted chains (contrast, saturation, gate, mix) this is today's math, step for step. N2.6 proves it.
- **Output:** one draw per branch into that branch's lane-owned RGBA8 texture (D3) at render size, stored with straight alpha. Branch textures are kept per (instance, branch index) and reallocated only when the render size changes.
- **Video alpha:** sources may carry alpha, but today's shaders ignore it, so the chain starts from alpha = 1 as shown.
- **No passes** (a source wired straight into a layer) still runs the shader, with count 0, so every layer gets the same straight-alpha input.

### N2.2 Patch rework: layer texture API

In a `scripts/patches.sh start` worktree:

1. Reset to after 0001 and keep 0003.
2. Write a new 0002, "layer-texture-api": `FeedbackBlend` becomes `LayerBlend`, a shader that draws `texture(uTexture, vUv)` with `glBlendFuncSeparate(SRC_ALPHA, ONE_MINUS_SRC_ALPHA, ZERO, ONE)`, exactly as today's state handling.
3. API:

   ```c
   typedef enum { PROJECTM_LAYER_FEEDBACK = 0, PROJECTM_LAYER_BEFORE_COMPOSITE = 1 } projectm_layer;
   PROJECTM_EXPORT void projectm_set_layer_texture(projectm_handle, projectm_layer, uint32_t texture); // 0 clears
   ```

   `BEFORE_COMPOSITE` is accepted and stored, but it's only drawn from N3.
4. Delete 0002, 0004 and 0005, keep the numbering gap, and update `third_party/patches/README.md`.
5. Re-run `cmake -S . -B build`.

### N2.3 Lane changes

- **Plans replace video settings:** `VideoSettings` is deleted. `Lane::Prepare(..., LanePlan plan, ...)` and `Lane::SetPlan(InstanceId, LanePlan)` replace `UpdateVideo`. `LanePlan` is `RenderPlan` without the preset or source, which is the branches only.
- **`DoTick` for the live instance:**
  1. When the plan has branches and the video frame isn't 0, `VideoChain::Run(frame, branch, target)` runs for each branch.
  2. Set the feedback layer: `projectm_set_layer_texture(FEEDBACK, tex or 0)`.
  3. Render.
  4. For each `OnTop` branch, alpha-over draw its texture onto the slot. `VideoOverlay` shrinks to this simple composite, sharing the fullscreen-triangle vertex shader.
- **Warm-up** keeps the 1×1 placeholder video, so projectM's layer blend program is compiled during preparation, not on the first live frame.
- **Engine:** `UpdatePerformance` compares compiled plans per hosted instance and sends `SetPlan` when they differ. Prepares pass the plan for the instance's item.

### N2.4 Fan-out

A scene with an overlay already compiles to two branches, feedback and on top, from one source, so fan-out works as soon as N2.3 lands. No new UI is needed; N4 exposes it.

### N2.5 Cost

- **Today:** one feedback blend draw, plus one overlay draw when the overlay is on.
- **After N2:** one chain draw per branch, plus one blend per layer. For a converted overlay scene that's 4 cheap fullscreen draws instead of 2.
- **Expected impact:** under 0.3 ms at 1080p on the M1. It's recorded in the PR from `LiveGpuMs`, before and after, on the scenes in your show that use the overlay.

### N2.6 Proving equality (C2)

- **A GPU test executable**, `tests/gl/test_video_chain.cpp`. It's a Catch2 binary, `gv_gl_tests`, that opens a hidden SDL window with a GL 4.1 core context.
  - It renders a fixed test image (gradients, a colour wheel, noise) through **today's shader**, copied verbatim into the test as the reference, into an RGBA8 target over a fixed background.
  - It renders the same image through `VideoChain` plus the new layer blend.
  - It compares pixels over about 200 parameter sets: random looks, invert on and off, softness 0 and above, mix 0–1, and contrast at the extremes.
  - **Pass:** at most 1/255 difference per channel.
- **Running it:** with ctest, labelled `gl`. It's skipped with a message if no GL context can be created.
- **The overlay** gets the same test against today's `VideoOverlay` shader.
- **Visual check for you:** smoke snapshots from main and from the PR build, side by side, linked in the PR.

### N2 done when

- [ ] `gv_gl_tests` passes, with feedback and overlay within 1/255.
- [ ] Smoke runs show no new held ticks, and GPU ms are recorded.
- [ ] Patches 0004 and 0005 are gone, and a clean configure applies the patch set.
- [ ] You compare the snapshots and approve.

---

## N3: Before-composite layer and texture override

**Goal:** the two new injection points work in the engine, testable before the canvas exists. Size: M; this is the riskiest milestone.

### N3.1 Patch 0006: before composite

- **Where:** in `MilkdropPreset::RenderFrame`, after `m_flipTexture.Draw(current, nullptr, true, false)` and before `m_finalComposite.Draw`:
  1. bind the flip texture's framebuffer;
  2. `LayerBlend::Draw(beforeCompositeTexture, flipY = true)`, since the flip texture is upside down relative to video;
  3. restore the binding.
- **Presets without a composite shader:** the default composite still samples `mainTexture`, which is the flip texture, so the layer shows. The second flip (lines 167–170) is unaffected.
- **Test hooks:** a smoke option, `--smoke-layer before-composite`, forces the first scene's video into this layer, so I can check it on presets with and without a composite shader before the editor exists.

### N3.2 Listing a preset's textures

- **`library::PresetTextures(const std::filesystem::path&) -> std::vector<std::string>`** scans the warp and composite shader text for `sampler_<name>`.
  - It leaves out the built-ins `main`, `blur1`–`blur3`, `noise_lq`, `noise_mq`, `noise_hq`, `noisevol_lq`, `noisevol_hq`, `pw_*`, `fw_*`, `fc_*` and `pc_*`.
  - It also leaves out `rand*` (D6).
- **Checked against projectM:** the exclusions are checked against `TextureManager` in 4.1.8 (its built-in names) as part of this milestone.
- **Caching:** results are cached in the library index next to the missing-texture check, which already parses the same text.

### N3.3 Patch 0007: texture override (C5)

```c
PROJECTM_EXPORT void projectm_set_texture_override(projectm_handle, const char* name, uint32_t texture, int width, int height);
PROJECTM_EXPORT void projectm_clear_texture_overrides(projectm_handle);
```

- **Inside projectM:** the override inserts a non-owning `Texture(name, id, GL_TEXTURE_2D, width, height, true)` into the instance's `TextureManager::m_textures` cache. A flag added in the patch stops `~Texture` from deleting a GL id it doesn't own. A later `GetTexture(name)` from the preset's shaders then finds it.
- **The lane, before loading a preset with textures:**
  - for **every** texture the preset uses, it creates a lane-owned texture sized like the original image file (loaded with stb, which is already a dependency);
  - it uploads the original image into it;
  - it registers that texture as the override.
- **Live attach and detach:** an attached texture input copies the branch result into that texture each tick, with a scaled `glBlitFramebuffer`. Detaching re-uploads the original image. The preset never reloads or rebinds, because the GL id never changes.
- **Size:** keeping the original image's size keeps projectM's `texsize_<name>` uniforms correct.
- **Cost:** the extra textures exist only for presets that use textures, on instances that are hosted.
- **Fallback**, if the cache insert doesn't behave (for example, the preset resolves textures before the override): re-prepare the preset in the background with overrides set, and swap it in, as a cut. That resets its feedback history once. This is decided in the first two days of N3.

### N3.4 Plan and lane wiring

- `RenderPlan` branches already carry `layer` and `textureName`.
- **The lane:**
  - before composite → `projectm_set_layer_texture(BEFORE_COMPOSITE, tex)`;
  - texture → blit into the override texture for that name.
- **Before the canvas exists,** these are reachable only through the smoke flags in N3.1 and the unit tests in `test_render_plan.cpp`.

### N3 done when

- [ ] Smoke runs with `--smoke-layer before-composite` and `--smoke-layer texture:<name>` on presets with and without a composite shader, and with textures, produce visible video in snapshots (checked by you), with no held ticks.
- [ ] Attaching and detaching a texture override on a live preset doesn't reload it: the instance id is unchanged in the log, and no prepare happens.
- [ ] `PresetTextures` tests cover the built-in exclusions.

---

## N4: Node canvas

**Goal:** edit a scene's graph on a canvas whose nodes carry their own controls. The old video section and the classic bridge are deleted. Size: L, the largest milestone.

### N4.1 Library spike (half a day, can start during N3)

- **Build both candidates** against ImGui 1.92.9b-docking in a scratch branch: imnodes and thedmd/imgui-node-editor.
- **Each must pass:**
  - a node with a slider, a combo, a checkbox and a popup menu, all working at 75%, 100% and 150% zoom;
  - pan and zoom;
  - typed pins with our own validation;
  - our own saved positions;
  - 100 nodes at 60 fps in the editor.
- **Result:** a short note in the PR with the choice and why. The chosen library is vendored with `FetchContent` at a pinned tag, the way ImGui is.

### N4.2 Structure

| File | Contents |
| --- | --- |
| `src/app/GraphEditor.hpp/.cpp` | Wraps the library: `Draw(const Graph&, const GraphView& state, GraphActions&)`; owns zoom and pan per scene (not saved); hit-testing; the wire being dragged |
| `src/app/nodes/NodeDraw.hpp` | `DrawNodeBody(const Node&, NodeContext&) -> std::optional<NodeEdit>`, one per type |
| `src/app/nodes/VideoSourceNode.cpp`, `EffectNodes.cpp`, `PresetNode.cpp`, `SwitcherNode.cpp`, `ViewportNode.cpp` | Each node's body, using today's widgets moved out of `Shell.cpp` (about 650 lines move) |
| `Shell.cpp` | `DrawSceneProperties` keeps name, CC and beat hits, then hosts the canvas. The video section, playlist editors and the classic bridge are deleted. |

### N4.3 Node layout

- **Fixed widths:** effects 180 px, source 240, preset 260, switcher 220, viewport 220.
- **Title bar:** the name, the expand/collapse button, and a state dot (live, ready, missing file).
- **Below 75% zoom,** nodes draw only their title bar.
- **Preset node (D5):** the thumbnail, or the live output when it's the live node; the preset name; a "Choose…" button; then one pin row per layer and one per texture, with the texture's name and thumbnail.

### N4.4 Interactions

| Action | Behaviour |
| --- | --- |
| Add | Right-click the canvas → menu of node types. Dropping a preset or video on empty canvas adds the node. |
| Replace | Dropping a preset or video on a node of that type replaces its file. |
| Wire | Drag from an output; hovering an input runs `ValidateGraph` on a trial copy, and inputs that would fail are dimmed. Release on a dimmed input shows the reason. |
| Delete | Select, then Delete or Backspace. The viewport refuses, with a message. |
| Move | Drag a title bar. One undo step per drag (coalesce key `move:<scene>:<node>`). |
| Errors | A message near the mouse for 3 seconds, from `TryEditGraph`'s return value. |
| Undo and redo | Unchanged: `Document` snapshots. |

### N4.5 Live edits

- **Each frame:**
  1. If `Document::Revision()` changed, the engine recompiles the live scene's plan.
  2. If the plan's preset is unchanged, it calls `SetPlan` on the next tick.
  3. If the preset changed, it prepares the new one with priority and cuts to it when it's ready.
- **New smoke script steps,** added to `--smoke-edits` and run through the same edit helpers:
  - add a Contrast node mid-chain;
  - rewire on top → before composite;
  - attach a texture input;
  - delete a node;
  - undo all of it.

### N4 done when

- [ ] Everything the old Scene panel could do can be done on the canvas. A checklist is in the PR.
- [ ] Converted shows open with a readable default layout.
- [ ] The extended `--smoke-edits` run has zero held ticks caused by edits.
- [ ] You build a scene from scratch and rewire the live scene during playback. The "please try" list is in the PR.

---

## N5: Switchers

**Goal:** the switcher node replaces preset and video playlists in the engine, and adds the switcher's own controls on the node. Size: M–L.

### N5.1 Runtime

- **`src/engine/SwitcherClock.hpp/.cpp`:** pure and unit-tested.
  - It holds the current input, the next input, and today's `CycleTimer` and pulse logic, extracted from `Engine::UpdatePlaylist` and `VideoManager::CycleVideos`.
  - It skips unwired inputs and picks shuffled inputs without immediate repeats.
- **The engine** owns one `SwitcherClock` per switcher of the live scene and builds the `SwitcherSelection` each tick.
- **Frame switcher:**
  - its input index is the existing ScenePool **item**, so the pool's prepare-ahead, `ActivateItem` and `TriggerItem` keep working unchanged;
  - by C4 there's at most one Frame switcher on the active path.
- **Video switcher:**
  - `VideoManager` takes the live plan's `source` and the next selection's `source` per scene, in place of its own playlist state;
  - the next video waits paused at its start point, as now.
- **Entering a scene** starts every switcher at its first wired input, as today.
- **Double-clicking** a Frame input calls `TriggerItem`; double-clicking a Video input jumps the video and restarts that switcher's interval.

### N5.2 Node

- **Controls:** the interval value and unit, the shuffle toggle, and one row per input, each with a pin, a "passing" marker and the time to the next switch on the live row.
- **The trailing empty input** is kept by `NormalizeSwitcherInputs`.

### N5.3 Removed

- `Engine::PlaylistState`, `Engine::UpdatePlaylist`, `PickNextItem`;
- `VideoManager` playlist cycling;
- the legacy structs, except in the v2 parser.

### N5 done when

- [ ] `SwitcherClock` tests cover order, shuffle, empty inputs, the three units, waiting for readiness, jumping, and two switchers in series.
- [ ] Smoke runs on the fixture show's converted playlist scenes (`scene-14-16`, `cip-1`, `scene-13`, `scene-14`) behave as before: switch times are logged and held ticks are recorded.
- [ ] A Frame switcher with three differently-looked chains switches cleanly. You check this in the real app.

---

## 4. Risks specific to implementation

| Risk | Where | Mitigation |
| --- | --- | --- |
| The bridge (D1) has subtle round-trip gaps, so a Scene panel edit changes something unrelated | N1 | A round-trip test over every fixture scene and generated cases, and the equivalence test against today's `VideoSettingsFor` |
| The GL test can't create a context in some environments | N2 | It's skipped with a message, never failed silently. Smoke runs and your visual check remain. |
| The clamp order in the chain shader differs subtly from today's | N2 | §N2.1, proven by the 200-case pixel test |
| projectM resolves textures in a way the cache insert doesn't catch | N3 | Decided in the first two days, with the re-prepare fallback ready |
| The node library fights ImGui 1.92 popups or zoom | N4 | Both candidates are built in the spike before committing to one |
| Shell.cpp churn conflicts with other work | N1, N4 | No other work is planned in parallel; milestones merge one at a time |

## 5. Not in this plan

- Copy and paste of nodes, box selection, and comments on the canvas.
- New effect nodes.
- Saving the canvas zoom and pan.
- A web player. It would read the v3 format later; see `web-player-notes-2026-11-08.md`.
