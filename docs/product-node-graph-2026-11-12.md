# Node Graph: Product Definition

*Drafted 2026-11-12 from `proposal-new-features-nov-11-2-26.md` and the answers to its clarifying questions; revised 2026-11-13 from review comments on PR #34. Companion document: `technical-node-graph-2026-11-12.md`.*

## Purpose

Replace each scene's fixed preset-and-video settings with a small **node graph** the artist wires by hand. The first version does what the app already does, laid out as nodes, plus four things it can't do today:

- inject video into a preset at **four different layers**, not only the feedback and the overlay;
- feed **one video into several layers**, each with its own look;
- switch between several inputs on a clock with a **switcher** node, replacing preset and video playlists;
- set the **output canvas size** for the whole show.

The user is still a non-technical visual artist: no text, no code, and no way to wire a scene into a broken state.

## Decisions

| Question | Decision |
| --- | --- |
| Where a graph lives | One graph per scene. Cutting to a scene swaps the whole graph. |
| How freely nodes wire | Chains with one preset at a time. Several effect nodes in any order; one video can branch into several chains. |
| Effect nodes in the first version | Only today's: luma gate (with invert and softness), contrast, saturation, mix. |
| Injection layers | All four: feedback before warp, before composite, on top, and as a preset texture. |
| Playlists | Replaced by a **switcher** node: several inputs collapse into one output, and a clock decides which one passes through. |
| Switcher transitions | Cut, as today, with the next input prepared ahead. |
| Viewport | A per-show output canvas size, shown as the end node of every scene's graph. |
| Where the editor appears | A node canvas for the selected scene replaces the Scene panel's video section. |
| Where node controls live | On the node itself, like an Ableton Live device: each node's sliders and menus sit in its own body. |
| Invalid wiring | Refused with an error message. A graph is never left in an invalid state. |
| Editing a live scene | Everything applies live, including rewiring, with no stutter. |
| Canvas above 1920 wide | Allowed, with a warning when frames run late. |
| Texture layer | Lists the preset's textures by name, each with a thumbnail. |
| Deadline | None; milestones follow dependencies. |

## Core concepts

| Concept | Definition |
| --- | --- |
| **Graph** | A scene's nodes and the wires between them. Every scene has exactly one. |
| **Node** | One step: a source, an effect, a preset, a switcher or the viewport. Inputs on its left, outputs on its right, and its controls on its body. |
| **Wire** | Connects an output to an input of the same kind. An output can have several wires; an input takes one. |
| **Layer** | Where a preset node takes in video. Each preset node has one input per layer. |
| **Switcher** | A node with several inputs and one output; its clock picks which input passes through. |
| **Canvas** | The show's output size, e.g. 1920×1080 or 3840×1080. |

## Nodes

Every node looks and behaves like an Ableton Live device:

- a title bar with the node's name and a fold button;
- its controls laid out in its body;
- its inputs and outputs on its edges.

Folding a node shrinks it to its title bar, so large graphs stay readable. The values still apply while it's folded.

### Video source

- **Controls:** a pool video or Syphon sender picker, **Restart on entry** and **Loop** (file videos), and the pool video's start point, speed and fit.
- **Output:** one, **Video**, which can wire to several chains (fan-out).

### Effect nodes

Each takes **Video** in and gives **Video** out, so they chain in any order.

| Node | Controls on the node | Notes |
| --- | --- | --- |
| **Contrast** | Contrast (0–100, 1 = unchanged) | As today. |
| **Saturation** | Saturation (0–2, 0 = greyscale), Black and white shortcut | As today. |
| **Luma gate** | Threshold, Softness, Invert | Keeps the bright parts (or the dark, inverted); Softness feathers the edge. Today's overlay softness and the feedback gate become one node. |
| **Mix** | Opacity (0–1) | How strongly the video shows in the layer it reaches. |

The artist now chooses the order, and it matters. Today the order is fixed: contrast and saturation first, then the gate. Converted shows keep that order.

### Preset

- **Controls:** the preset's name, a small live preview, and a button to pick another preset. Dragging a preset from the Presets tab onto the node also replaces it.
- **Output:** one, **Frame**, the finished picture.
- **Inputs:** four video inputs, one per layer. Any can be left empty.

