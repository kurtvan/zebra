# M1 compile spike: results and decision

*Measured 2026-10-22 on the author's M1 Pro MacBook Pro (macOS 26.5, OpenGL 4.1 Metal-90.5), 1920×1080, projectM v4.1.8
plus patch 0001, Cream of the Crop library. Raw data: `results/*.csv`.*

## Decision

**Lanes.** Every projectM instance lives on a *lane*: a worker thread with its own GL context in the output
context's share group. An instance is created, prepared, rendered and destroyed on one lane and never moves.
The lane hosting the live scene renders it each output tick into one of three shared textures and publishes a GL
fence; the output thread waits on the fence on the GPU and draws the latest frame. Scenes are prepared only on
lanes that aren't live.

- **No projectM patch is needed to move objects between contexts.** Strategies S1 (CPU/GL split) and S2 (shared
  context plus VAO/FBO re-attach) from the plan are dropped. See "Why not S1/S2" below.
- **Pre-warm at performance load:** prepare every scene's preset once, throwaway, so the driver's shader cache is
  hot before the show (cold → warm: load 30 → 19 ms, first frame 28 → 6 ms median).
- **Flag heavy presets:** about 15% of the library can't reach 60 fps at any resolution on this machine (CPU-bound
  expression code). The app should measure and show this; no loading strategy fixes it.

## 1. Where preset load time goes (`--mode timing`, 150 presets stratified across categories)

Wall time on the calling thread, after the shader cache is warm (run 2):

| Phase | p50 | p90 | max |
|---|---|---|---|
| `projectm_load_preset_file` total | 20.1 ms | 25.0 ms | 31.8 ms |
| · parse file | 0.2 | 0.3 | 1.5 |
| · eval compile + init code | 0.4 | 1.0 | 13.5 |
| · HLSL→GLSL transpile (CPU) | 4.3 | 6.5 | 10.0 |
| · GL compile + link (`gl.compileprogram`, ~9 programs per preset) | 12.7 | 14.9 | 17.6 |
| First frame after load | 6.4 | 146.7 | 679 |
| Steady frame (frames 40–60) | 2.6 | 39.2 | 183 |

- About 6 of the ~9 GL programs compiled per load are projectM's built-in shaders (`PresetState`,
  `PerPixelMesh`, `BlurTexture`, `MotionVectors` constructors), recompiled for every preset. A future patch could
  cache them; it isn't needed with lanes.
- `Shader::CompileProgram` queries `GL_LINK_STATUS` straight away, so link time is paid synchronously.
- **Deferred driver work:** with a cold cache, the first frame costs a median of 28 ms (p90 239 ms) on top of the
  load. Frames 2+ are already at steady state, so **one or two warm-up draws absorb it.**

### Shader cache (`--passes 2`, fresh sample with seed 7)

| | load p50 | first frame p50 | first frames > 16.7 ms |
|---|---|---|---|
| Cold (never seen) | 30.1 ms | 28.5 ms | 62% |
| Same process, 2nd load | 19.3 ms | 5.9 ms | 26% |
| New process, presets seen in an earlier run | 20.2 ms | 6.4 ms | 26% |

The macOS Metal shader cache persists **across processes**. The remaining 26% are almost all the heavy presets below.

### Heavy presets

23 of 150 (15%) take 40–183 ms per frame at steady state. Identical times at 640×360 (`run2-360p.csv`) show they
are **CPU-bound** (per-frame/per-point expression code), not GPU-bound. Examples: `amandio c - epicenter zero
improvement hackery` (183 ms), `Stahlregen & Boz - Machine Code (Reaction Diffusion)` (156 ms), and many around 95 ms.

## 2. Memory

**34 MB per loaded instance at 1080p** (physical footprint delta over 8 instances). The 5 GB budget is no
constraint on the pool: 6 prepared scenes ≈ 200 MB.

## 3. Does background GL work disturb the output thread? (`--mode lanes`)