| Layer | What the video does |
| --- | --- |
| **Feedback** | Mixed into the previous frame before the preset's warp, so the preset's motion smears and distorts it. Today's video blend. |
| **Before composite** | Added after the preset's warp, waves and shapes. The composite shader colours and distorts it, but it doesn't smear over time. |
| **On top** | Drawn crisp over the finished frame. Today's Overlay on top. |
| **Texture** | Replaces one of the preset's texture images, so the preset's shaders sample the video. One input per texture the preset uses, each shown with its name and a thumbnail from the texture folder. It only affects presets that use textures. |

### Switcher

```
input 1 ─┐
input 2 ─┼─[ clock ]─ output
input 3 ─┘
```

- **Inputs and output:** several inputs collapse into one output, and the switcher's clock decides which input passes through. Inputs and output are the same kind, either all **Frame** or all **Video**. There's always one empty input at the bottom; wiring into it adds another.
- **Controls:** the interval (every N seconds, beats or bars), the order (in order or shuffled), and a marker on the input that's currently passing.
- **Clock:** today's playlist rules apply. Beats and bars follow the MIDI clock and change only on bar lines.
- **What feeds it:** whatever is wired into its inputs, laid out on the canvas like any other nodes. Each input can carry its own preset and its own look.
  - A **Frame switcher** fed by several preset chains replaces today's preset playlist with per-preset looks.
  - A **Video switcher** fed by several video sources replaces the video playlist.
  - One preset fed by a Video switcher of several looks gives "same preset, different looks".
- **Cuts are clean:** the next input's preset and video are prepared before the switch. If one isn't ready at the bar line, the switch waits for the next bar, as today.
- **Jumping:** double-clicking an input's marker goes live on it, and the clock carries on from there.

### Viewport

- **Input:** it's the end node of every scene's graph and can't be deleted. It takes the **Frame** to show.
- **Controls:** these set the **show's canvas size**:
  - **Match screen** (today's behaviour, and the default);
  - a list of sizes: 1920×1080, 1280×720, 3840×2160, 3840×1080;
  - or a custom width and height.
- **One canvas per show:** changing it in one scene changes it for the whole show, and the node says so.
- **Fitting the screen:** the canvas is scaled to fit the output screen, with black bars when the shapes differ.
- **Above 1920 wide:** a canvas wider than 1920 renders at full size. If frames start running late, a warning shows on the node and in the frame-time readout.

## The editor

- **Layout:** the Scene panel's video section is replaced by the selected scene's **node canvas**, which can be panned and zoomed. The scene's name, CC value and beat hits stay in the Scene panel.
- **Controls:** these are on each node, so editing is done on the canvas. There's no separate inspector.
- **Adding nodes:** right-click the canvas, or drag a preset or video onto it.
- **Wiring:** drag from an output to an input. While dragging, inputs that can't take the wire are dimmed. A second wire into a taken input replaces the first.
- **Invalid edits are blocked:** an edit that would break the graph is refused, and a short message says why. Nothing changes and the scene keeps playing. Refused edits include:
  - a loop;
  - wires between different kinds;
  - two presets or two videos active at once;
  - a switcher feeding a switcher of the same kind.
- **Unfinished graphs:** these are allowed, so a graph can be built step by step. A viewport with nothing wired into it shows black and says "Nothing connected" on the node.
- **Everything is live:** changing values, adding, removing and rewiring nodes on the live scene apply on the next frame, without a stutter. Swapping the preset on a live preset node behaves like a cut to it: it plays as soon as it's prepared.
- **Undo:** every edit is one undo step, as today.

## Existing shows

Shows saved before this change open automatically as graphs that look and behave exactly as before. Saving writes the new format, and the app still opens the old one.

| Today | Becomes |
| --- | --- |
| Preset | Preset node into the viewport |
| Video with its look | Video source → Contrast → Saturation → Luma gate → Mix, into the preset's Feedback layer |
| Invert luma gate | Invert on the Luma gate node |
| Overlay on top, Softness | A second chain from the same video (Contrast → Saturation → Luma gate with that softness → Mix), into the On top layer |
| Preset playlist with per-preset looks | A Frame switcher with one input per preset. Each preset is fed by its own look chain from the shared video |
| Video playlist | A Video switcher of video sources, feeding the look chain |

## Out of scope for the first version

- New effect nodes (transform, colour, chroma key). These are candidates for later, along with the ideas in the TouchDesigner comparison doc.
- Several presets rendering at once, blend nodes between presets, and crossfades.
- Switchers feeding switchers of the same kind (a switch inside a switch).
- One graph for the whole performance, or a shared output graph.
- Per-scene canvas sizes, cropping and multi-display output.
- Beat hits as a node; they stay a scene setting.