Main thread rendering a light preset (2.6 ms/frame) while a worker on a shared context prepares 79 presets:

| Worker does | main frames > 16.7 ms | main p99 | main max |
|---|---|---|---|
| nothing (baseline) | 0% | 3.6 ms | 4.0 ms |
| load only | 1.8% | 17.8 | 18.6 |
| draw only (one heavy preset) | 0.6% | 10.3 | 76.0 |
| load + 2 warm-up draws | 4.5% | 27.4 | 38.2 |

Background GL work does interfere. The driver serialises some work across contexts. That's why the live scene
must not be rendered on a thread that also prepares, and why the output thread itself should do almost no GL work.

## 4. Lanes pipeline prototype (`--mode pipeline`, simulated 60 Hz output)

| Scenario | dropped output ticks | cuts not ready | cut latency |
|---|---|---|---|
| Baseline: 1 light scene, 60 s, nothing prepared | 0.22% | — | — |
| 40 light scenes (steady ≤ 4 ms), cut every 2 s, 2 lanes | 0.27% | 0 | 1 tick (p50) |
| 30–60 mixed scenes (steady ≤ 12 ms), cut every 2 s, 2–3 lanes | 0.17–0.62% | 0 | 1 tick p50, 2–3 max |
| 60 mixed scenes, cut every 0.5 s (preparing constantly), 2 lanes | 0.89% | 0 | 1 tick p50, 2 max |

- Output-thread work per tick: 0.2–0.4 ms median.
- A dropped tick means the output re-shows the previous frame once. The baseline's 0.22% is the prototype's own
  timer and thread-wake jitter (`sleep_until` ticks instead of vsync).
- Preparation interferes far less in this topology than in §3: no cut waited for a load, and drop rates stay within
  about 2× baseline even under constant preparation.
- Cut latency is one tick by design (the lane renders tick *N+1* while the output shows *N*).

## Why not S1 / S2

- **S1 (CPU work on a thread, GL sliced on the render thread)** only moves about 5 ms (parse, eval, transpile). The GL
  compile/link (about 13 ms) and the driver's deferred first-draw compile (up to hundreds of ms cold) stay on the
  render thread. It can't reach zero-stutter cuts.
- **S2 (build on a worker context, re-create VAOs/FBOs on the render context)** needs a patch touching about 10
  projectM classes. It would still have the worker interfering with the render thread (§3), and gains nothing over
  lanes, which need no such patch.

## Notes for M2

- Lanes per engine: 2 are enough; use 3 for headroom (out-of-order jumps while the next scene is preparing).
- Feed PCM to every pooled instance on its lane (only the live one renders).
- Warm-up: 2 draws into a lane-local scratch target during prepare. Record the steady frame time there to flag
  heavy presets.
- projectM's `time` variable counts from instance creation, so a scene prepared well in advance starts with `time`
  > 0. Acceptable; if it ever matters, add a tiny patch to reset the instance's TimeKeeper on activation.
- No locking patch is needed for projectm-eval: libprojectM's mutex hooks are empty, but each preset has its own
  memory buffer and an instance only ever runs on one lane thread.
- Presets referencing textures that aren't installed log "texture unloadable" and sample black. Cream of the Crop
  has a separate texture pack; see the open question in the PR.

## Reproducing

```sh
cmake -S . -B build-spike -DGV_SPIKE=ON -DGV_BUILD_TESTS=OFF && cmake --build build-spike -j --target gv_spike_compile
B=build-spike/tools/spike_compile/gv_spike_compile
$B --count 150 --csv tools/spike_compile/results/run2-1080p.csv               # load phases + memory
$B --count 100 --seed 7 --passes 2 --memory-instances 0                         # shader cache
$B --mode lanes --worker-mode both --count 80 --active "<light preset, relative>"   # interference
$B --mode pipeline --scenes-csv tools/spike_compile/results/run2-1080p.csv --max-steady 12 --count 60 --lanes 2 --seconds-per-scene 2
```
